#pragma once
#include "HostContext.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace hypha::local_blind
{
// Uses JUCE's public client extension hook. No vendor patch, global host pointer,
// synthetic project identity, polling timer, or audio-thread interface query.
class VST3HostContext final : public juce::VST3ClientExtensions
{
public:
    std::int32_t queryIEditController (const Steinberg::TUID iid, void** out) override
    { return context.queryEditController (iid, out); }
    void setIComponentHandler (Steinberg::FUnknown* value) override { context.setComponentHandler (value); }
    void setIHostApplication (Steinberg::FUnknown* value) override { context.setHostApplication (value); }
    HostContext context;
};
}
