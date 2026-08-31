#pragma once

#include <cstdint>
#include <vector>

#include <juce_core/juce_core.h>

namespace hypha::pre_display
{
    enum class GuideTargetRole
    {
        pre,
        post,
    };

    enum class DisplayStatus
    {
        none,
        received,
        waitingForProjectClock,
        active,
        next,
        end,
    };

    enum class GuideFactPhase
    {
        none,
        next,
        cue,
        active,
        held,
    };

    enum class GuideClockState
    {
        unavailable,
        projectable,
        outsideGuideRange,
    };

    struct DisplaySnapshot
    {
        DisplayStatus status = DisplayStatus::none;
        juce::String guideId;
        juce::String contentHash;
        juce::String payloadKind;
        juce::String primary;
        juce::String detail;
        juce::String stateText;
        bool sectionActive = false;
        bool cueActive = false;
    };

    struct RuntimeIdentity
    {
        GuideTargetRole role = GuideTargetRole::pre;
        juce::String runtimeInstanceId;
        juce::String instanceId;
        juce::String projectUuid;
        juce::String dawSessionUuid;
        juce::String name;
        juce::String pluginVersion;
        juce::String pluginFormat;
        juce::String platform;
        juce::String architecture;
        std::uint32_t hostProcessId = 0;
        juce::String workId;
        juce::String bindingId;
    };

    struct ConnectionRequest
    {
        GuideTargetRole targetRole = GuideTargetRole::pre;
        juce::String bindingId;
        juce::String workId;
        juce::String workTitle;
        std::int64_t observedAtMs = 0;
        std::int64_t expiresAtMs = 0;

        bool validAt (std::int64_t nowMs) const noexcept
        {
            return bindingId.isNotEmpty() && workId.isNotEmpty()
                && observedAtMs >= 0 && expiresAtMs >= observedAtMs
                && expiresAtMs >= nowMs;
        }
    };

    enum class TemporalFactKind
    {
        measuredInterval,
        instantMarker,
    };

    enum class GuidePresentationFactKind
    {
        inspectEvent,
        maskingMeasuredInterval,
        maskingReviewSelection,
    };

    struct GuideItem
    {
        juce::String itemId;
        juce::String selectionRef;
        juce::String label;
        juce::String sourceLabel;
        juce::String channel;
        juce::String frequencyBasis;
        std::int64_t startNs = 0;
        std::int64_t endNs = 0;
        TemporalFactKind temporalKind = TemporalFactKind::measuredInterval;
        double lowHz = 0.0;
        double highHz = 0.0;
        bool hasBand = false;
    };

    struct GuideReviewSelection
    {
        juce::String selectionId;
        std::int64_t startNs = 0;
        std::int64_t endNs = 0;
        double lowHz = 0.0;
        double highHz = 0.0;
        bool hasBand = false;
    };

    // Bounded, UI-facing fact copied by the Guide worker. Domain views consume this type instead
    // of reparsing transport JSON or inferring time state from presentation text.
    struct GuidePresentationFact
    {
        GuidePresentationFactKind kind = GuidePresentationFactKind::inspectEvent;
        GuideFactPhase phase = GuideFactPhase::none;
        juce::String itemId;
        juce::String selectionRef;
        juce::String label;
        juce::String sourceLabel;
        juce::String channel;
        juce::String frequencyBasis;
        std::int64_t startNs = 0;
        std::int64_t endNs = 0;
        TemporalFactKind temporalKind = TemporalFactKind::measuredInterval;
        double lowHz = 0.0;
        double highHz = 0.0;
        bool hasBand = false;
        bool focused = false;
    };

    struct GuidePresentationSnapshot
    {
        GuideTargetRole targetRole = GuideTargetRole::pre;
        DisplayStatus status = DisplayStatus::none;
        GuideClockState clockState = GuideClockState::unavailable;
        juce::String guideId;
        juce::String contentHash;
        juce::String payloadKind;
        juce::String sourcePairLabel;
        std::int64_t revision = 0;
        std::int64_t sourcePositionNs = 0;
        GuidePresentationFact primary;
        GuidePresentationFact next;
        GuidePresentationFact maskingFocus;
        GuidePresentationFact nextMaskingFocus;
        int overlapCount = 0;
        bool guideAvailable = false;
        bool hasSourcePosition = false;
        bool clockPaused = false;
        bool hasPrimary = false;
        bool hasNext = false;
        bool hasMaskingFocus = false;
        bool hasNextMaskingFocus = false;
        bool truncated = false;
    };

    struct GuideModel
    {
        GuideTargetRole targetRole = GuideTargetRole::pre;
        juce::String cacheKey;
        juce::String protocolVersion;
        juce::String groupId;
        juce::String workId;
        juce::String bindingId;
        juce::String runtimeInstanceId;
        juce::String guideId;
        juce::String contentHash;
        juce::String payloadKind;
        juce::String focusEventId;
        juce::String sourcePairLabel;
        juce::String maskingMeasurementState;
        std::int64_t sourceZeroProjectNs = 0;
        std::int64_t revision = 0;
        std::vector<GuideItem> items;
        std::vector<GuideReviewSelection> reviewSelections;

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
        GuideTargetRole targetRole = GuideTargetRole::pre;
        juce::String groupId;
        juce::String workId;
        juce::String bindingId;
        juce::String runtimeInstanceId;
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
