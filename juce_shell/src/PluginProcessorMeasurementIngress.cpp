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
        const auto auxiliary = hypha::offsetAuxiliaryClock (clock.auxiliary, (std::uint64_t) windowStartFrame);
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
    kirin_hypha_push_samples (hyphaHandle, nullptr, 0, (uint32_t) numCh);
    return false;
}

void KirinHyphaProcessorBase::pushAaxAnalysis (uint64_t capturedFrames, uint32_t channels,
                                             int windowStartFrame) noexcept
{
    if (wrapperType != juce::AudioProcessor::wrapperType_AAX) return;
    auto local = liveCompare.analysisClock.local;
    auto paired = liveCompare.analysisClock.paired;
    if (windowStartFrame < 0
        || ! hypha::live_compare::checkedClockAdd (local.start, windowStartFrame, local.start)) local.valid = false;
    if (windowStartFrame < 0
        || ! hypha::live_compare::checkedClockAdd (paired.start, windowStartFrame, paired.start)) paired.valid = false;
    // The output may now contain an explicit audition. The scratch still holds THIS callback's
    // original A. The FFI consumes its staged cardinality once; neither Watch nor Record advances.
    kirin_hypha_push_aax_analysis (hyphaHandle, capturedFrames != 0 ? interleaveScratch.data() : nullptr,
        (size_t) capturedFrames, channels, local.valid, local.start, local.revision,
        paired.valid, paired.start, paired.revision);
}
