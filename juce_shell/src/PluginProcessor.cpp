#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HyphaClockSourceContract.h"
#include <algorithm>
#include <cmath> // B-107: std::abs(float) for the silence peak threshold
#include <limits>

namespace
{
    static_assert (sizeof (KirinMeterSession) == 832u,
                   "Rust/C++ Meter Session ABI size must remain exact");
    static_assert (sizeof (KirinObservatoryFrame) == 912u,
                   "Rust/C++ Observatory frame ABI size must remain exact");
    static_assert (sizeof (KirinMeterHistoryEntry) == 176u,
                   "Rust/C++ Meter history ABI size must remain exact");
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    static_assert (static_cast<std::uint8_t> (hypha::pre_display::ClockSource::unknown)
                       == KIRIN_HYPHA_CLOCK_UNKNOWN);
    static_assert (static_cast<std::uint8_t> (hypha::pre_display::ClockSource::projectTimeline)
                       == KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE);
    static_assert (static_cast<std::uint8_t> (hypha::pre_display::ClockSource::audioRenderTimeline)
                       == KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE);
#endif

    // Logic stopped-state fix: expose Inactive PRE/POST presence without waiting for the first audio callback.
    // The 50 ms Timer grants a bounded state-restore window before enabling from prepareToPlay.
    constexpr int kPrepareEnableDelayTicks = 10;

    // B-125 (b): prealloc-max headroom (frames). The interleave scratch is sized in
    // prepareToPlay to max(maximumExpectedSamplesPerBlock, this) frames so that realistic
    // variable / offline-render blocks larger than the realtime-declared block are still
    // measured without a (non-RT-safe) reallocation on the audio thread. Hosts can deliver
    // offline / freeze / bounce blocks well above the realtime maximum; 262144 frames
    // (~5.46 s @ 48 kHz) absorbs large offline chunks while keeping the one-time, non-RT
    // prepareToPlay allocation bounded at 262144 * numCh * sizeof(float) (≈2 MB stereo).
    // Pathological blocks beyond this ceiling are not reallocated; their frames are counted as
    // oversized drops (B-125 (c) / kirin_hypha_note_oversized_drop) while audio passes through.
    constexpr int kOversizeHeadroomFrames = 262144;
    // C ABI signal-state codes: 0 = Inactive, 1 = Active, 2 = Bypassed.
    uint8_t resolveSignalStateCode (bool bypassed,
                                    bool playing,
                                    bool silent,
                                    bool recording,
                                    bool nonRealtime)
    {
        if (bypassed)
            return 2;
        // B-205: silence gates the displayed Active state even during Record. A held Record
        // session with the transport stopped (playing=false, silent=true) reports Inactive
        // instead of showing stale near-zero residue as a bogus ~-400 LUFS/TP. Record capture is
        // deliberately decided by shouldCaptureBufferForMeasurement() below, so silent
        // non-realtime Record buffers can still advance the Record timeline without forcing the
        // display state to Active.
        // B-230: Studio One can render bounce audio with playing=false before the PRE side has
        // observed the POST Record signal. Non-realtime non-silent buffers are real audio and
        // must feed the meter/Record path, otherwise PRE can close with frames=0 while POST has data.
        if (! silent && (recording || playing || nonRealtime))
            return 1;
        return 0;
    }

    bool shouldCaptureBufferForMeasurement (uint8_t stateCode,
                                            bool bypassed,
                                            bool recording,
                                            bool playing,
                                            bool positionChanged,
                                            bool nonRealtime)
    {
        return ! bypassed
            && (stateCode == 1 || (recording && (playing || positionChanged || nonRealtime)));
    }

}

KirinHyphaProcessorBase::KirinHyphaProcessorBase (Role roleIn)
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
          .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      role (roleIn)
{
    // Host bypass routed through this parameter; processBlock reads it to set the
    // Bypassed signal state while still passing audio through (parity with hypha_pre).
    addParameter (bypassParam = new juce::AudioParameterBool ({ "bypass", 1 }, "Bypass", false));

    // The non-RT enable timer starts only after prepareToPlay creates a fresh engine and stops as
    // soon as writes are enabled. An instantiated-but-never-prepared plugin owns no periodic work.
}

KirinHyphaProcessorBase::~KirinHyphaProcessorBase()
{
    stopTimer(); // B-126: stop the non-RT enable poll before teardown (was cancelPendingUpdate / B-070).
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    preDisplayController.reset();
#endif
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
    {
        if (role == Role::Post)
        {
            kirin_hypha_set_spectrum_visible (hyphaHandle, false);
            kirin_hypha_set_internal_attack_enabled (hyphaHandle, false);
        }
        kirin_hypha_destroy (hyphaHandle);
        hyphaHandle = nullptr;
    }
}

void KirinHyphaProcessorBase::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = getTotalNumInputChannels();
    if (role == Role::Post && numCh != 2
        && preferredSpectrumChannelMode.load (std::memory_order_acquire)
            == KIRIN_SPECTRUM_CHANNEL_SIDE)
    {
        preferredSpectrumChannelMode.store (
            KIRIN_SPECTRUM_CHANNEL_LR, std::memory_order_release);
    }

    // Pre-allocate the interleave scratch so processBlock never allocates (RT-safe).
    // B-125 (b): prealloc-max — size to max(declared block, kOversizeHeadroomFrames) frames
    // so realistic variable / offline-render blocks above the realtime maximum are absorbed
    // without an audio-thread realloc. This .assign runs in prepareToPlay (non-RT) — allowed.
    const int   maxFrames = juce::jmax (juce::jmax (0, samplesPerBlock), kOversizeHeadroomFrames);
    interleaveScratch.assign ((size_t) maxFrames * (size_t) juce::jmax (1, numCh), 0.0f);
    // B-125: cache the prepared capacity so processBlock re-checks against it (the oversized
    // fallback fires only for blocks beyond this) without re-deriving from samplesPerBlock.
    scratchCapacitySamples = interleaveScratch.size();

    const juce::ScopedLock sl (handleLock);
    // B-141: Studio One offline bounce can call prepareToPlay again after All Keep has entered
    // Record. The maximumExpectedSamplesPerBlock may change for render, but the user-visible
    // Record state must not be thrown away. Reuse the Rust engine when the audio format is the same.
    //
    // B-334: sample-rate / channel-count reprepare is also not Stop authority while Record is
    // armed. Destroying the Rust engine here closes the PRE writer through Drop/shutdown before
    // POST All Stop, which presents as "PRE detached mid-KEEP". Defer incompatible rebuilds until
    // the host calls prepareToPlay outside Record.
    const bool needsNewHandle = hyphaHandle == nullptr
                             || std::abs (preparedSampleRate - sampleRate) > 0.001
                             || preparedInputChannels != numCh;
    if (! needsNewHandle)
        return;

    if (hyphaHandle != nullptr && kirin_hypha_is_recording (hyphaHandle))
        return;

    lastProcessPositionValid = false;
    lastProcessHadPosition = false;
    lastProcessNumFrames = 0;
    watchSilenceGate.reset();

    if (hyphaHandle != nullptr)
    {
        kirin_hypha_destroy (hyphaHandle);
        hyphaHandle = nullptr;
    }

    // num_channels: pass the actual negotiated input channel count. Mono must remain 1ch
    // all the way into the meter; duplicating to stereo would bias loudness by +3.01 dB.
    hyphaHandle = kirin_hypha_create ((uint32_t) sampleRate, (uint32_t) numCh);
    preparedSampleRate = hyphaHandle != nullptr ? sampleRate : 0.0;
    preparedInputChannels = hyphaHandle != nullptr ? numCh : 0;

    // A fresh handle receives the current entitlement immediately. Further refreshes are tied to
    // editor open / explicit Keep / pair-menu actions; there is no steady-state disk polling.
    // set_identity + enable_*_writes are deferred to the message-thread Timer
    // (enableWritesNow) so any setStateInformation restore is applied before enable.
    if (hyphaHandle != nullptr)
    {
        const uint8_t lic = kirin_hypha_load_license();
        cachedLicenseCode.store ((int) lic, std::memory_order_release);
        kirin_hypha_set_license (hyphaHandle, lic);
        // A recalled Studio Pro session may deliver setActive(false) before prepareToPlay creates
        // the Rust engine. Apply the retained host fact to every fresh handle so an insert that
        // was already OFF at project-open reaches the same ABS state as an explicit live click.
        kirin_hypha_set_host_component_active (hyphaHandle, hostComponentActive);
        writesEnabled.store (false, std::memory_order_release);
        // Logic stopped-state fix: re-prepare needs a fresh enable, but Logic may not call processBlock until
        // playback. Start a message-thread fallback so Inactive presence/candidates are published
        // even while stopped. If setStateInformation already arrived for this instance, skip the
        // grace delay; otherwise keep the window so project recall can restore identity before the
        // io_thread snapshots it.
        const int restoreDelay = stateInformationSeen.load (std::memory_order_acquire) ? 0 : kPrepareEnableDelayTicks;
        enableDelayTicks.store (restoreDelay, std::memory_order_release);
        enablePending.store (true, std::memory_order_release);
        startTimer (50);
    }
    // A null handle (create failure) is tolerated; processBlock / pollMeasureResult guard on it.
}

