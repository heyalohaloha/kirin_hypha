#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath> // B-107: std::abs(float) for the silence peak threshold

namespace
{
    // Logic stopped-state fix: expose Inactive PRE/POST presence without waiting for the first audio callback.
    // The 50 ms Timer grants a bounded state-restore window before enabling from prepareToPlay.
    constexpr int kPrepareEnableDelayTicks = 10;

    // B-125 (b): prealloc-max headroom (frames). The interleave scratch is sized in
    // prepareToPlay to max(maximumExpectedSamplesPerBlock, this) frames so that realistic
    // variable / offline-render blocks larger than the realtime-declared block are still
    // measured without a (non-RT-safe) reallocation on the audio thread. Hosts can deliver
    // offline / freeze / bounce blocks well above the realtime maximum; 65536 frames
    // (~1.36 s @ 48 kHz) is a conservative ceiling. The one-time, non-RT prepareToPlay
    // allocation is bounded at 65536 * numCh * sizeof(float) (≈512 KB stereo). Pathological
    // blocks beyond this ceiling are not reallocated; their frames are counted as oversized
    // drops (B-125 (c) / kirin_hypha_note_oversized_drop) while audio keeps passing through.
    constexpr int kOversizeHeadroomFrames = 65536;

    // C ABI signal-state codes: 0 = Inactive, 1 = Active, 2 = Bypassed.
    uint8_t resolveSignalStateCode (bool bypassed, bool playing, bool silent, bool recording)
    {
        if (bypassed)
            return 2;
        if (recording || (playing && ! silent))
            return 1;
        return 0;
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

    // B-126: start the non-RT enable poll. The constructor runs on the message thread (plugin
    // instantiation), so starting the Timer here is safe. It ticks at a low rate and no-ops once
    // writesEnabled is true (re-prepare resets the flags and it re-enables). This replaces the
    // audio-thread triggerAsyncUpdate (which JUCE warns may block — juce_AsyncUpdater.h). 50ms keeps
    // enable latency ~one buffer after the first processBlock while costing one atomic load per tick.
    startTimer (50);
}

KirinHyphaProcessorBase::~KirinHyphaProcessorBase()
{
    stopTimer(); // B-126: stop the non-RT enable poll before teardown (was cancelPendingUpdate / B-070).
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
    {
        kirin_hypha_destroy (hyphaHandle);
        hyphaHandle = nullptr;
    }
}

void KirinHyphaProcessorBase::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = getTotalNumInputChannels();

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
    // Re-prepare is allowed: drop any prior handle before creating a fresh one.
    if (hyphaHandle != nullptr)
    {
        kirin_hypha_destroy (hyphaHandle);
        hyphaHandle = nullptr;
    }

    // num_channels: pass the actual negotiated input channel count. Mono must remain 1ch
    // all the way into the meter; duplicating to stereo would bias loudness by +3.01 dB.
    hyphaHandle = kirin_hypha_create ((uint32_t) sampleRate, (uint32_t) numCh);

    // B-070: a fresh handle needs license applied and (re-)enabling. License is read once
    // from identity.json (single source) and cached for the editor's Os-gate (B-072).
    // set_identity + enable_*_writes are deferred to the message-thread Timer
    // (enableWritesNow) so any setStateInformation restore is applied before enable.
    if (hyphaHandle != nullptr)
    {
        const uint8_t lic = kirin_hypha_load_license();
        cachedLicenseCode.store ((int) lic, std::memory_order_release);
        kirin_hypha_set_license (hyphaHandle, lic);
        writesEnabled.store (false, std::memory_order_release);
        // Logic stopped-state fix: re-prepare needs a fresh enable, but Logic may not call processBlock until
        // playback. Start a message-thread fallback so Inactive presence/candidates are published
        // even while stopped. If setStateInformation already arrived for this instance, skip the
        // grace delay; otherwise keep the window so project recall can restore identity before the
        // io_thread snapshots it.
        const int restoreDelay = stateInformationSeen.load (std::memory_order_acquire) ? 0 : kPrepareEnableDelayTicks;
        enableDelayTicks.store (restoreDelay, std::memory_order_release);
        enablePending.store (true, std::memory_order_release);
    }
    // A null handle (create failure) is tolerated; processBlock / pollMeasureResult guard on it.
}

void KirinHyphaProcessorBase::releaseResources()
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
    {
        kirin_hypha_destroy (hyphaHandle);
        hyphaHandle = nullptr;
    }
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
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            playing = pos->getIsPlaying();
    lastPlaying.store (playing, std::memory_order_release); // B-054: POST pair lock reads this

    const bool silent = bufferIsSilent (buffer);
    const bool recording = kirin_hypha_is_recording (hyphaHandle);

    // C ABI signal-state codes: 0 = Inactive, 1 = Active, 2 = Bypassed.
    const uint8_t stateCode = resolveSignalStateCode (bypassed, playing, silent, recording);
    kirin_hypha_set_signal_state (hyphaHandle, stateCode);
    // B-113: 旧 lastSignalState キャッシュは廃止。editor は signalStateLive()（FFI 直読 / heartbeat-aware）で表示分岐する。

    // --- Feed the engine ---------------------------------------------------------
    // Parity with nih-plug: ring push only when Active; heartbeat advances every block.
    // push_samples advances heartbeat internally, so a 0-frame call is a heartbeat-only
    // keepalive (no ring push) for the Inactive / Bypassed case.
    if (stateCode == 1)
    {
        const size_t needed = (size_t) numFrames * (size_t) numCh;
        if (numCh > 0 && needed <= scratchCapacitySamples)
        {
            // Interleave [c0f0, c1f0, c0f1, c1f1, ...] — same order as nih-plug iter_samples.
            // getReadPointer only: the input buffer is never modified (R-12).
            size_t idx = 0;
            for (int f = 0; f < numFrames; ++f)
                for (int ch = 0; ch < numCh; ++ch)
                    interleaveScratch[idx++] = buffer.getReadPointer (ch)[f];

            kirin_hypha_push_samples (hyphaHandle, interleaveScratch.data(),
                                      (size_t) numFrames, (uint32_t) numCh);
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
    persistPairName = name; // persisted; the FFI sanitizes its own copy (ASCII graphic + space, 16).
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle != nullptr)
        kirin_hypha_set_pair_target (hyphaHandle, name.toRawUTF8());
}

