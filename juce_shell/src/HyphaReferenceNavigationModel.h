#pragma once

#include <algorithm>
#include <vector>

#include "HyphaReferenceComponent.h"

// The Checks of C and their songs, as the page's tabs and the keys both walk them.
namespace hypha::reference_ui
{
inline constexpr const char* noSource = " / NO SOURCE IN KIRIN OS";

// state.checks は「checkId/candidateId」と「Check の名前  /  曲」の組（Kirin OS の Preset の順）。
struct CheckGroup
{
    juce::String checkId, label;
    std::vector<SelectionOption> songs;
};

inline std::vector<CheckGroup> checkGroups (const std::vector<SelectionOption>& targets)
{
    std::vector<CheckGroup> groups;
    for (const auto& target : targets)
    {
        const auto checkId = target.id.upToFirstOccurrenceOf ("/", false, false);
        // The runtime gives the CHECK and its song apart; only a label without them (an older
        // producer) is split at its first separator.
        const bool parts = target.checkLabel.isNotEmpty();
        const auto separator = target.label.indexOf ("  /  ");
        const bool missing = ! parts && target.label.endsWith (noSource);
        const auto label = parts ? target.checkLabel
                         : missing ? target.label.dropLastCharacters (juce::String (noSource).length())
                         : separator >= 0 ? target.label.substring (0, separator) : target.label;
        const auto song = parts ? target.itemLabel
                        : missing ? juce::String ("NO SOURCE IN KIRIN OS")
                        : separator >= 0 ? target.label.substring (separator + 5) : target.label;
        auto group = std::find_if (groups.begin(), groups.end(), [&checkId] (const auto& item) { return item.checkId == checkId; });
        if (group == groups.end())
        {
            groups.push_back ({ checkId, label, {} });
            group = std::prev (groups.end());
        }
        group->songs.push_back ({ target.id, song });
    }
    return groups;
}

inline const CheckGroup* currentGroup (const std::vector<CheckGroup>& groups, const juce::String& targetId)
{
    const auto checkId = targetId.upToFirstOccurrenceOf ("/", false, false);
    for (const auto& group : groups)
        if (group.checkId == checkId) return &group;
    return nullptr;
}
}
