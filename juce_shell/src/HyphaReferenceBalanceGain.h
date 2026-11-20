#pragma once
#include "HyphaReferencePreviewGain.h"
#include <algorithm>
#include <vector>

namespace hypha::reference_ui
{
// Drawing only. Never use these gains for MATCH, audition, or lower-A approval.
inline double balanceReferenceLevel (const State& state)
{
    std::vector<double> levels;
    const bool targetSelected = state.targetsEnabled
        && state.targets.collection.find (state.targets.collection.selected) != nullptr;
    for (size_t i = 0; i < state.songs.size() && i < state.songFacts.size(); ++i)
    {
        const auto& fact = state.songFacts[i];
        const auto level = fact.lufsI + fact.loudnessOffsetLu;
        if (!std::isfinite (level)) continue;
        if (!targetSelected && state.songs[i].id == state.songId) return level;
        levels.push_back (level);
    }
    if (levels.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort (levels.begin(), levels.end());
    const auto middle = levels.size() / 2;
    return levels.size() % 2 ? levels[middle] : levels[middle - 1] * 0.5 + levels[middle] * 0.5;
}
inline double balanceSongGainDb (const State& state, const SongFact& fact, bool selected)
{
    if (!std::isfinite (state.aWindowLoudness))
        return balanceReferenceLevel (state) - (fact.lufsI + fact.loudnessOffsetLu);
    if (selected && state.bSelected && state.audibleComparisonSlot == 3 && std::isfinite (state.appliedGainDb))
        return state.appliedGainDb;
    return songPreviewGainDb (state, fact, selected);
}
inline double balanceTargetGainDb (const State& state, double integrated, double matchedGain)
{
    return std::isfinite (state.aWindowLoudness) ? matchedGain : balanceReferenceLevel (state) - integrated;
}
inline const char* balanceLevelText (const State& state, bool unmatched)
{
    if (unmatched) return "LEVEL NOT MATCHED";
    return !std::isfinite (state.aWindowLoudness) && std::isfinite (balanceReferenceLevel (state))
        ? "MATCHED TO EACH OTHER" : "20 Hz - 20 kHz";
}
}
