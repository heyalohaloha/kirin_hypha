#pragma once
#include "ReferenceAuditionController.h"
#include "ReferenceRuntimeV2Model.h"
#include "ReferenceRuntimePendingPresets.h"
#include "ReferenceLibrarySongs.h"
#include <algorithm>
#include <set>

namespace hypha::reference_audition
{
    inline juce::String runtimePresetDisplaySelection (const Snapshot& snapshot)
    {
        if (snapshot.presetSelectionTargetId.isNotEmpty() && snapshot.presetSelectionStatus != "prepared")
            return snapshot.presetSelectionTargetId;
        const auto selected = snapshot.presetSelectionTargetId.isEmpty()
            ? snapshot.presetId : snapshot.presetSelectionTargetId;
        for (const auto& option : snapshot.presets)
            if (! option.requiresPreparation && runtimePresetOptionIdentity (option.id) == selected) return option.id;
        return selected;
    }

    inline void appendRuntimePresetOptions (Snapshot& snapshot, const RuntimeWorkspace& workspace)
    {
        if (workspace.library)
            for (const auto& preset : workspace.presets)
                if (!preset.versionEntry && !preset.songEntry && preset.sourcePresetArtifact.presetId == snapshot.presetId)
                    for (const auto& check : preset.checks)
                    {
                        snapshot.checkViewBindings[check.checkId] = check.viewBindings;
                        if (check.mode == "audition_only") snapshot.listeningChecks.insert (check.checkId);
                        if (check.candidates.empty())
                            snapshot.checkTargets.push_back ({ check.checkId + "/",
                                check.label + " / NO SOURCE IN KIRIN OS", {}, false, check.label, "NO SOURCE IN KIRIN OS" });
                        for (const auto& candidate : check.candidates)
                            snapshot.checkTargets.push_back ({ check.checkId + "/" + candidate.candidateId,
                                check.label + "  /  " + candidate.displayName,
                                {}, ! candidate.prepared, check.label, candidate.displayName });
                    }
        std::set<juce::String> versions;
        if (workspace.library)
            for (const auto& preset : workspace.presets)
                if (!preset.songEntry && (!workspace.independentVersions || preset.versionEntry))
                for (const auto& check : preset.checks)
                    for (const auto& candidate : check.candidates)
                        if (candidate.sourceKind == "work_version"
                            && versions.insert (candidate.sourceIdentityKey).second)
                            snapshot.versions.push_back ({ preset.sourcePresetArtifact.presetId
                                + "/" + check.checkId + "/" + candidate.candidateId,
                                candidate.displayName, {}, ! candidate.prepared });
        for (const auto& global : workspace.globalPresetCatalog.presets)
        {
            const bool ready = std::any_of (workspace.presets.begin(), workspace.presets.end(), [&] (const auto& item) {
                return item.sourceTemplateArtifact.presetId == global.presetId
                    && item.sourceTemplateArtifact.revisionId == global.revisionId;
            });
            snapshot.presets.push_back ({ global.presetId, global.nameSnapshot, global.revisionId, ! ready });
        }
        const auto appendWork = [&] (const auto& item, bool pending) {
            const bool sameGlobalRevision = std::any_of (workspace.globalPresetCatalog.presets.begin(),
                workspace.globalPresetCatalog.presets.end(), [&] (const auto& global) {
                    return item.sourceTemplateArtifact.presetId == global.presetId
                        && item.sourceTemplateArtifact.revisionId == global.revisionId;
                });
            if (! sameGlobalRevision)
                snapshot.presets.push_back ({ "work:" + item.sourcePresetArtifact.presetId,
                    item.name + " / WORK", item.sourceTemplateArtifact.revisionId, pending });
        };
        for (const auto& item : workspace.presets) if (!item.versionEntry && !item.songEntry) appendWork (item, false);
        if (workspace.library && workspace.librarySets) snapshot.checkSetRanks = workspace.librarySets->checkSets;
        if (workspace.library)
        {
            snapshot.songSetsIssue = workspace.librarySetsIssue;
            snapshot.librarySkipped = workspace.librarySkipped;
            snapshot.librarySkipped.insert (snapshot.librarySkipped.end(), workspace.setsSkipped.begin(), workspace.setsSkipped.end());
        }
        if (workspace.library && workspace.librarySets)
            for (const auto& set : workspace.librarySets->songSets)
            {
                RuntimeSongSetOption option { set.songSetId, set.name, set.rank, {}, {} };
                for (const auto& song : set.songs)
                {
                    const auto entry = referenceSongEntryId (set.songSetId, song.candidateId);
                    option.songs.push_back ({ referenceSongSelectionId (entry, song.candidateId),
                                              song.displayName, set.songSetId, ! song.prepared });
                    const auto facts = workspace.librarySets->songFacts.find (entry);
                    option.facts.push_back (facts != workspace.librarySets->songFacts.end() ? facts->second : RuntimeSongFacts {});
                }
                snapshot.songSets.push_back (std::move (option));
            }
        for (const auto& item : workspace.manifest.pendingPresets) appendWork (item, true);
    }
}
