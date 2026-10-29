#pragma once
#include <atomic>
#include <juce_core/juce_core.h>

namespace hypha::automatic_pair
{
// Intent and discovery are non-audio state. The existing exact pair remains the only binding.
struct State
{
    mutable juce::CriticalSection lock;
    std::atomic<bool> inhibited { false };
    std::atomic<bool> disconnected { false };
};
}
