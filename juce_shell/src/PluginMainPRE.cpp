#include "PluginProcessor.h"

// Plugin factory entry point for the PRE target (B-070). JUCE 7.0.12 calls
// ::createPluginFilter() (global scope, no juce_ prefix) from
// detail/juce_CreatePluginFilter.h. The PRE target compiles this file; the POST target
// compiles PluginMainPOST.cpp instead — the only per-target difference is the Role.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KirinHyphaProcessorBase (KirinHyphaProcessorBase::Role::Pre);
}
