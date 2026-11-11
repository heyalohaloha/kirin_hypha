#pragma once
#include <juce_core/juce_core.h>
#include "HyphaQueuedStartText.h"

namespace hypha::live_compare_ui
{
// The one press waiting for the A/B tail, and the pair it was pressed for (PluginEditorAfterTail.cpp).
struct AfterTailWait
{
    AfterTail action = AfterTail::none;
    juce::String pairKey;                // the pair it was pressed for
    std::uint64_t ticket = 0;            // restore and pairing generation at the press (checked again at the start)
};
}
