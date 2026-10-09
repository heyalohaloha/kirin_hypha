#pragma once
#include "HyphaAttackV2Formatter.h"
#include <array>
#include <memory>
#include <vector>

namespace hypha::attack_v2
{
enum class Scope { wholePoint, wholeInterval, confirmedSubset, noScalar, single };
enum class ClockState { initial, moving, hold };
struct Lane
{
    Scope scope = Scope::noScalar;
    FormattedInterval number;
    std::array<std::uint8_t, 5> counts {};
    std::array<std::uint8_t, KIRIN_REASON_CAPACITY> reasons {};
    std::uint8_t count = 0, exactCount = 0, reason = 0;
    double exactMedian = 0, ageSeconds = 0, resolution = 0;
    bool unbounded = false, withinResolution = false;
};
// Only one adopted producer stamp supplies the lanes and their envelope. Navigation may advance
// independently; cutoff/viewport are explicitly different, and no median is recomputed in UI.
struct Presentation
{
    KirinSnapshotHeader header {};
    std::array<Lane, 4> lanes {};
    std::shared_ptr<const KirinAttackBandSummaryV2> summary;
    std::shared_ptr<const KirinAttackSingleSnapshotV2> single;
    std::uint64_t revision = 0;
    std::int64_t viewport = -1, factsCutoff = -1;
    ClockState clock = ClockState::initial;
    bool live = true, inputActive = false, details = false;
    double maximumSubsetAge = 0;
};
bool sameSource (const KirinSnapshotSourceKey&, const KirinSnapshotSourceKey&) noexcept;
bool sameEvent (const KirinSnapshotEventKey&, const KirinSnapshotEventKey&) noexcept;
bool sameAuthority (const KirinSnapshotHeader&, const KirinSnapshotHeader&) noexcept;
bool validHeader (const KirinSnapshotHeader&) noexcept;
bool validSummary (const KirinAttackBandSummaryV2&) noexcept;
bool validSingle (const KirinAttackSingleSnapshotV2&) noexcept;
Presentation summaryPresentation (std::shared_ptr<const KirinAttackBandSummaryV2>);
Presentation singlePresentation (std::shared_ptr<const KirinAttackSingleSnapshotV2>);
juce::String scopeText (const Lane&, bool live);
juce::String laneReason (const Lane&);
}