void KirinHyphaProcessorBase::releaseResources()
{
    // B-141: releaseResources is an audio lifecycle callback, not user intent. Hosts may call it
    // around offline bounce/freeze; destroying the engine here drops Record state and makes the
    // editor fall back to Watch/Keep mid-bounce. Keep the handle alive until destructor or an
    // incompatible prepareToPlay rebuild.
    // Offline bounce end is not user intent either. Record stop authority stays with explicit
    // Stop/All Stop and the IO-thread idle timeout; this callback intentionally does nothing.
}

void KirinHyphaProcessorBase::updateTrackProperties (const TrackProperties& properties)
{
    // JUCE specifies this callback on the message thread. Keep only the host's human-readable
    // label; colour and all implementation identity remain outside Capture.
    hostTrackDisplayName = properties.name;
}

void KirinHyphaProcessorBase::hostComponentActivationChanged (bool active)
{
    // VST3 IComponent::setActive is distinct from transport/silence. Studio Pro uses this path
    // when the insert power button is changed, while releaseResources alone is too ambiguous
    // (sample-rate reconfigure / offline render / teardown). Rust applies the shared heartbeat
    // grace before publishing Bypassed, so transient host reconfiguration remains Inactive.
    const juce::ScopedLock sl (handleLock);
    hostComponentActive = active;
    if (hyphaHandle != nullptr)
        kirin_hypha_set_host_component_active (hyphaHandle, active);
}

bool KirinHyphaProcessorBase::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    return mainOut == juce::AudioChannelSet::mono()
        || mainOut == juce::AudioChannelSet::stereo();
}

void KirinHyphaProcessorBase::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh     = getTotalNumInputChannels();
    const int numOut    = getTotalNumOutputChannels();
    const int numFrames = buffer.getNumSamples();

    // R-12: read-only passthrough. Never write to signal channels; only clear surplus
    // output channels with no matching input (no-op when in == out).
    for (int ch = numCh; ch < numOut; ++ch)
        buffer.clear (ch, 0, numFrames);

    // Not locked on the audio thread: JUCE suspends processing around prepare/release,
    // so hyphaHandle is stable here. (The editor poll and create/destroy use handleLock.)
    if (hyphaHandle == nullptr)
        return;

    // B-070/B-126: enable writes once, deferred to the message-thread Timer so any
    // setStateInformation (identity / pair name restore) can win before the io_thread snapshots
    // path identity. The audio thread does ONLY lock-free atomics (no alloc/lock/syscall).
    if (! writesEnabled.load (std::memory_order_acquire))
    {
        if (stateInformationSeen.load (std::memory_order_acquire))
            enableDelayTicks.store (0, std::memory_order_release);
        enablePending.store (true, std::memory_order_release);
    }

    // --- Signal state derivation (parity: hypha_pre.rs:397-403) -------------------
    const bool bypassed = (bypassParam != nullptr && bypassParam->get());

    bool playing = false;
    bool hasPosition = false;
    uint8_t clockSource = KIRIN_HYPHA_CLOCK_UNKNOWN;
    int64_t positionSamples = 0;
    bool hasClockEnd = false;
    int64_t clockStartSamples = 0;
    int64_t clockEndSamples = 0;
    uint8_t presentationSource = KIRIN_HYPHA_PRESENTATION_SOURCE_UNKNOWN;
    bool inputPresentationValid = false;
    uint32_t inputPresentationSamples = 0;
    bool outputPresentationValid = false;
    uint32_t outputPresentationSamples = 0;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (const auto timeSamples = pos->getTimeInSamples())
            {
                hasPosition = true;
                positionSamples = *timeSamples;
                clockSource = KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE;
               #if KIRIN_HYPHA_AU_CLOCK_PROVENANCE
                // Processor.cpp is shared by the AU and VST3 products. JucePlugin_Build_AU is
                // therefore true in this translation unit even for a VST3 instance and cannot
                // identify the active wrapper. Read the AU-only provenance marker only for an
                // actual Audio Unit v2 instance; VST3 always keeps the host project timeline.
                if (wrapperType == juce::AudioProcessor::wrapperType_AudioUnit
                    && hypha::clock_source_contract::audioUnitV2UsesRenderTimeline (
                        pos->getKirinAuUsesHostTransportTimeline()))
                    clockSource = KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE;
               #endif
            }
           #if KIRIN_HYPHA_PRESENTATION_CLOCK
            const auto wrapperSource = pos->getKirinPresentationLatencySource();
            if (wrapperSource == KIRIN_HYPHA_PRESENTATION_SOURCE_VST3
                || wrapperSource == KIRIN_HYPHA_PRESENTATION_SOURCE_AUDIO_UNIT_V2)
                presentationSource = (uint8_t) wrapperSource;
            const auto readPresentationLatency = [] (const auto& value, bool& valid, uint32_t& samples)
            {
                if (value.hasValue() && *value >= 0
                    && *value <= (int64_t) std::numeric_limits<uint32_t>::max())
                {
                    valid = true;
                    samples = (uint32_t) *value;
                }
            };
            readPresentationLatency (pos->getKirinInputPresentationLatencySamples(),
                                     inputPresentationValid, inputPresentationSamples);
            readPresentationLatency (pos->getKirinOutputPresentationLatencySamples(),
                                     outputPresentationValid, outputPresentationSamples);
           #endif
        }
    // JUCE exposes loop points in PPQ, not the exact exported WAV sample range. Do not
    // promote those values to wav_clock_native; render span remains a lower-trust fallback
    // until a host-supplied native sample range exists.
    lastPlaying.store (playing, std::memory_order_release); // B-054: POST pair lock reads this
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    preDisplayClock.publish (positionSamples, preparedSampleRate,
                             static_cast<std::uint32_t> (juce::jmax (0, numFrames)), playing,
                             static_cast<hypha::pre_display::ClockSource> (clockSource));
