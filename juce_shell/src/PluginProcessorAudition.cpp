#include "PluginProcessor.h"

// Explicit comparison paths stay after the canonical input measurement transaction.
void KirinHyphaProcessorBase::processComparisonPaths (
    juce::AudioBuffer<float>& buffer, const hypha::HostProcessClock& clock,
    bool timelineActive, bool bypassed, bool nonRealtimeMode)
{
    // A dormant lane is one atomic null-pointer check. Once a non-RT owner arms it, the exact
    // role-local A input is copied before any audition path can replace the output buffer.
    const hypha::local_blind::CaptureClockObservation captureClock {
        clock.positionSamples, buffer.getNumSamples(), clock.clockSource,
        clock.presentationSource, clock.inputPresentationSamples,
        clock.outputPresentationSamples, clock.hasPosition, timelineActive, bypassed,
        ! nonRealtimeMode, clock.inputPresentationValid, clock.outputPresentationValid
    };
   #if JUCE_DEBUG
    constexpr bool diagnosticCaptureEnabled = true;
   #else
    const bool diagnosticCaptureEnabled = localBlindProductSupported();
   #endif
    if (diagnosticCaptureEnabled)
        localBlindCapture.process (buffer.getArrayOfReadPointers(), getTotalNumInputChannels(),
                                   captureClock, static_cast<std::uint32_t> (preparedSampleRate));

    // The admitted session owns exact, immutable PCM and its epochs. Wrapper-specific host
    // proof remains a separate prerequisite; a trial never creates another Analysis slot.
    if (localBlindProductSupported()
        && role == Role::Post && localBlindProductSession.hasPublishedRealtime())
    {
        hypha::local_blind::TrialBlock block;
        block.sampleRate = static_cast<std::uint32_t> (preparedSampleRate);
        block.position = clock.positionSamples;
        block.positionValid = clock.hasPosition;
        block.playing = clock.playing;
        block.realtime = ! nonRealtimeMode;
        block.bypassed = bypassed;
        block.clock = { clock.clockSource, clock.presentationSource,
                        clock.inputPresentationSamples, clock.outputPresentationSamples,
                        clock.inputPresentationValid, clock.outputPresentationValid };
        // The host looping boolean authorizes only the exact native end->start wrap that the
        // renderer itself observes. PPQ loop points are never converted into sample boundaries.
        block.exactLoopRangeValid = clock.looping;
        if (localBlindProductSession.render (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                             buffer.getNumSamples(), block)) return;
    }
#if KIRIN_HYPHA_GUIDE_TRANSPORT && ! KIRIN_HYPHA_PRE_DISPLAY
    if (role == Role::Post && referenceAuditionController != nullptr)
        referenceAuditionController->observeAInput (
            buffer, clock.positionSamples, clock.hasPosition, clock.playing,
            ! bypassed && ! nonRealtimeMode && licenseIsOs());
    // Explicit B is an output-only audition copy. A has already been measured. Offline
    // render, bypass, missing project time, cache miss, and every consumer failure keep A intact.
    if (role == Role::Post && referenceAuditionController != nullptr)
        referenceAuditionController->renderSelectedB (
            buffer, clock.positionSamples, clock.hasPosition,
            ! bypassed && ! nonRealtimeMode && licenseIsOs(),
            ! bypassed && ! nonRealtimeMode);
#else
    juce::ignoreUnused (buffer, clock, bypassed, nonRealtimeMode);
#endif
}
