#pragma once

#include <juce_core/juce_core.h>

namespace hypha::reference_audition
{
bool writeWorkflowDurable (const juce::File& root, const juce::File& target,
                           const juce::String& content, bool replace);
juce::String workflowCheckpointFromHash (const juce::String& hash);
}
