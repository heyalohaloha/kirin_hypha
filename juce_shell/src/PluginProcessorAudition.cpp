#include "PluginProcessor.h"

// RT output selection stays after the canonical input measurement transaction.
void KirinHyphaProcessorBase::renderComparisonOutputs (
    juce::AudioBuffer<float>& buffer, int64_t positionSamples, bool hasPosition,
    bool playing, bool bypassed, bool nonRealtimeMode)
{
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