#endif
    const bool positionChanged = hasPosition && lastProcessPositionValid
                              && positionSamples != lastProcessPositionSamples;
    const bool watchSampleTimelineStartedNewPass =
        hypha::signal_state_contract::WatchSilenceGate::sampleTimelineStartsNewPass (
            hasPosition,
            lastProcessPositionValid && lastProcessHadPosition,
            positionSamples,
            lastProcessPositionSamples,
            lastProcessNumFrames);
    if (hasPosition)
    {
        lastProcessPositionValid = true;
        lastProcessPositionSamples = positionSamples;
        lastProcessNumFrames = (uint64_t) juce::jmax (0, numFrames);
    }
    lastProcessHadPosition = hasPosition;

    const bool silent = bufferIsSilent (buffer);
    const bool recording = kirin_hypha_is_recording (hyphaHandle);
    if (! recording)
    {
        recordStartWindowLatched = false;
        recordNativeRangeLatched = false;
    }
    const bool nonRealtimeMode = isNonRealtime();
    // Some AU hosts omit the optional transport callback. A valid AudioUnit render timestamp is
    // still an exact processing timeline, so non-silent Watch audio and an already-started Record
    // can advance on it. Clock availability alone is not transport authority: otherwise silent
    // idle callbacks between Keep and Drop would be captured as Record pre-roll.
    const bool measurementTimelineActive = playing
                                        || clockSource == KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE;
    lastMeasurementTimelineActive.store (measurementTimelineActive, std::memory_order_release);
    const uint8_t previousSignalState = kirin_hypha_get_signal_state (hyphaHandle);
    const bool watchAvailabilityBoundary = ! recording
                                        && ! bypassed
                                        && measurementTimelineActive
                                        && previousSignalState != KIRIN_SIGNAL_STATE_ACTIVE;
    // A single silent callback used to collapse PRE to Inactive, clear its measurement engine,
    // and make the paired POST lose its delta for one or more UI ticks. Preserve Watch continuity
    // across musical rests shorter than the exact 3 s LUFS-S window. Transport stop, bypass, and
    // every Record/TRACE path remain outside this gate and retain their existing state rules.
    const bool watchActiveThroughSilence = watchSilenceGate.observeBlock (
        hypha::signal_state_contract::WatchSilenceGate::eligible (
            bypassed, recording, measurementTimelineActive),
        watchSampleTimelineStartedNewPass || watchAvailabilityBoundary,
        silent,
        (uint64_t) juce::jmax (0, numFrames),
        preparedSampleRate);
    const bool stateSilent = silent && ! watchActiveThroughSilence;
    int windowStartFrame = 0;
    int windowEndFrame = numFrames;
    int64_t windowPositionSamples = positionSamples;
    uint64_t windowNumFrames = (uint64_t) numFrames;
    if (recording && hasPosition)
    {
        const int64_t blockStart = positionSamples;
        const int64_t blockEnd = positionSamples + (int64_t) numFrames;
        const int64_t clockStart = hasClockEnd ? clockStartSamples : 0;
        const int64_t clippedStart = std::max (blockStart, clockStart);
        const int64_t clippedEnd = hasClockEnd ? std::min (blockEnd, clockEndSamples) : blockEnd;
        if (clippedEnd <= clippedStart)
        {
            windowStartFrame = 0;
            windowEndFrame = 0;
            windowPositionSamples = clippedStart;
            windowNumFrames = 0;
        }
        else
        {
            windowStartFrame = (int) (clippedStart - blockStart);
            windowEndFrame = (int) (clippedEnd - blockStart);
            windowPositionSamples = clippedStart;
            windowNumFrames = (uint64_t) (clippedEnd - clippedStart);
        }
    }

    // C ABI signal-state codes: 0 = Inactive, 1 = Active, 2 = Bypassed.
    const uint8_t stateCode = resolveSignalStateCode (bypassed, measurementTimelineActive,
                                                      stateSilent, recording, nonRealtimeMode);
    kirin_hypha_set_signal_state (hyphaHandle, stateCode);
    const bool watchAvailabilityStartedNewPass =
        hypha::signal_state_contract::availabilityStartsNewPass (
            previousSignalState, stateCode, recording);
    kirin_hypha_note_transport_block (hyphaHandle, measurementTimelineActive, hasPosition,
                                      positionSamples, (uint64_t) numFrames,
                                      watchAvailabilityStartedNewPass);
    // B-113: 旧 lastSignalState キャッシュは廃止。editor は signalStateLive()（FFI 直読 / heartbeat-aware）で表示分岐する。

    // --- Feed the engine ---------------------------------------------------------
    // Watch meters push when Active. Record only enters after a real rendered/replayed
    // window has established the native sample start; later silent position-advanced
    // windows may continue the already-started Record timeline.
    const bool captureBuffer = shouldCaptureBufferForMeasurement (stateCode,
                                                                  bypassed,
                                                                  recording,
                                                                  measurementTimelineActive,
                                                                  positionChanged,
                                                                  nonRealtimeMode);
    const bool recordStartCandidateWindow = captureBuffer
                                         && recording
                                         && hasPosition
                                         && windowNumFrames > 0
                                         && numCh > 0
                                         && (stateCode == 1 || playing || nonRealtimeMode || hasClockEnd);
    const bool renderedRecordWindow = recording
                                   && hasPosition
                                   && windowNumFrames > 0
                                   && numCh > 0
                                   && captureBuffer
                                   && (recordStartWindowLatched || recordStartCandidateWindow);
    // An explicit native WAV range is a producer TakeStart edge. Offline entry is intentionally
    // not derived from Record state here: the FFI observes the first fully-admitted offline Watch
    // callback, before Keep ACK, and preserves its raw pre-roll for WAV sample 0. Forcing the first
    // Record callback would discard that exact prefix and recreate the 7,676-sample skew class.
    const bool forceTakeStartEpoch = renderedRecordWindow
                                  && hasClockEnd
                                  && ! recordNativeRangeLatched;
    const bool pushBuffer = recording ? renderedRecordWindow : captureBuffer;
    kirin_hypha_note_record_window (hyphaHandle,
                                    recording,
                                    renderedRecordWindow,
                                    playing,
                                    nonRealtimeMode,
                                    hasPosition,
                                    windowPositionSamples,
                                    windowNumFrames,
                                    clockStartSamples,
                                    hasClockEnd,
                                    clockEndSamples);
    // push_samples advances heartbeat internally, so a 0-frame call is a heartbeat-only
    // keepalive for the non-captured Inactive / Bypassed case.
    if (pushBuffer)
    {
        const size_t needed = (size_t) windowNumFrames * (size_t) numCh;
        if (numCh > 0 && needed <= scratchCapacitySamples)
        {
            // Interleave [c0f0, c1f0, c0f1, c1f1, ...] — same order as nih-plug iter_samples.
            // getReadPointer only: the input buffer is never modified (R-12).
            size_t idx = 0;
            for (int f = windowStartFrame; f < windowEndFrame; ++f)
                for (int ch = 0; ch < numCh; ++ch)
                    interleaveScratch[idx++] = buffer.getReadPointer (ch)[f];

            kirin_hypha_note_capture_window (hyphaHandle, hasPosition,
                                             windowPositionSamples, windowNumFrames, clockSource,
                                             presentationSource,
                                             inputPresentationValid, inputPresentationSamples,
                                             outputPresentationValid, outputPresentationSamples,
                                             forceTakeStartEpoch);
            const bool blockAccepted = kirin_hypha_push_samples (
                hyphaHandle, interleaveScratch.data(),
                (size_t) windowNumFrames, (uint32_t) numCh);
            // Producer take boundaries are commit facts, not callback intentions. If the
            // complete audio+clock transaction is rejected, the next eligible callback must
            // still carry the same first-Record/native-range edge.
            if (blockAccepted && renderedRecordWindow)
            {
                recordStartWindowLatched = true;
                if (hasClockEnd)
                    recordNativeRangeLatched = true;
            }
        }
        else
        {
            // B-125 (c): a pathological block beyond prealloc-max (needed > scratch capacity).
            // We do NOT reallocate on the audio thread — instead the dropped frames are recorded
            // so the gap surfaces in dropped_samples / integrity_degraded (ZSA, no silent loss).
            // The note call is fetch_add-only (RT-safe; alloc/lock/syscall-free). The keepalive
            // push (nullptr,0) still advances the heartbeat; audio keeps passing through (R-12).
            if (numCh > 0 && needed > scratchCapacitySamples)
                kirin_hypha_note_oversized_drop (hyphaHandle, (uint64_t) needed);

            kirin_hypha_push_samples (hyphaHandle, nullptr, 0, (uint32_t) numCh);
        }
    }
    else
    {
        kirin_hypha_push_samples (hyphaHandle, nullptr, 0, (uint32_t) numCh);
    }
}

