#include "ReferenceRuntimeV2Repository.h"
#include "ReferenceRuntimeRepositoryParsing.h"
#include <set>
#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
using namespace runtime_repository_parsing;

RuntimeWorkspaceLoadResult RuntimeV2Repository::refreshLibrary (
    std::shared_ptr<const RuntimeWorkspace> previous) const
{
    const auto file = root.getChildFile ("library/manifest.json");
    if (! file.exists()) return previous ? failure ("reference_library_missing", previous)
                                       : RuntimeWorkspaceLoadResult {};
    juce::MemoryBlock bytes;
    juce::var json;
    if (! readJson (file, maximumManifestBytes, bytes, json))
        return failure ("reference_library_rejected", previous);
    const auto* object = json.getDynamicObject();
    auto next = std::make_shared<RuntimeWorkspace>();
    next->library = true;
    next->publicationHash = juce::SHA256 (bytes).toHexString();
    if (object == nullptr || ! exactProperties (*object,
        { "format", "version", "revision", "default_preset_id", "presets" })
        || json["format"] != "kirin_hypha_reference_library" || json["version"] != "1.0"
        || ! exactInteger (json["revision"], 1, 9'007'199'254'740'991, next->manifest.revision)
        || ! json["default_preset_id"].isString()
        || ! uuidV4 (json["default_preset_id"].toString()))
        return failure ("reference_library_rejected", previous);
    next->manifest.activePresetId = json["default_preset_id"].toString();
    if (previous)
    {
        if (! previous->library || next->manifest.revision < previous->manifest.revision)
            return failure ("reference_library_rollback", previous);
        if (next->manifest.revision == previous->manifest.revision)
            return next->publicationHash == previous->publicationHash
                ? RuntimeWorkspaceLoadResult { RuntimeWorkspaceLoadState::unchanged, previous, {} }
                : failure ("reference_library_revision_conflict", previous);
    }
    const auto* presets = json["presets"].getArray();
    if (presets == nullptr || presets->isEmpty() || presets->size() > 133)
        return failure ("reference_library_presets_rejected", previous);
    std::set<juce::String> ids;
    for (const auto& value : *presets)
    {
        const auto* item = value.getDynamicObject();
        RuntimePresetReceipt receipt;
        receipt.presetId = value["preset_id"].toString();
        receipt.revisionId = value["revision_id"].toString();
        receipt.sha256 = value["sha256"].toString();
        receipt.relativePath = value["relative_path"].toString();
        if (item == nullptr || ! exactProperties (*item,
                { "preset_id", "revision_id", "sha256", "bytes", "relative_path" })
            || ! uuidV4 (receipt.presetId) || ! uuidV4 (receipt.revisionId)
            || ! sha256 (receipt.sha256) || ! ids.insert (receipt.presetId).second
            || receipt.relativePath != "plugin_data/reference/v2/library/presets/" + receipt.sha256 + ".json"
            || ! exactInteger (value["bytes"], 1, maximumPresetBytes, receipt.bytes))
            return failure ("reference_library_receipt_rejected", previous);
        juce::MemoryBlock presetBytes;
        juce::var presetJson;
        const auto presetFile = root.getChildFile ("library/presets/" + receipt.sha256 + ".json");
        RuntimePreset preset;
        if (! readJson (presetFile, maximumPresetBytes, presetBytes, presetJson)
            || presetBytes.getSize() != static_cast<size_t> (receipt.bytes)
            || juce::SHA256 (presetBytes).toHexString() != receipt.sha256
            || ! parsePreset (presetJson, receipt, {}, preset, true))
            return failure ("reference_library_preset_rejected", previous);
        if (receipt.presetId == next->manifest.activePresetId)
            next->manifest.activePresetRevisionId = receipt.revisionId;
        next->globalPresetCatalog.presets.push_back ({ receipt.presetId, receipt.revisionId, preset.name, "user" });
        next->manifest.presetArtifacts.push_back (receipt);
        next->presets.push_back (std::move (preset));
    }
    if (next->manifest.activePresetRevisionId.isEmpty())
        return failure ("reference_library_default_missing", previous);
    return { RuntimeWorkspaceLoadState::updated, next, {} };
}

bool RuntimeV2Repository::libraryOnline (std::int64_t nowMs) const
{
    juce::MemoryBlock bytes;
    juce::var json;
    std::int64_t updated = 0, expires = 0;
    return readJson (root.getChildFile ("library/presence.json"), 4096, bytes, json)
        && json["format"] == "kirin_hypha_reference_library_presence" && json["version"] == "1.0"
        && uuidV4 (json["session_id"].toString())
        && exactInteger (json["updated_at_ms"], 0, 9'007'199'254'740'991, updated)
        && exactInteger (json["expires_at_ms"], 0, 9'007'199'254'740'991, expires)
        && updated <= nowMs + 2000 && nowMs < expires && expires - updated == 5000;
}
}
