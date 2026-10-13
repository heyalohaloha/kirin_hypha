#pragma once
#include "../src/PluginProcessor.h"
#include <atomic>
#include <stdexcept>

namespace hypha::tests
{
// Native fixtures own their opaque instance IDs. Seed only an empty persist field before
// prepare; retain restored/explicit IDs and the real project/session resolution and timers.
// No test hook or alternate identity generation is compiled into the shipping processor.
struct FixtureInstanceField
{
    using Type = juce::String KirinHyphaProcessorBase::*;
    friend Type fixtureInstanceField (FixtureInstanceField);
};
template <class Tag, typename Tag::Type Member> struct FixtureFieldAccess
{
    friend typename Tag::Type fixtureInstanceField (Tag) { return Member; }
};
template struct FixtureFieldAccess<FixtureInstanceField, &KirinHyphaProcessorBase::persistInstanceId>;
struct FixtureReadyField
{
    using Type = std::atomic<bool> KirinHyphaProcessorBase::*;
    friend Type fixtureInstanceField (FixtureReadyField);
};
template struct FixtureFieldAccess<FixtureReadyField, &KirinHyphaProcessorBase::writesEnabled>;

inline bool fixtureStorageReady (const KirinHyphaProcessorBase& processor)
{
    // Supplying an ID must not make an asynchronous product fixture look ready early.
    return (processor.*fixtureInstanceField (FixtureReadyField {})).load (std::memory_order_acquire);
}


inline void prepareFixtureProcessor (KirinHyphaProcessorBase& processor, double rate, int frames)
{
    auto& id = processor.*fixtureInstanceField (FixtureInstanceField {});
    if (id.isEmpty())
    {
        static std::atomic<unsigned> next { 1 };
        const auto value = next.fetch_add (1);
        if (value > 0xffff) throw std::runtime_error ("fixture instance ID budget exhausted");
        id = "i" + juce::String::toHexString (value);
    }
    processor.prepareToPlay (rate, frames);
}
}
