#pragma once
#include "ReferenceRuntimeRepositoryParsing.h"
#include <juce_cryptography/juce_cryptography.h>
#include <set>

namespace hypha::reference_audition
{
inline juce::String referenceVersionEntryId (const RuntimeCandidate& candidate)
{
    const auto key = "hypha-version:" + candidate.sourceWorkId + ":" + candidate.sourceRecordingId + ":" + candidate.sourceVersionId;
    const auto hash = juce::SHA256 (key.toRawUTF8(), key.getNumBytesAsUTF8()).toHexString();
    return hash.substring (0, 8) + "-" + hash.substring (8, 12) + "-4" + hash.substring (13, 16)
        + "-8" + hash.substring (17, 20) + "-" + hash.substring (20, 32);
}

inline bool readReferenceLibraryVersions (const juce::File& root, const juce::var& values, RuntimeWorkspace& workspace)
{
    using namespace runtime_repository_parsing;
    const auto* array = values.getArray();
    if (array == nullptr || array->size() > 512) return false;
    std::set<juce::String> ids;
    for (const auto& value : *array)
    {
        const auto* object = value.getDynamicObject();
        RuntimePresetReceipt receipt;
        receipt.presetId = value["entry_id"].toString(); receipt.revisionId = value["revision_id"].toString();
        receipt.sha256 = value["sha256"].toString(); receipt.relativePath = value["relative_path"].toString();
        if (object == nullptr || !exactProperties (*object, { "entry_id", "revision_id", "relative_path", "sha256", "bytes" })
            || !uuidV4 (receipt.presetId) || receipt.revisionId != receipt.presetId || !sha256 (receipt.sha256)
            || !ids.insert (receipt.presetId).second
            || std::any_of (workspace.presets.begin(), workspace.presets.end(), [&] (const auto& preset) { return preset.sourcePresetArtifact.presetId == receipt.presetId; })
            || receipt.relativePath != "plugin_data/reference/v2/library/versions/" + receipt.sha256 + ".json"
            || !exactInteger (value["bytes"], 1, 65536, receipt.bytes)) return false;
        juce::MemoryBlock bytes; juce::var descriptor;
        if (!readJson (root.getChildFile ("library/versions/" + receipt.sha256 + ".json"), 65536, bytes, descriptor)
            || bytes.getSize() != static_cast<size_t> (receipt.bytes) || juce::SHA256 (bytes).toHexString() != receipt.sha256) return false;
        RuntimeCandidate candidate;
        const auto* item = descriptor.getDynamicObject();
        if (item == nullptr || !exactProperties (*item, { "format", "version", "entry_id", "display_name", "candidate" })
            || descriptor["format"] != "kirin_hypha_reference_library_version" || descriptor["version"] != "1.0"
            || descriptor["entry_id"] != receipt.presetId
            || !parseLibraryVersionCandidate (descriptor["candidate"], candidate)
            || candidate.sourceKind != "work_version" || !candidate.prepared
            || candidate.candidateId != receipt.presetId || referenceVersionEntryId (candidate) != receipt.presetId
            || descriptor["display_name"] != candidate.displayName || candidate.cues.size() != 1
            || candidate.defaultCueId != receipt.presetId || candidate.cues[0].cueId != receipt.presetId
            || candidate.cues[0].startSample != 0 || candidate.cues[0].loopEnabled) return false;
        RuntimePreset entry;
        entry.versionEntry = true;
        entry.sourcePresetArtifact = receipt;
        entry.name = candidate.displayName.substring (0, 80);
        RuntimeCheck check;
        check.checkId = receipt.presetId; check.label = "Version"; check.mode = "audition_with_facts";
        check.viewBindings = { "waveform", "loudness", "dynamics" }; check.comparisonMode = "loudness_match";
        check.candidates.push_back (std::move (candidate)); entry.checks.push_back (std::move (check));
        workspace.presets.push_back (std::move (entry));
    }
    workspace.independentVersions = true;
    return true;
}
}
