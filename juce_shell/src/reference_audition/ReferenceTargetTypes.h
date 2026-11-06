#pragma once
#include "ReferenceMeasuredTarget.h"

namespace hypha::reference_audition
{
struct TargetSnapshot
{
    TargetCollection collection;
    bool measuring = false;
    double progress = 0.0;
    juce::String issue;
    std::uint64_t revision = 0;
    std::uint64_t restoreEpoch = 1;
};
struct TargetRequest
{
    enum class Action { importFiles, make, select, remove, cancel } action;
    juce::StringArray files;
    juce::String id;
    std::uint64_t restoreEpoch = 0; // 0 is an immediate internal choice, never an open menu's reply.
};

}