bool KirinHyphaProcessorBase::bufferIsSilent (const juce::AudioBuffer<float>& buffer)
{
    // B-107: silent iff peak < -140 dBFS. Parity with hypha_pre/hypha_post sample_is_silent:
    // linear threshold 10^(-140/20) = 1e-7, compared without log10 (RT-safe on the audio thread).
    static constexpr float kSilencePeakLinear = 1.0e-7f; // -140 dBFS
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* p = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs (p[i]) >= kSilencePeakLinear)
                return false;
    }
    return true;
}

juce::AudioProcessorEditor* KirinHyphaProcessorBase::createEditor()
{
    refreshLicenseForUserAction();
    return new KirinHyphaEditor (*this);
}

bool KirinHyphaProcessorBase::hasEditor() const          { return true; }

juce::AudioProcessorParameter* KirinHyphaProcessorBase::getBypassParameter() const
{
    return bypassParam;
}

bool KirinHyphaProcessorBase::pollMeasureResult (KirinMeasureResult& out) const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_poll_result (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollWatchDisplay (KirinWatchDisplay& out) const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_poll_watch_display (
        hyphaHandle,
        lastMeasurementTimelineActive.load (std::memory_order_acquire),
        &out);
}

bool KirinHyphaProcessorBase::pollRecordDisplay (KirinRecordDisplay& out) const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_poll_record_display (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollMeterSession (KirinMeterSession& out) const
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_meter_session (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollObservatoryFrame (KirinObservatoryFrame& out) const
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_observatory_frame (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollMeterHistory (
    uint8_t resolution,
    std::vector<KirinMeterHistoryEntry>& out,
    size_t maxEntries,
    size_t maxOutputEntries) const
{
    const auto boundedRange = std::min (maxEntries,
                                        static_cast<size_t> (KIRIN_METER_HISTORY_MAX_ENTRIES));
    const auto boundedOutput = std::min (maxOutputEntries, boundedRange);
    out.resize (boundedOutput);
    uint32_t count = 0;
    const juce::ScopedLock sl (handleLock);
    const auto ok = hyphaHandle != nullptr
                 && kirin_hypha_poll_meter_history_decimated (
                        hyphaHandle, resolution, static_cast<uint32_t> (boundedRange),
                        out.data(), static_cast<uint32_t> (boundedOutput), &count);
    if (! ok)
    {
        out.clear();
        return false;
    }
    out.resize (count);
    return true;
}

bool KirinHyphaProcessorBase::pollMeterDeltaHistory (
    uint8_t resolution,
    std::vector<KirinMeterHistoryEntry>& out,
    size_t maxEntries,
    size_t maxOutputEntries) const
{
    const auto boundedRange = std::min (maxEntries,
                                        static_cast<size_t> (KIRIN_METER_HISTORY_MAX_ENTRIES));
    const auto boundedOutput = std::min (maxOutputEntries, boundedRange);
    out.resize (boundedOutput);
    uint32_t count = 0;
    const juce::ScopedLock sl (handleLock);
    const auto ok = hyphaHandle != nullptr
                 && kirin_hypha_poll_meter_delta_history_decimated (
                        hyphaHandle, resolution, static_cast<uint32_t> (boundedRange),
                        out.data(), static_cast<uint32_t> (boundedOutput), &count);
    if (! ok)
    {
        out.clear();
        return false;
    }
    out.resize (count);
    return true;
}

bool KirinHyphaProcessorBase::resetMeterSession()
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_reset_meter_session (hyphaHandle);
}

void KirinHyphaProcessorBase::setUseShortTermLoudness (bool shortTerm)
{
    if (persistShortTermLoudness.exchange (shortTerm, std::memory_order_acq_rel) == shortTerm)
        return;
    updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setObservatoryDomainPreference (uint8_t value)
{
    const uint8_t bounded = value < 4u ? value : uint8_t { 0 };
    if (preferredObservatoryDomain.exchange (bounded, std::memory_order_acq_rel) != bounded)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setObservatoryTargetPreference (uint8_t value)
{
    const uint8_t bounded = value < 2u ? value : uint8_t { 0 };
    if (preferredObservatoryTarget.exchange (bounded, std::memory_order_acq_rel) != bounded)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

void KirinHyphaProcessorBase::setObservatoryTimeRangePreference (uint8_t value)
{
    const uint8_t bounded = value < 5u ? value : uint8_t { 0 };
    if (preferredObservatoryTimeRange.exchange (bounded, std::memory_order_acq_rel) != bounded)
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
}

#if KIRIN_HYPHA_GUIDE_TRANSPORT
hypha::pre_display::DisplaySnapshot KirinHyphaProcessorBase::preDisplaySnapshot() const
{
    return preDisplayController != nullptr
        ? preDisplayController->displaySnapshot()
        : hypha::pre_display::DisplaySnapshot {};
}

hypha::pre_display::GuidePresentationSnapshot
KirinHyphaProcessorBase::guidePresentationSnapshot() const
{
    return preDisplayController != nullptr
        ? preDisplayController->guidePresentationSnapshot()
        : hypha::pre_display::GuidePresentationSnapshot {};
}

hypha::pre_display::ConnectionRequest KirinHyphaProcessorBase::pendingPreDisplayConnection() const
{
    return preDisplayController != nullptr
        ? preDisplayController->pendingConnection()
        : hypha::pre_display::ConnectionRequest {};
}

bool KirinHyphaProcessorBase::acceptPreDisplayConnection()
{
    return preDisplayController != nullptr && preDisplayController->acceptPendingConnection();
}

juce::String KirinHyphaProcessorBase::connectedWorkTitle() const
{
    return preDisplayController != nullptr
        ? preDisplayController->connectedWorkTitle() : juce::String {};
}
#endif

// --- B-072: POST pairing surface ---------------------------------------------------------

bool KirinHyphaProcessorBase::isRecording() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_is_recording (hyphaHandle);
}

void KirinHyphaProcessorBase::setPairName (const juce::String& name)
{
    if (persistPairName != name)
    {
        persistPairInstanceId.clear();
        persistPairProjectHash.clear();
    }
    persistPairName = name; // persisted; the FFI sanitizes its own copy (ASCII graphic + space, 16).
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_set_pair_target (hyphaHandle, name.toRawUTF8());
}

bool KirinHyphaProcessorBase::setPairCandidate (const juce::String& instanceId,
                                                const juce::String& name)
{
    bool selected = false;
    {
        const juce::ScopedLock sl (handleLock);
        if (hyphaHandle == nullptr)
            return false;
        selected = kirin_hypha_select_pair_candidate (hyphaHandle, instanceId.toRawUTF8());
    }
    if (selected)
    {
        persistPairName = name;
        persistPairProjectHash.clear();
        persistPairInstanceId.clear();
        juce::String projectHash, selectedInstanceId;
        if (pairedPreLocator (projectHash, selectedInstanceId))
        {
            persistPairProjectHash = projectHash;
            persistPairInstanceId = selectedInstanceId;
        }
    }
    return selected;
}

int KirinHyphaProcessorBase::pairStatus() const
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr ? (int) kirin_hypha_pair_status (hyphaHandle) : 0;
}

juce::String KirinHyphaProcessorBase::pairedPreInstanceId() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return {};
    char out[64] = { 0 };
    return kirin_hypha_get_paired_pre_instance_id (hyphaHandle, out, sizeof (out))
             ? juce::String::fromUTF8 (out)
             : juce::String();
}

bool KirinHyphaProcessorBase::pairedPreLocator (juce::String& projectHash,
                                                juce::String& instanceId) const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    char projectOut[64] = { 0 };
    char instanceOut[64] = { 0 };
    if (! kirin_hypha_get_paired_pre_locator (
            hyphaHandle, projectOut, sizeof (projectOut), instanceOut, sizeof (instanceOut)))
        return false;
    projectHash = juce::String::fromUTF8 (projectOut);
    instanceId = juce::String::fromUTF8 (instanceOut);
    return true;
}

bool KirinHyphaProcessorBase::keepPair()
{
    refreshLicenseForUserAction();
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_keep (hyphaHandle);
}

int KirinHyphaProcessorBase::keepPhase() const
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr ? (int) kirin_hypha_keep_phase (hyphaHandle)
                                  : (int) KIRIN_KEEP_PHASE_IDLE;
}

