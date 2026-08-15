#pragma once

#include <juce_core/juce_core.h>

#include "PreDisplayClock.h"
#include "PreDisplayModel.h"

namespace hypha::pre_display
{
    bool writePresence (const juce::File& transportRoot,
                        const RuntimeIdentity& identity,
                        const ClockSnapshot& clock,
                        std::int64_t clockObservedAtMs,
                        std::int64_t nowMs);

    bool writeAcknowledgement (const juce::File& transportRoot,
                               const RuntimeIdentity& identity,
                               const GuideModel& guide,
                               const DisplaySnapshot& display,
                               std::int64_t nowMs);

    void removePresence (const juce::File& file);
    void removeAcknowledgement (const juce::File& file);
}
