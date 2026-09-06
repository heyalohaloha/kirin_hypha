#include "PluginProcessor.h"

// RT output selection stays after the canonical input measurement transaction.
void KirinHyphaProcessorBase::renderComparisonOutputs (
    juce::AudioBuffer<float>& buffer, int64_t positionSamples, bool hasPosition,
    bool playing, bool bypassed, bool nonRealtimeMode)
{
    // Default closed: no production admission owner publishes PCM/epochs yet. No new button,
    // fake PDC, local-PID scope assumption, or third Analysis slot is enabled by this hook.
    if (role == Role::Post && localBlindOutput.hasPublishedRealtime())
    {
        hypha::local_blind::TrialBlock block;
        block.epochs = localBlindEpochs.read();
        block.sampleRate = static_cast<std::uint32_t> (preparedSampleRate);
        block.position = positionSamples;
        block.positionValid = hasPosition;
        block.playing = playing;
        block.realtime = ! nonRealtimeMode;
        block.bypassed = bypassed;
        // JUCE PPQ loop points do not prove native sample-exact loop boundaries.
        if (localBlindOutput.render (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                     buffer.getNumSamples(), block)) return;
    }
#if KIRIN_HYPHA_GUIDE_TRANSPORT && ! KIRIN_HYPHA_PRE_DISPLAY
    if (role == Role::Post && referenceAuditionController != nullptr)
        referenceAuditionController->observeAInput (
            buffer, positionSamples, hasPosition, playing,
            ! bypassed && ! nonRealtimeMode && licenseIsOs());
    // Explicit B is an output-only audition copy. A has already been measured. Offline
    // render, bypass, missing project time, cache miss, and every consumer failure keep A intact.
    if (role == Role::Post && referenceAuditionController != nullptr)
        referenceAuditionController->renderSelectedB (
            buffer, positionSamples, hasPosition, ! bypassed && ! nonRealtimeMode && licenseIsOs());
#else
    juce::ignoreUnused (buffer, positionSamples, hasPosition, playing, bypassed, nonRealtimeMode);
#endif
}