bool KirinHyphaProcessorBase::recordExclusionConflict() const
{
    // B-118 (②): advisory only. keepPair()/keepAll() の reserve→count>MAX が正本。
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_record_exclusion_conflict (hyphaHandle);
}

juce::String KirinHyphaProcessorBase::recordErrorMessage() const
{
    // B-118 (③): io_thread 連続失敗の固定文言（G-115-29）。None は空 String（R-26 沈黙）。
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return {};
    char buf[128] = { 0 };
    if (kirin_hypha_record_error_message (hyphaHandle, buf, sizeof (buf)))
        return juce::String::fromUTF8 (buf);
    return {};
}

juce::String KirinHyphaProcessorBase::drainKeepActionNotice()
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return {};
    char buf[128] = { 0 };
    if (kirin_hypha_drain_keep_action_notice (hyphaHandle, buf, sizeof (buf)))
        return juce::String::fromUTF8 (buf);
    return {};
}

juce::String KirinHyphaProcessorBase::pathAnomalyMessage() const
{
    // B-128 (G-115-373 / D3): 当該 instance（hyphaHandle の instance_id）の anomaly を per-instance で
    // drain（materialize event は自 instance のみ / wall event は global）。文言あり=Some / 無し=空。
    const juce::ScopedLock sl (handleLock);
    char buf[256] = { 0 };
    if (kirin_hypha_drain_path_event (hyphaHandle, buf, sizeof (buf)))
        return juce::String::fromUTF8 (buf);
    return {};
}

bool KirinHyphaProcessorBase::licenseIsOs() const
{
    return cachedLicenseCode.load (std::memory_order_acquire) == 0;
}

void KirinHyphaProcessorBase::refreshLicenseForUserAction()
{
    const int observed = (int) kirin_hypha_load_license();
    cachedLicenseCode.store (observed, std::memory_order_release);
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_set_license (hyphaHandle, (uint8_t) observed);
}

void KirinHyphaProcessorBase::stopPair()
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_stop (hyphaHandle);
}

// --- B-073: POST Δ readout ---------------------------------------------------------------

bool KirinHyphaProcessorBase::pollDelta (KirinDelta& out) const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_poll_delta (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::setSpectrumVisible (bool visible)
{
    if (role != Role::Post)
        return false;
    if (visible)
    {
        perceptualAnalysisRequested.store (false, std::memory_order_release);
        absoluteAnalysisRequested.store (false, std::memory_order_release);
        internalAttackRequested.store (false, std::memory_order_release);
    }
    spectrumVisibleRequested.store (visible, std::memory_order_release);
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || ! writesEnabled.load (std::memory_order_acquire))
        return false;
    if (visible && ! kirin_hypha_set_spectrum_channel_mode (
            hyphaHandle,
            preferredSpectrumChannelMode.load (std::memory_order_acquire)))
        return false;
    return kirin_hypha_set_spectrum_visible (hyphaHandle, visible);
}

bool KirinHyphaProcessorBase::setPerceptualVisible (bool visible)
{
    if (role != Role::Post)
        return false;
    if (visible)
    {
        perceptualAnalysisRequested.store (true, std::memory_order_release);
        absoluteAnalysisRequested.store (false, std::memory_order_release);
        internalAttackRequested.store (false, std::memory_order_release);
    }
    else
    {
        perceptualAnalysisRequested.store (false, std::memory_order_release);
    }
    spectrumVisibleRequested.store (visible, std::memory_order_release);
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || ! writesEnabled.load (std::memory_order_acquire))
        return false;
    if (visible && ! kirin_hypha_set_spectrum_channel_mode (
            hyphaHandle,
            preferredSpectrumChannelMode.load (std::memory_order_acquire)))
        return false;
    return kirin_hypha_set_perceptual_visible (hyphaHandle, visible);
}

