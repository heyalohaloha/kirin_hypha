#pragma once

#include "HyphaReferenceComponent.h"

#include <algorithm>
#include <memory>
#include <vector>

// V の Version の一覧の並び。今の曲の Version だけを最初に出し、ほかの曲は「OTHER SONGS」の中へ畳む。今の曲は、選んで
// いる Version と AUTO の印の Version の曲。どちらも無ければ、どの曲か分からないので全部を曲ごとにまとめて出す。
// 見せ方だけの話で、AUTO が探す範囲は狭めない。
namespace hypha::reference_ui
{
inline constexpr const char* versionsElsewhereTitle = "OTHER SONGS";

inline void arrangeVersionRows (std::vector<SelectionOption>& rows, const juce::String& selectedId,
                                const juce::String& autoId)
{
    juce::StringArray current, order;
    int index = 0;
    for (auto& row : rows)
    {
        row.sourceIndex = index++;
        order.addIfNotAlreadyThere (row.workId);
        if (row.id == selectedId || (autoId.isNotEmpty() && row.id == autoId))
            current.addIfNotAlreadyThere (row.workId);
    }
    const auto rank = [&] (const SelectionOption& row)
    {
        const auto mine = current.indexOf (row.workId);
        return mine >= 0 ? mine : current.size() + order.indexOf (row.workId);
    };
    std::stable_sort (rows.begin(), rows.end(),
                      [&] (const auto& left, const auto& right) { return rank (left) < rank (right); });
    for (auto& row : rows)
        row.elsewhere = ! current.isEmpty() && ! current.contains (row.workId);
}

// 行の札。AUTO の印と採用の印は 1 つの札に並べる（一覧の文字は最後の 3 つの空白の後ろを札として訳す）。
inline void markVersionRows (std::vector<SelectionOption>& rows, const juce::String& autoId,
                             const reference_audition::RuntimeAdoptedVersions* adopted)
{
    for (auto& row : rows)
    {
        const bool chosenByKirinOs = adopted != nullptr && adopted->adopted (row.workId, row.versionId);
        const bool automatic = row.id == autoId;
        if (chosenByKirinOs || automatic)
            row.label += chosenByKirinOs && automatic ? "   ADOPTED / AUTO" : chosenByKirinOs ? "   ADOPTED" : "   AUTO";
    }
}
}
