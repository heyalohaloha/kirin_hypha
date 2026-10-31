#include "PluginProcessor.h"

// Common AU/VST3/AAX input transaction extracted before adding deferred AAX analysis.
bool KirinHyphaProcessorBase::feedMeasurementInput (const juce::AudioBuffer<float>& buffer,
    const hypha::HostProcessClock& clock, int windowStartFrame, int windowEndFrame,
    int64_t windowPositionSamples, uint64_t windowNumFrames, bool forceTakeStartEpoch)
{
    const int numCh = buffer.getNumChannels();
    const size_t needed = (size_t) windowNumFrames * (size_t) numCh;
    if (numCh > 0 && needed <= scratchCapacitySamples)
    {
        // Read original A into the prepare-time scratch. No output write or RT allocation.
        size_t idx = 0;
        for (int f = windowStartFrame; f < windowEndFrame; ++f)
            for (int ch = 0; ch < numCh; ++ch)
                interleaveScratch[idx++] = buffer.getReadPointer (ch)[f];
        // The needle's own copy of the VU integration (atomics only; idle unless a VU is on screen).
        // PRE has no chain timing, so its own callback clock finds a gap between callbacks.
        vuLiveTap.push (interleaveScratch.data(), (int) windowNumFrames, numCh, clock.playing, clock.hasPosition,
                        windowPositionSamples, clock.callbackNanos != 0 ? clock.callbackNanos
                                                                        : hypha::live_compare::callbackWallNanos());
        const auto auxiliary = hypha::offsetAuxiliaryClock (clock.auxiliary, (std::uint64_t) windowStartFrame);
        // A Keep's later pass is a LOOP wrap only when the host's own loop range names the return.
        kirin_hypha_note_capture_loop_wrap (hyphaHandle, liveCompare.keepLoopWrap.observe (clock.loop, clock.playing,
            clock.hasPosition, clock.positionSamples, buffer.getNumSamples(), preparedFormat.sampleRate,
            clock.outputPresentationValid ? (std::int64_t) clock.outputPresentationSamples : 0));
        kirin_hypha_note_capture_window (hyphaHandle, clock.hasPosition, windowPositionSamples,
            windowNumFrames, clock.clockSource, clock.presentationSource,
            clock.inputPresentationValid, clock.inputPresentationSamples,
            clock.outputPresentationValid, clock.outputPresentationSamples,
            (std::uint8_t) auxiliary.source, auxiliary.valid, auxiliary.samples, forceTakeStartEpoch);
        return kirin_hypha_push_samples (hyphaHandle, interleaveScratch.data(), (size_t) windowNumFrames, (uint32_t) numCh);
    }
    // Oversize blocks keep passing through; count the loss without growing the scratch.
    if (numCh > 0 && needed > scratchCapacitySamples)
        kirin_hypha_note_oversized_drop (hyphaHandle, (uint64_t) needed);
    feedNoMeasurement ((uint32_t) numCh);
    return false;
}

// A callback that feeds nothing: the heartbeat-only keepalive, and the needle's window ends here
// (a stop, bypass or hold must not be bridged by a later block at the same position).
void KirinHyphaProcessorBase::feedNoMeasurement (uint32_t channels) noexcept
{
    vuLiveTap.interrupt();
    kirin_hypha_push_samples (hyphaHandle, nullptr, 0, channels);
}

void KirinHyphaProcessorBase::pushPairedAnalysis (uint64_t capturedFrames, uint32_t channels,
                                             int windowStartFrame) noexcept
{
    if (wrapperType != juce::AudioProcessor::wrapperType_AudioUnit
        && wrapperType != juce::AudioProcessor::wrapperType_AAX
        && ! (wrapperType == juce::AudioProcessor::wrapperType_VST3
            && hypha::live_compare::certifiedVst3Analysis (
                static_cast<hypha::live_compare::ClockAuthority> (liveCompare.clockAuthority)))) return;
    auto local = liveCompare.analysisClock.local;
    auto paired = liveCompare.analysisClock.paired;
    if (windowStartFrame < 0
        || ! hypha::live_compare::checkedClockAdd (local.start, windowStartFrame, local.start)) local.valid = false;
    if (windowStartFrame < 0
        || ! hypha::live_compare::checkedClockAdd (paired.start, windowStartFrame, paired.start)) paired.valid = false;
    // The output may now contain an explicit audition. The scratch still holds THIS callback's
    // original A. The FFI consumes its staged cardinality once; neither Watch nor Record advances.
    kirin_hypha_push_paired_analysis (hyphaHandle, capturedFrames != 0 ? interleaveScratch.data() : nullptr,
        (size_t) capturedFrames, channels, local.valid, local.start, local.revision,
        paired.valid, paired.start, paired.revision);
}