bool KirinHyphaProcessorBase::setAbsoluteVisible (bool visible)
{
    if (role != Role::Post)
        return false;
    if (visible)
    {
        perceptualAnalysisRequested.store (false, std::memory_order_release);
        absoluteAnalysisRequested.store (true, std::memory_order_release);
        internalAttackRequested.store (false, std::memory_order_release);
    }
    else
    {
        absoluteAnalysisRequested.store (false, std::memory_order_release);
    }
    spectrumVisibleRequested.store (visible, std::memory_order_release);
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || ! writesEnabled.load (std::memory_order_acquire))
        return false;
    return kirin_hypha_set_absolute_visible (hyphaHandle, visible);
}

bool KirinHyphaProcessorBase::setSpectrumChannelMode (uint8_t channelMode)
{
    if (role != Role::Post || channelMode > KIRIN_SPECTRUM_CHANNEL_SIDE)
        return false;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr && writesEnabled.load (std::memory_order_acquire))
    {
        if (! kirin_hypha_set_spectrum_channel_mode (hyphaHandle, channelMode))
            return false;
    }
    else if (channelMode == KIRIN_SPECTRUM_CHANNEL_SIDE
             && getTotalNumInputChannels() != 2)
    {
        return false;
    }
    preferredSpectrumChannelMode.store (channelMode, std::memory_order_release);
    return true;
}

