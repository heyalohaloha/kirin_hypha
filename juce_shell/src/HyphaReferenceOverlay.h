#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "HyphaReferenceComponent.h"
#include "HyphaReferenceOverlayParts.h"
#include "HyphaReferencePreviewGain.h"
#include "HyphaReferenceSongChoice.h"
#include "reference_audition/ReferenceVisualTimeline.h"

// B の画面の Balance の重ね比べ（ALL）。ONE は今の形（A・選んだ曲・セットの帯）、ALL は A（金）と、左の一覧の
// 番号で出した線を重ねる。一覧はセットの曲の後に、ドロップした目標を入れた順に並べる（ALL の時だけ）。線を出し
// 入れしても鳴る音は変えない（鳴るのは選んだ 1 曲のまま。目標には音が無い）。色は出した時に決め、消すまで
// 変えない（reference_audition::BalanceOverlay）。
namespace hypha::reference_ui
{
// What a target row says when it is pressed to be heard, and the line's own words.
inline constexpr const char* targetSilentText = "A target is measured data with no audio. Compare it on C.";
inline constexpr const char* overlayEmptyText = "PRESS A NUMBER TO SHOW ITS LINE";
inline constexpr const char* shapeEmptyText = "TURN ON SONGS TO SEE THEIR SHAPE";

// One row of B's list as the overlay sees it.
struct OverlayItem
{
    juce::String id, name;
    int number = 0;  // as the list numbers it, from 1
    bool target = false;
    const std::vector<double>* centers = nullptr;
    const std::vector<float>* medians = nullptr;
    double gainDb = std::numeric_limits<double>::quiet_NaN();  // the level B would play it at; NaN: not matched
    const reference_audition::BalanceOverlay::Line* line = nullptr;  // shown in ALL
    bool drawable() const noexcept { return centers != nullptr && medians != nullptr && ! medians->empty(); }
};

// A target at B's level: A's window less the target's Integrated, with the loudness channel sum
// normalized to A's layout as C does (HyphaReferenceTargetView.h).
inline double targetOverlayGainDb (const State& state, const reference_audition::MeasuredTarget& target)
{
    if (! std::isfinite (state.aWindowLoudness) || ! std::isfinite (target.integrated)) return std::numeric_limits<double>::quiet_NaN();
    const auto channels = state.visualTimeline ? state.visualTimeline->aTickChannels : 0;
    const auto offset = channels >= 1 && channels <= 2 ? 10.0 * std::log10 (double (channels) / target.channels) : 0.0;
    return state.aWindowLoudness - (target.integrated + offset);
}

// The list in its order: the set's songs, then in ALL the targets in the order added.
inline std::vector<OverlayItem> overlayItems (const State& state)
{
    std::vector<OverlayItem> items;
    const bool bPlaying = state.bSelected && state.audibleComparisonSlot == 3;
    for (size_t index = 0; index < state.songs.size(); ++index)
    {
        const auto& song = state.songs[index];
        OverlayItem item { song.id, songChoiceName (song.label), static_cast<int> (index) + 1 };
        if (index < state.songFacts.size() && ! state.songFacts[index].medianDb.empty())
        {
            const auto& fact = state.songFacts[index];
            const bool chosen = song.id == state.songId;
            item.centers = &fact.centersHz;
            item.medians = &fact.medianDb;
            // As ONE draws it: the playing song at the gain it plays at, the others at the gain they would.
            item.gainDb = chosen && bPlaying && std::isfinite (state.appliedGainDb) ? state.appliedGainDb
                                                                                    : songPreviewGainDb (state, fact, chosen);
        }
        items.push_back (std::move (item));
    }
    if (state.overlay.all && state.targetsEnabled)
        for (const auto& target : state.targets.collection.items)
        {
            OverlayItem item { target.id, target.name, static_cast<int> (items.size()) + 1, true };
            item.centers = &target.spectrum.centersHz;
            item.medians = &target.spectrum.medianDb;
            item.gainDb = targetOverlayGainDb (state, target);
            items.push_back (std::move (item));
        }
    if (state.overlay.all)
        for (auto& item : items) item.line = state.overlay.find (item.id);
    return items;
}

// SHAPE：出している線（every なら番号に関わらず一覧の全部＝ALL SHAPE）の、帯域ごとの平均（dB）と最小〜最大。
// どれも B で鳴る音量にそろえた値（そろえられない線はそのままの高さで入れ、unmatched）。値の無い線と、帯域の
// 並びが最初の線と違う線は入れない（数えない）。
struct OverlayShape
{
    std::vector<double> centers;
    std::vector<float> mean, low, high;
    int count = 0;
    bool unmatched = false;
};

inline OverlayShape overlayShape (const std::vector<OverlayItem>& items, bool every = false)
{
    OverlayShape shape;
    const auto sameBands = [&shape] (const std::vector<double>& centers)
    {
        if (centers.size() != shape.centers.size()) return false;
        for (size_t band = 0; band < centers.size(); ++band)
            if (! (std::abs (centers[band] / shape.centers[band] - 1.0) < 1.0e-3)) return false;
        return true;
    };
    for (const auto& item : items)
    {
        if ((item.line == nullptr && ! every) || ! item.drawable() || item.centers->size() != item.medians->size()) continue;
        if (shape.count == 0)
        {
            shape.centers = *item.centers;
            shape.mean.assign (shape.centers.size(), 0.0f);
            shape.low.assign (shape.centers.size(), std::numeric_limits<float>::max());
            shape.high.assign (shape.centers.size(), std::numeric_limits<float>::lowest());
        }
        else if (! sameBands (*item.centers)) continue;
        shape.unmatched = shape.unmatched || ! std::isfinite (item.gainDb);
        const auto gain = static_cast<float> (std::isfinite (item.gainDb) ? item.gainDb : 0.0);
        for (size_t band = 0; band < shape.centers.size(); ++band)
        {
            const auto level = (*item.medians)[band] + gain;
            shape.mean[band] += level;
            shape.low[band] = std::min (shape.low[band], level);
            shape.high[band] = std::max (shape.high[band], level);
        }
        ++shape.count;
    }
    for (auto& level : shape.mean) level /= static_cast<float> (juce::jmax (1, shape.count));
    return shape;
}
}
