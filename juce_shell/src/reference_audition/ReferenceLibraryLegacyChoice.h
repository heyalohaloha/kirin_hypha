#pragma once
#include "ReferenceLibraryVersions.h"
#include "ReferenceComparisonSettings.h"

namespace hypha::reference_audition
{
inline juce::String migrateLegacyVersionChoice (const juce::File& root, const RuntimeWorkspace& workspace,
                                                const ReferenceChoice& choice)
{
    if (!workspace.independentVersions || choice.candidateId.isEmpty()) return {};
    const auto fromPreset = [&] (const RuntimePreset& preset) -> juce::String {
        if (preset.versionEntry || preset.sourcePresetArtifact.presetId != choice.presetId) return {};
        for (const auto& check : preset.checks) if (check.checkId == choice.checkId)
            for (const auto& old : check.candidates) if (old.candidateId == choice.candidateId && old.sourceKind == "work_version")
                for (const auto& entry : workspace.presets) if (entry.versionEntry)
                {
                    const auto& current = entry.checks[0].candidates[0];
                    if (old.sourceIdentityKey == current.sourceIdentityKey) return entry.sourcePresetArtifact.presetId;
                }
        return {};
    };
    for (const auto& preset : workspace.presets)
    {
        if (preset.versionEntry && preset.sourcePresetArtifact.presetId == choice.presetId) return {};
        const auto id = fromPreset (preset); if (id.isNotEmpty()) return id;
    }
    using namespace runtime_repository_parsing;
    // Old settings saved C-based IDs. Resolve only from immutable, hash-verified
    // delivery receipts, never by display name or an arbitrary current Version.
    const auto first = juce::jmax<std::int64_t> (1, workspace.manifest.revision - 128);
    for (auto revision = workspace.manifest.revision - 1; revision >= first; --revision)
    {
        juce::MemoryBlock bytes; juce::var manifest;
        if (!readJson (root.getChildFile ("library/manifests/" + juce::String (revision) + ".json"), maximumManifestBytes, bytes, manifest)
            || manifest["format"] != "kirin_hypha_reference_library" || manifest["version"] != "1.0") continue;
        const auto* presets = manifest["presets"].getArray(); if (presets == nullptr || presets->size() > 133) continue;
        for (const auto& value : *presets) if (value["preset_id"] == choice.presetId)
        {
            RuntimePresetReceipt receipt;
            receipt.presetId = choice.presetId; receipt.revisionId = value["revision_id"].toString();
            receipt.relativePath = value["relative_path"].toString(); receipt.sha256 = value["sha256"].toString();
            if (!uuidV4 (receipt.revisionId) || !sha256 (receipt.sha256)
                || receipt.relativePath != "plugin_data/reference/v2/library/presets/" + receipt.sha256 + ".json"
                || !exactInteger (value["bytes"], 1, maximumPresetBytes, receipt.bytes)) continue;
            juce::var json; RuntimePreset preset;
            if (readJson (root.getChildFile ("library/presets/" + receipt.sha256 + ".json"), maximumPresetBytes, bytes, json)
                && bytes.getSize() == static_cast<size_t> (receipt.bytes) && juce::SHA256 (bytes).toHexString() == receipt.sha256
                && parsePreset (json, receipt, {}, preset, true))
            { const auto id = fromPreset (preset); if (id.isNotEmpty()) return id; }
        }
    }
    return {};
}
}