bool KirinHyphaProcessorBase::pollSpectrum (KirinSpectrumView& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_spectrum (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollSpectrumBatch (KirinSpectrumBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_spectrum_batch (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollPerceptual (KirinPerceptualView& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_perceptual (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollPerceptualBatch (KirinPerceptualBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_perceptual_batch (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollAbsoluteBatch (KirinAbsoluteBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_absolute_batch (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollAnalysisOwnerNames (juce::String& out) const
{
    if (role != Role::Post)
        return false;
    KirinAnalysisOwners owners {};
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || ! kirin_hypha_poll_analysis_owners (hyphaHandle, &owners))
        return false;
    if (owners.count != KIRIN_ANALYSIS_SLOT_COUNT)
    {
        out.clear();
        return true;
    }
    juce::StringArray names;
    for (size_t index = 0u; index < KIRIN_ANALYSIS_SLOT_COUNT; ++index)
    {
        const auto* utf8 = owners.names[index];
        if (utf8[0] == '\0')
        {
            out.clear();
            return true;
        }
        names.add (juce::String (juce::CharPointer_UTF8 (utf8)));
    }
    out = names.joinIntoString (", ");
    return true;
}

bool KirinHyphaProcessorBase::setInternalAttackEnabled (bool enabled)
{
    if (role != Role::Post)
        return false;
    if (enabled)
    {
        perceptualAnalysisRequested.store (false, std::memory_order_release);
        absoluteAnalysisRequested.store (false, std::memory_order_release);
    }
    spectrumVisibleRequested.store (enabled, std::memory_order_release);
    internalAttackRequested.store (enabled, std::memory_order_release);
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_set_internal_attack_enabled (hyphaHandle, enabled);
}

bool KirinHyphaProcessorBase::pollInternalAttackBatch (KirinAttackBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_internal_attack_batch (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollInternalAttackEvents (KirinAttackEventBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_internal_attack_events (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollInternalAttackWaveform (KirinAttackWaveformBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_internal_attack_waveform (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollInternalAttackDetails (KirinAttackDetailBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_internal_attack_details (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollInternalAttackPreWaveform (KirinAttackWaveformBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_internal_attack_pre_waveform (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollInternalAttackPreDetails (KirinAttackDetailBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_internal_attack_pre_details (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollInternalAttackPairEvents (KirinAttackPairEventBatch& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_poll_internal_attack_pair_events (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::internalAttackStats (KirinAttackStats& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr
        && kirin_hypha_internal_attack_stats (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::spectrumStats (KirinSpectrumStats& out) const
{
    if (role != Role::Post)
        return false;
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_spectrum_stats (hyphaHandle, &out);
}

// --- B-054: PRE live name + LED pollers ---------------------------------------------------

void KirinHyphaProcessorBase::setPreName (const juce::String& name)
{
    // PRE self name == identity.name: persist it (chunk round-trip) AND push it live to the
    // io_thread via the FFI (sanitized to ASCII graphic + space / 16 there). Mirrors how the
    // egui PRE writes its shared name Arc; persistName keeps DAW save/load consistent.
    persistName = name;
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    if (preDisplayController != nullptr)
        preDisplayController->setName (name);
#endif
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_set_pre_name (hyphaHandle, name.toRawUTF8());
}

bool KirinHyphaProcessorBase::measureAlive() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_measure_alive (hyphaHandle);
}

bool KirinHyphaProcessorBase::recordAcknowledged() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_record_acknowledged (hyphaHandle);
}

int KirinHyphaProcessorBase::signalStateLive() const
{
    // B-113: editor の表示分岐は Rust の signal_state を直読する。Measure Thread が heartbeat
    // 停止検出で Inactive へ上書きするため、processBlock 停止後に stale な Active を表示しない
    // （旧 B-073 lastSignalState キャッシュを置換）。
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return 0; // Inactive（安全側 = ---）
    return (int) kirin_hypha_get_signal_state (hyphaHandle);
}

bool KirinHyphaProcessorBase::heartbeatLive() const
{
    // B-115: heartbeat 鮮度（processBlock が呼ばれている事実 / Measure Thread が publish）。
    // editor は `playing かつ live` で POST pair 変更をロックする（playing 凍結値の false-release
    // 防止 / signal_state とは別軸＝無音再生中も state=Inactive のため state を live の代用にしない）。
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false; // 安全側 = live でない = ロックしない
    return kirin_hypha_heartbeat_live (hyphaHandle);
}

bool KirinHyphaProcessorBase::presetAvailable() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_preset_available (hyphaHandle);
}

bool KirinHyphaProcessorBase::addMark (const juce::String& tag)
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_add_mark (hyphaHandle, tag.toRawUTF8());
}

// --- B-102: POST broadcast + candidate enumeration -----------------------------------------

bool KirinHyphaProcessorBase::keepAll()
{
    refreshLicenseForUserAction();
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_keep_all (hyphaHandle);
}

void KirinHyphaProcessorBase::stopAll()
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_stop_all (hyphaHandle);
}

juce::Array<KirinHyphaProcessorBase::PreCandidate> KirinHyphaProcessorBase::enumeratePreCandidates() const
{
    juce::Array<PreCandidate> out;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return out;

    constexpr size_t kCap = 32; // generous; FFI truncates beyond this
    KirinPreCandidate buf[kCap];
    const size_t n = kirin_hypha_enumerate_pre_candidates (hyphaHandle, buf, kCap);
    for (size_t i = 0; i < n; ++i)
    {
        PreCandidate c;
        c.instanceId = juce::String::fromUTF8 (buf[i].instance_id);
        c.name       = juce::String::fromUTF8 (buf[i].name);
        c.hasName    = (buf[i].has_name != 0);
        out.add (c);
    }
    return out;
}

juce::Array<KirinHyphaProcessorBase::PostPairClaim> KirinHyphaProcessorBase::enumeratePostPairClaims() const
{
    juce::Array<PostPairClaim> out;
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return out;

    constexpr size_t kCap = 32; // same cap as PRE candidates; FFI truncates beyond this
    KirinPostPairClaim buf[kCap];
    const size_t n = kirin_hypha_enumerate_post_pair_claims (hyphaHandle, buf, kCap);
    for (size_t i = 0; i < n; ++i)
    {
        PostPairClaim c;
        c.instanceId      = juce::String::fromUTF8 (buf[i].instance_id);
        c.pairPreName     = juce::String::fromUTF8 (buf[i].pair_pre_name);
        c.hasPairPreName  = (buf[i].has_pair_pre_name != 0);
        c.pairedPreInstanceId = juce::String::fromUTF8 (buf[i].paired_pre_instance_id);
        c.hasPairedPreInstanceId = (buf[i].has_paired_pre_instance_id != 0);
        out.add (c);
    }
    return out;
}

int KirinHyphaProcessorBase::keepReadyCount() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return 0;
    return (int) kirin_hypha_count_keep_ready (hyphaHandle);
}

const juce::String KirinHyphaProcessorBase::getName() const
{
    return role == Role::Post ? "POST Kirin Hypha" : "PRE Kirin Hypha";
}

bool KirinHyphaProcessorBase::acceptsMidi() const        { return false; }
bool KirinHyphaProcessorBase::producesMidi() const       { return false; }
bool KirinHyphaProcessorBase::isMidiEffect() const       { return false; }
double KirinHyphaProcessorBase::getTailLengthSeconds() const { return 0.0; }

int KirinHyphaProcessorBase::getNumPrograms()            { return 1; }
int KirinHyphaProcessorBase::getCurrentProgram()         { return 0; }
void KirinHyphaProcessorBase::setCurrentProgram (int)    {}
const juce::String KirinHyphaProcessorBase::getProgramName (int) { return {}; }
void KirinHyphaProcessorBase::changeProgramName (int, const juce::String&) {}

void KirinHyphaProcessorBase::getStateInformation (juce::MemoryBlock& destData)
{
    // Persist both the human reconnect selector and the exact PRE instance. Hosts may restore PRE
    // and POST in either order; the exact ID is reconstructed as a Waiting fixed-path latch.
    juce::String livePairProjectHash, livePairInstanceId;
    if (pairedPreLocator (livePairProjectHash, livePairInstanceId))
    {
        persistPairInstanceId = livePairInstanceId;
        persistPairProjectHash = livePairProjectHash;
    }
    else if (writesEnabled.load (std::memory_order_acquire))
    {
        persistPairInstanceId.clear();
        persistPairProjectHash.clear();
    }
    juce::XmlElement xml ("KirinHyphaState");
    xml.setAttribute ("instance_id",      persistInstanceId);
    xml.setAttribute ("project_uuid",     persistProjectUuid);
    xml.setAttribute ("daw_session_uuid", persistDawSessionUuid);
    xml.setAttribute ("name",             persistName);
    xml.setAttribute ("pair_pre_name",    persistPairName);
    xml.setAttribute ("paired_pre_instance_id", persistPairInstanceId);
    xml.setAttribute ("paired_pre_project_hash", persistPairProjectHash);
    xml.setAttribute ("loudness_view",
                      persistShortTermLoudness.load (std::memory_order_acquire) ? "S" : "M");
    xml.setAttribute ("display_state_version", 2);
    xml.setAttribute ("observatory_domain", (int) observatoryDomainPreference());
    xml.setAttribute ("observatory_target", (int) observatoryTargetPreference());
    xml.setAttribute ("observatory_time_range", (int) observatoryTimeRangePreference());
    xml.setAttribute ("observatory_size", (int) spectrumSizePreference());
    copyXmlToBinary (xml, destData);
}

void KirinHyphaProcessorBase::setStateInformation (const void* data, int sizeInBytes)
{
    // B-069/B-072: restore the 4 identity keys + pair target into the persist members. May
    // run before or after prepareToPlay (JUCE does not guarantee ordering); the FFI receives
    // these at enable time (enableWritesNow), deferred to the message-thread Timer.
    stateInformationSeen.store (true, std::memory_order_release);

    juce::String restoredInstanceId, restoredProjectUuid, restoredDawSessionUuid;
    juce::String restoredName, restoredPairName, restoredPairInstanceId, restoredPairProjectHash;
    bool restoredShortTermLoudness = false;
    uint8_t restoredObservatoryDomain = 0;
    uint8_t restoredObservatoryTarget = 0;
    uint8_t restoredObservatoryTimeRange = 0;
    uint8_t restoredObservatorySize = 0;
    bool restored = false;

    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName ("KirinHyphaState"))
        {
            restoredInstanceId     = xml->getStringAttribute ("instance_id");
            restoredProjectUuid    = xml->getStringAttribute ("project_uuid");
            restoredDawSessionUuid = xml->getStringAttribute ("daw_session_uuid");
            restoredName           = xml->getStringAttribute ("name");
            restoredPairName       = xml->getStringAttribute ("pair_pre_name");
            restoredPairInstanceId = xml->getStringAttribute ("paired_pre_instance_id");
            restoredPairProjectHash = xml->getStringAttribute ("paired_pre_project_hash");
            restoredShortTermLoudness = xml->getStringAttribute ("loudness_view") == "S";
            if (xml->getIntAttribute ("display_state_version", 0) >= 2)
            {
                restoredObservatoryDomain = (uint8_t) juce::jlimit (
                    0, 3, xml->getIntAttribute ("observatory_domain", 0));
                restoredObservatoryTarget = (uint8_t) juce::jlimit (
                    0, 1, xml->getIntAttribute ("observatory_target", 0));
                restoredObservatoryTimeRange = (uint8_t) juce::jlimit (
                    0, 4, xml->getIntAttribute ("observatory_time_range", 0));
                restoredObservatorySize = (uint8_t) juce::jlimit (
                    0, 3, xml->getIntAttribute ("observatory_size", 0));
            }
            restored = true;
        }
    }

    // Existing Studio One projects contain the old nih-plug VST3 JSON state. The JUCE shell keeps
    // the same component CID and decodes that exact one-time legacy contract here, so switching the
    // shipped VST3 adapter does not fabricate new identities or lose the selected PRE name.
    if (! restored && data != nullptr && sizeInBytes > 0)
    {
        KirinLegacyNihState legacy {};
        if (kirin_hypha_decode_legacy_nih_state (
                static_cast<const uint8_t*> (data), (size_t) sizeInBytes, &legacy))
        {
            restoredInstanceId     = juce::String::fromUTF8 (legacy.instance_id);
            restoredProjectUuid    = juce::String::fromUTF8 (legacy.project_uuid);
            restoredDawSessionUuid = juce::String::fromUTF8 (legacy.daw_session_uuid);
            restoredName           = juce::String::fromUTF8 (legacy.name);
            restoredPairName       = juce::String::fromUTF8 (legacy.pair_pre_name);
            restored = true;
        }
    }

    if (restored)
    {
        // Additive display-only state. Old JUCE states, legacy nih-plug JSON, and invalid values
        // all resolve to the established Momentary default without touching identity/pair fields.
        persistShortTermLoudness.store (restoredShortTermLoudness, std::memory_order_release);
        preferredObservatoryDomain.store (restoredObservatoryDomain, std::memory_order_release);
        preferredObservatoryTarget.store (restoredObservatoryTarget, std::memory_order_release);
        preferredObservatoryTimeRange.store (restoredObservatoryTimeRange,
                                              std::memory_order_release);
        preferredSpectrumSize.store (restoredObservatorySize, std::memory_order_release);
        // Once writes are enabled, the io_thread has already snapshotted path identity. Only the
        // live-editable name/pair fields may be applied at that point; the exact-path writer stays
        // coherent with its established identity.
        if (writesEnabled.load (std::memory_order_acquire))
        {
            persistName = restoredName;
            persistPairName = restoredPairName;
            persistPairInstanceId = restoredPairInstanceId;
            persistPairProjectHash = restoredPairProjectHash;
            const juce::ScopedLock sl (handleLock);
            if (hyphaHandle != nullptr)
            {
                if (role == Role::Post)
                {
                    kirin_hypha_set_pair_target (hyphaHandle, persistPairName.toRawUTF8());
                    if (persistPairInstanceId.isNotEmpty() && persistPairProjectHash.isNotEmpty())
                    {
                        if (! kirin_hypha_restore_pair_candidate (
                                hyphaHandle,
                                persistPairProjectHash.toRawUTF8(),
                                persistPairInstanceId.toRawUTF8()))
                        {
                            persistPairInstanceId.clear();
                            persistPairProjectHash.clear();
                        }
                    }
                }
                else
                    kirin_hypha_set_pre_name (hyphaHandle, persistName.toRawUTF8());
            }
            return;
        }

        persistInstanceId = restoredInstanceId;
        persistProjectUuid = restoredProjectUuid;
        persistDawSessionUuid = restoredDawSessionUuid;
        persistName = restoredName;
        persistPairName = restoredPairName;
        persistPairInstanceId = restoredPairInstanceId;
        persistPairProjectHash = restoredPairProjectHash;
    }

    if (! writesEnabled.load (std::memory_order_acquire))
    {
        enableDelayTicks.store (0, std::memory_order_release);
        enablePending.store (true, std::memory_order_release);
    }
}

void KirinHyphaProcessorBase::timerCallback()
{
    // B-126 + Logic stopped-state fix: non-RT enable poll on the message thread.
    if (! writesEnabled.load (std::memory_order_acquire)
        && enablePending.load (std::memory_order_acquire))
    {
        const int ticks = enableDelayTicks.load (std::memory_order_acquire);
        if (ticks > 0)
            enableDelayTicks.store (ticks - 1, std::memory_order_release);
        else
            enableWritesNow();
    }
    if (writesEnabled.load (std::memory_order_acquire))
        stopTimer();
}

void KirinHyphaProcessorBase::enableWritesNow()
{
    // B-070 + Logic stopped-state fix: message-thread one-shot. Applies the FFI contract order create -> set_license
    // (done in prepareToPlay) -> set_identity -> enable_*_writes. processBlock still takes the
    // fast path; when the host is stopped, prepareToPlay enables after a short state-restore grace
    // period so PRE/POST discovery does not depend on audible playback.
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr || writesEnabled.load (std::memory_order_acquire))
        return;

    // set_identity BEFORE enable: empty keys -> the FFI generates fresh UUIDs and writes
    // them back; restored keys -> reused. A set_identity after enable would not propagate
    // (the io_thread snapshots identity at enable), hence this single ordered point.
    kirin_hypha_set_identity (hyphaHandle,
                              persistInstanceId.toRawUTF8(),
                              persistProjectUuid.toRawUTF8(),
                              persistDawSessionUuid.toRawUTF8(),
                              persistName.toRawUTF8());

    // Role selects the plugin_data role: PRE (Watch pre.json + Record) vs POST (post.json
    // Δ via select_target_pre). enable_pre_writes / enable_post_writes are mutually
    // exclusive and idempotent in the FFI.
    if (role == Role::Post)
    {
        kirin_hypha_enable_post_writes (hyphaHandle);
        // B-072: apply the restored/current pair target after enable (contract order).
        kirin_hypha_set_pair_target (hyphaHandle, persistPairName.toRawUTF8());
        if (persistPairInstanceId.isNotEmpty() && persistPairProjectHash.isNotEmpty())
        {
            if (! kirin_hypha_restore_pair_candidate (
                    hyphaHandle,
                    persistPairProjectHash.toRawUTF8(),
                    persistPairInstanceId.toRawUTF8()))
            {
                persistPairInstanceId.clear();
                persistPairProjectHash.clear();
            }
        }
        if (internalAttackRequested.load (std::memory_order_acquire))
        {
            kirin_hypha_set_internal_attack_enabled (hyphaHandle, true);
        }
        else if (spectrumVisibleRequested.load (std::memory_order_acquire))
        {
            kirin_hypha_set_spectrum_channel_mode (
                hyphaHandle,
                preferredSpectrumChannelMode.load (std::memory_order_acquire));
            if (absoluteAnalysisRequested.load (std::memory_order_acquire))
                kirin_hypha_set_absolute_visible (hyphaHandle, true);
            else if (perceptualAnalysisRequested.load (std::memory_order_acquire))
                kirin_hypha_set_perceptual_visible (hyphaHandle, true);
            else
                kirin_hypha_set_spectrum_visible (hyphaHandle, true);
        }
    }
    else
    {
        kirin_hypha_enable_pre_writes (hyphaHandle);
    }

    // Read back the final (restored or freshly generated) identity so getStateInformation
    // persists it across DAW save/load.
    KirinIdentity id;
    kirin_hypha_get_identity (hyphaHandle, &id);
    persistInstanceId     = juce::String::fromUTF8 (id.instance_id);
    persistProjectUuid    = juce::String::fromUTF8 (id.project_uuid);
    persistDawSessionUuid = juce::String::fromUTF8 (id.daw_session_uuid);
    persistName           = juce::String::fromUTF8 (id.name);

#if KIRIN_HYPHA_GUIDE_TRANSPORT
    if (preDisplayController == nullptr)
        preDisplayController = std::make_unique<hypha::pre_display::Controller> (preDisplayClock);
    hypha::pre_display::RuntimeIdentity displayIdentity;
    displayIdentity.role = role == Role::Post ? hypha::pre_display::GuideTargetRole::post
                                              : hypha::pre_display::GuideTargetRole::pre;
    displayIdentity.instanceId = persistInstanceId;
    displayIdentity.projectUuid = persistProjectUuid;
    displayIdentity.dawSessionUuid = persistDawSessionUuid;
    displayIdentity.name = persistName;
    displayIdentity.pluginVersion = JucePlugin_VersionString;
    displayIdentity.pluginFormat = wrapperType == juce::AudioProcessor::wrapperType_AudioUnit ? "AU" : "VST3";
       #if JUCE_WINDOWS
    displayIdentity.platform = "windows";
       #else
    displayIdentity.platform = "macos";
       #endif
       #if JUCE_ARM
    displayIdentity.architecture = "arm64";
       #else
    displayIdentity.architecture = "x86_64";
       #endif
    preDisplayController->configureAndStart (std::move (displayIdentity));
#endif

    writesEnabled.store (true, std::memory_order_release);
}
