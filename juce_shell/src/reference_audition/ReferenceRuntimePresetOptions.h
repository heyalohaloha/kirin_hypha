#pragma once
#include "ReferenceAuditionController.h"
#include "ReferenceRuntimeV2Model.h"
#include "ReferenceRuntimePendingPresets.h"
#include <algorithm>

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
                    item.name + " · WORK", item.sourceTemplateArtifact.revisionId, pending });
        };
        for (const auto& item : workspace.presets) appendWork (item, false);
        for (const auto& item : workspace.manifest.pendingPresets) appendWork (item, true);
    }
}
