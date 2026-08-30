#pragma once

#include <juce_core/juce_core.h>

#include "PreDisplayClock.h"
#include "PreDisplayModel.h"

namespace hypha::pre_display
{
    struct RuntimeLease
    {
        RuntimeIdentity identity;
        juce::File presenceFile;
        juce::File acknowledgementFile;
        juce::File capabilityFile;

        bool valid() const noexcept
        {
            return identity.runtimeInstanceId.isNotEmpty() && presenceFile != juce::File();
        }
    };

    RuntimeLease prepareRuntimeLease (const juce::File& transportRoot,
                                      RuntimeIdentity requested,
                                      const RuntimeIdentity& current,
                                      bool alreadyConfigured);

    bool writePresence (const juce::File& transportRoot,
                        const RuntimeIdentity& identity,
                        const ClockSnapshot& clock,
                        std::int64_t clockObservedAtMs,
                        std::int64_t nowMs);

    bool writeCapability (const juce::File& transportRoot,
                          const RuntimeIdentity& identity,
                          std::int64_t nowMs);

    bool writeAcknowledgement (const juce::File& transportRoot,
                               const RuntimeIdentity& identity,
                               const GuideModel& guide,
                               const DisplaySnapshot& display,
                               std::int64_t nowMs);

    bool writeRejectedAcknowledgement (const juce::File& transportRoot,
                                       const RuntimeIdentity& identity,
                                       const GuideReceipt& receipt,
                                       std::int64_t nowMs);

    void removePresence (const juce::File& file);
    void removeAcknowledgement (const juce::File& file);
    void removeCapability (const juce::File& file);
}
