#pragma once
#include "reference_audition/ReferenceAuditionController.h"
#include "reference_audition/ReferenceTrackingGain.h"

// C の MATCH（2026-10-31）：A の直近が Cue の長さ（最長 30 秒）に足りないあいだに押された MATCH は断らずに
// 待たせ、たまり次第合わせる（押した C の MATCH を捨てない）。C を止めた・
// 替えた・元の音量の C は待ちを捨てる。上限を超えるなら押したときと同じく A を下げる承認を出す。
namespace hypha::reference_ui
{
enum class RematchWait : std::uint8_t { done, wait, offerLowerA, drop };

constexpr RematchWait rematchWait (reference_audition::RematchResult result) noexcept
{
    using R = reference_audition::RematchResult;
    switch (result)
    {
        case R::matched:          return RematchWait::done;
        case R::ceilingExceeded:  return RematchWait::offerLowerA;
        case R::levelUnavailable: return RematchWait::wait;
        case R::notPlaying:
        case R::original:
        case R::peakMatch:        return RematchWait::drop;
    }
    return RematchWait::drop;
}

// The C a waiting MATCH belongs to: the C that sounds, its Check and Cue, and C's own selection
// generation. Showing B or V while C sounds keeps the press; another C selection drops it.
inline juce::String rematchTarget (const reference_audition::Snapshot& latest)
{
    if (latest.audibleComparisonSlot != 2) return {};
    const auto& check = latest.checkSelection ? *latest.checkSelection : latest;
    return check.checkId + "|" + check.cueId + "|" + juce::String ((juce::int64) check.selectionGeneration);
}
}
