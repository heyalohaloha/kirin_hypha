#include "PluginProcessor.h"

// Plugin factory entry point for the POST target (B-070). Identical to PluginMainPRE.cpp
// except for the Role: POST drives enable_post_writes (post.json Δ via select_target_pre).
// Pairing UI (set_pair_target / keep / stop / poll_delta) is not wired in this step — the
// POST foundation only does Watch measurement + the post.json runtime, same as PRE's base.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KirinHyphaProcessorBase (KirinHyphaProcessorBase::Role::Post);
}
