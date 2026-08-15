#pragma once

#include <cstdint>
#include <vector>

#include <juce_core/juce_core.h>

namespace hypha::pre_display
{
    enum class DisplayStatus
    {
        none,
        received,
        waitingForProjectClock,
        active,
        next,
        end,
    };

    struct DisplaySnapshot
    {
        DisplayStatus status = DisplayStatus::none;
        juce::String guideId;
        juce::String contentHash;
        juce::String payloadKind;
        juce::String primary;
        juce::String detail;
        bool sectionActive = false;
    };

    struct RuntimeIdentity
    {
        juce::String instanceId;
        juce::String projectUuid;
        juce::String dawSessionUuid;
        juce::String name;
        juce::String pluginVersion;
        juce::String pluginFormat;
        juce::String platform;
        juce::String architecture;
        std::uint32_t hostProcessId = 0;
    };

    struct GuideItem
    {
        juce::String itemId;
        juce::String label;
        juce::String sourceLabel;
        juce::String channel;
        juce::String frequencyBasis;
        std::int64_t startNs = 0;
        std::int64_t endNs = 0;
        double lowHz = 0.0;
        double highHz = 0.0;
        bool hasBand = false;
    };

    struct GuideModel
    {
        juce::String cacheKey;
        juce::String groupId;
        juce::String guideId;
        juce::String contentHash;
        juce::String payloadKind;
        juce::String focusEventId;
        juce::String sourcePairLabel;
        juce::String maskingMeasurementState;
        std::int64_t sourceZeroProjectNs = 0;
        std::int64_t revision = 0;
        std::vector<GuideItem> items;

        bool valid() const noexcept { return guideId.isNotEmpty(); }
        bool hasMeasuredEmptyMasking() const noexcept
        {
            return payloadKind == "masking" && items.empty()
                && maskingMeasurementState == "measured";
        }
    };

    enum class GuideRefreshState
    {
        unavailable,
        cleared,
        accepted,
        rejected,
    };

    struct GuideReceipt
    {
        GuideRefreshState state = GuideRefreshState::unavailable;
        juce::String groupId;
        juce::String guideId;
        juce::String contentHash;
        juce::String payloadKind;
        std::int64_t revision = 0;

        bool hasGuideIdentity() const noexcept
        {
            return groupId.isNotEmpty() && guideId.isNotEmpty()
                && contentHash.isNotEmpty() && payloadKind.isNotEmpty() && revision > 0;
        }
    };
}
