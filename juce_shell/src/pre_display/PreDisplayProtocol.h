#pragma once

#include <cstdint>

#include <juce_core/juce_core.h>

#include "PreDisplayModel.h"

namespace hypha::pre_display
{
    constexpr std::int64_t maxPointerBytes = 16 * 1024;
    constexpr std::int64_t maxGuideBytes = 1024 * 1024;
    constexpr int maxGuideItems = 2'048;
    constexpr std::int64_t maxSafeJsonInteger = 9'007'199'254'740'991;

    bool safeId (const juce::String& value);
    bool safeHash (const juce::String& value);
    bool safeGuideFileName (const juce::String& value);
    bool canonicalIsoInstant (const juce::String& value);
    juce::String objectString (const juce::DynamicObject& object, const char* name);
    bool objectInteger (const juce::DynamicObject& object, const char* name,
                        std::int64_t minimum, std::int64_t maximum, std::int64_t& result);
    bool parseArtifactVerifiedGuideModel (const juce::DynamicObject& guide,
                                          const juce::String& cacheKey,
                                          GuideModel& model);
}