bool KirinHyphaProcessorBase::keepPair()
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_keep (hyphaHandle);
}

bool KirinHyphaProcessorBase::recordExclusionConflict() const
{
    // B-118 (②): engine keep は 12-limit 非強制（egui UI 専管）。keep 前の pre-check 用 read-only。
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
    // B-118 (①): identity.json の license（0=Os / 1=Sense / 2=Unknown）。handle 不要。
    return kirin_hypha_load_license() == 0;
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

// --- B-054: PRE live name + LED pollers ---------------------------------------------------

void KirinHyphaProcessorBase::setPreName (const juce::String& name)
{
    // PRE self name == identity.name: persist it (chunk round-trip) AND push it live to the
    // io_thread via the FFI (sanitized to ASCII graphic + space / 16 there). Mirrors how the
    // egui PRE writes its shared name Arc; persistName keeps DAW save/load consistent.
    persistName = name;
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

bool KirinHyphaProcessorBase::addAnnotation (const juce::String& memo)
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_add_annotation (hyphaHandle, memo.toRawUTF8());
}

// --- B-102: POST broadcast + candidate enumeration -----------------------------------------

bool KirinHyphaProcessorBase::keepAll()
{
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

int KirinHyphaProcessorBase::keepReadyCount() const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return 0;
    return (int) kirin_hypha_count_keep_ready (hyphaHandle);
}

const juce::String KirinHyphaProcessorBase::getName() const
{
    return role == Role::Post ? "Kirin Hypha POST" : "Kirin Hypha PRE";
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
    // B-069/B-072: serialize the 4 identity keys + POST pair target as a JUCE-native XML
    // chunk. The persist members are kept in sync with the FFI at enable time.
    juce::XmlElement xml ("KirinHyphaState");
    xml.setAttribute ("instance_id",      persistInstanceId);
    xml.setAttribute ("project_uuid",     persistProjectUuid);
    xml.setAttribute ("daw_session_uuid", persistDawSessionUuid);
    xml.setAttribute ("name",             persistName);
    xml.setAttribute ("pair_pre_name",    persistPairName);
    copyXmlToBinary (xml, destData);
}

void KirinHyphaProcessorBase::setStateInformation (const void* data, int sizeInBytes)
{
    // B-069/B-072: restore the 4 identity keys + pair target into the persist members. May
    // run before or after prepareToPlay (JUCE does not guarantee ordering); the FFI receives
    // these at enable time (enableWritesNow), deferred to the message-thread Timer.
    stateInformationSeen.store (true, std::memory_order_release);

    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName ("KirinHyphaState"))
        {
            const auto restoredInstanceId     = xml->getStringAttribute ("instance_id");
            const auto restoredProjectUuid    = xml->getStringAttribute ("project_uuid");
            const auto restoredDawSessionUuid = xml->getStringAttribute ("daw_session_uuid");
            const auto restoredName           = xml->getStringAttribute ("name");
            const auto restoredPairName       = xml->getStringAttribute ("pair_pre_name");

            // Once writes are enabled, the io_thread has already snapshotted the path identity
            // (instance/project/session). Replacing those persisted keys now would split the DAW
            // chunk from the active writer. Live-editable fields are safe to restore and push
            // through their dedicated FFI setters.
            if (writesEnabled.load (std::memory_order_acquire))
            {
                persistName     = restoredName;
                persistPairName = restoredPairName;

                const juce::ScopedLock sl (handleLock);
                if (hyphaHandle != nullptr)
                {
                    if (role == Role::Post)
                        kirin_hypha_set_pair_target (hyphaHandle, persistPairName.toRawUTF8());
                    else
                        kirin_hypha_set_pre_name (hyphaHandle, persistName.toRawUTF8());
                }
                return;
            }

            persistInstanceId     = restoredInstanceId;
            persistProjectUuid    = restoredProjectUuid;
            persistDawSessionUuid = restoredDawSessionUuid;
            persistName           = restoredName;
            persistPairName       = restoredPairName;
        }
    }

    if (! writesEnabled.load (std::memory_order_acquire))
    {
        enableDelayTicks.store (0, std::memory_order_release);
        enablePending.store (true, std::memory_order_release);
    }
}

void KirinHyphaProcessorBase::timerCallback()
{
    // B-126 + Logic stopped-state fix: non-RT enable poll on the message thread. No-op once enabled (cheap atomic
    // load). Normally processBlock clears the prepare fallback delay and enables promptly; if the
    // host does not call processBlock while stopped, the prepare fallback publishes Inactive
    // presence after a short state-restore grace period.
    if (writesEnabled.load (std::memory_order_acquire))
        return;
    if (! enablePending.load (std::memory_order_acquire))
        return;
    const int ticks = enableDelayTicks.load (std::memory_order_acquire);
    if (ticks > 0)
    {
        enableDelayTicks.store (ticks - 1, std::memory_order_release);
        return;
    }
    enableWritesNow();
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

    writesEnabled.store (true, std::memory_order_release);
}
