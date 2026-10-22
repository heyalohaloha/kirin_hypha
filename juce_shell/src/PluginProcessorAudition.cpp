#include "PluginProcessor.h"
#include "chain_trace/ChainTraceScope.h"

// Explicit comparison paths stay after the canonical input measurement transaction.
void KirinHyphaProcessorBase::processComparisonPaths (
    juce::AudioBuffer<float>& buffer, const hypha::HostProcessClock& clock,
    bool timelineActive, bool bypassed, bool nonRealtimeMode)
{
    hypha::chain_trace::Scope trace (liveCompare, buffer.getArrayOfReadPointers(),
        buffer.getNumChannels(), buffer.getNumSamples(), preparedFormat.sampleRate,
        role == Role::Post, static_cast<int> (wrapperType), clock, bypassed, nonRealtimeMode, isUsingDoublePrecision() ? 64 : 32);
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
                                   captureClock, static_cast<std::uint32_t> (preparedFormat.sampleRate));

    // The admitted session owns exact, immutable PCM and its epochs. Runtime clock/PDC
    // continuity remains required; a trial never creates another Analysis slot.
    if (localBlindProductSupported()
        && role == Role::Post && localBlindProductSession.hasPublishedRealtime())
    {
        hypha::local_blind::TrialBlock block;
        block.sampleRate = static_cast<std::uint32_t> (preparedFormat.sampleRate);
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
        block.exactLoopRangeValid = clock.loop.active;
        if (localBlindProductSession.render (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                             buffer.getNumSamples(), block))
        {
            processLiveCompare (buffer, clock, bypassed, nonRealtimeMode, true);
            return;
        }
    }
#if ! KIRIN_HYPHA_PRE_DISPLAY
    // 仕様 A：オフライン書き出しを見たら、メッセージスレッドが停止前の選択の保留を消す（ここでは知らせるだけ）。
    if (role == Role::Post && referenceAuditionController != nullptr && nonRealtimeMode)
        referenceAuditionController->noteOfflineRender();
    if (role == Role::Post && referenceAuditionController != nullptr)
        referenceAuditionController->observeTransport (clock.positionSamples, clock.hasPosition, clock.playing);
    if (role == Role::Post && referenceAuditionController != nullptr)
        referenceAuditionController->observeAInput (
            buffer, clock.positionSamples, clock.hasPosition, clock.playing,
            ! bypassed && ! nonRealtimeMode && licenseIsOs(), clock.clockSource, !bypassed && !nonRealtimeMode,
            {clock.inputPresentationSamples,clock.outputPresentationSamples,clock.presentationSource,clock.inputPresentationValid,clock.outputPresentationValid});
    // Explicit B is an output-only audition copy. A has already been measured. Offline
    // render, bypass, missing project time, cache miss, and every consumer failure keep A intact.
    bool referenceRendered = false;
    if (role == Role::Post && referenceAuditionController != nullptr)
        referenceRendered = referenceAuditionController->renderSelectedB (
            buffer, clock.positionSamples, clock.hasPosition,
            ! bypassed && ! nonRealtimeMode && licenseIsOs(),
            ! bypassed && ! nonRealtimeMode);
    // Live PRE/POST compare comes last: A is already observed and another audition keeps POST.
    processLiveCompare (buffer, clock, bypassed, nonRealtimeMode, referenceRendered);
#else
    // The PRE product feeds the live compare ring only while a POST session demands it.
    processLiveCompare (buffer, clock, bypassed, nonRealtimeMode, false);
#endif
}

// What the last block actually played, for LISTEN's notice. A plain atomic read; once the host stops
// calling the audio callback, nothing plays and no role is named.
int KirinHyphaProcessorBase::referenceOutputSlot() const noexcept
{
#if ! KIRIN_HYPHA_PRE_DISPLAY
    return referenceAuditionController != nullptr && heartbeatLive() ? referenceAuditionController->outputSlot() : 0;
#else
    return 0;
#endif
}
