#include "ReferenceRuntimeV2Repository.h"
#include "ReferenceRuntimeRepositoryParsing.h"
#include <algorithm>
#include <set>
#include "ReferenceLibraryVersions.h"
#include "ReferenceLibrarySets.h"
#include "ReferenceLibrarySongs.h"
#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
using namespace runtime_repository_parsing;

namespace
{
// 書き換えの途中（別の manifest の sets）は理由にしない（すぐ追いつく）。
juce::String setsIssue (const juce::String& rejection)
{
    return rejection == "reference_library_sets_stale" ? juce::String {} : rejection;
}

// manifest が同じでも、Kirin OS で順位だけを変えると sets.json だけが書き換わる。
// 読めないときや書き換えの途中（別の manifest の sets）のときは、今の sets をそのまま保つ。
RuntimeWorkspaceLoadResult refreshLibrarySets (const juce::File& root, std::shared_ptr<const RuntimeWorkspace> current)
{
    juce::String rejection;
    auto sets = readReferenceLibrarySets (root, *current, rejection);
    const auto unchanged = RuntimeWorkspaceLoadResult { RuntimeWorkspaceLoadState::unchanged, current, {} };
    const auto issue = rejection == "reference_library_sets_stale" ? current->librarySetsIssue : rejection;
    if (! sets && rejection.isNotEmpty())
    {
        if (issue == current->librarySetsIssue) return unchanged;
        auto updated = std::make_shared<RuntimeWorkspace> (*current);
        updated->librarySetsIssue = issue;
        return { RuntimeWorkspaceLoadState::updated, updated, {} };
    }
    if (sets.has_value() == current->librarySets.has_value() && (! sets || sets->hash == current->librarySets->hash)
        && issue == current->librarySetsIssue)
        return unchanged;
    auto updated = std::make_shared<RuntimeWorkspace> (*current);
    updated->librarySets = std::move (sets);
    updated->librarySetsIssue = issue;
    applyLibrarySongEntries (root, *updated);
    return { RuntimeWorkspaceLoadState::updated, updated, {} };
}
}

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
        json["version"] == "1.1"
            ? std::initializer_list<const char*> { "format", "version", "revision", "default_preset_id", "presets", "versions" }
            : std::initializer_list<const char*> { "format", "version", "revision", "default_preset_id", "presets" })
        || json["format"] != "kirin_hypha_reference_library" || (json["version"] != "1.0" && json["version"] != "1.1")
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
                ? refreshLibrarySets (root, previous)
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
    if (json["version"] == "1.1" && !readReferenceLibraryVersions (root, json["versions"], *next))
        return failure ("reference_library_versions_rejected", previous);
    juce::String setsRejection;
    next->librarySets = readReferenceLibrarySets (root, *next, setsRejection);
    // Kirin OS は manifest を先に書き、sets.json はその直後に続く。追いつくまで（読めないあいだも）前の sets を
    // 保つ（B の曲が一瞬消えて選択や鳴っている B を失わない）。
    if (! next->librarySets && setsRejection.isNotEmpty() && previous && previous->librarySets)
        next->librarySets = carriedLibrarySets (*previous->librarySets, next->manifest);
    next->librarySetsIssue = setsIssue (setsRejection);
    applyLibrarySongEntries (root, *next);
    return { RuntimeWorkspaceLoadState::updated, next, {} };
}

std::shared_ptr<const RuntimeLibraryPreparation> RuntimeV2Repository::libraryPreparation (std::int64_t nowMs) const
{
    juce::MemoryBlock bytes;
    juce::var json;
    std::int64_t updated = 0, expires = 0;
    const auto* object = readJson (root.getChildFile ("library/preparation.json"), 64 * 1024, bytes, json) ? json.getDynamicObject() : nullptr;
    const auto phase = json["phase"].toString();
    const auto* songs = json["songs"].getArray();
    if (object == nullptr
        || ! exactProperties (*object, { "format", "version", "session_id", "updated_at_ms", "expires_at_ms", "phase", "songs" })
        || json["format"] != "kirin_hypha_reference_library_preparation" || json["version"] != "1.0"
        || ! uuidV4 (json["session_id"].toString())
        || ! exactInteger (json["updated_at_ms"], 0, 9'007'199'254'740'991, updated)
        || ! exactInteger (json["expires_at_ms"], 0, 9'007'199'254'740'991, expires)
        || updated > nowMs + 2000 || nowMs >= expires || expires - updated != 5000
        || (phase != "idle" && phase != "working" && phase != "waiting") || songs == nullptr || songs->size() > 256)
        return nullptr;
    // 値は決まった言葉か null（空にする）だけ。
    const auto word = [] (const juce::var& value, std::initializer_list<const char*> allowed, bool nullable, juce::String& out) {
        out = {};
        if (value.isVoid()) return nullable;
        if (! value.isString()) return false;
        out = value.toString();
        return std::any_of (allowed.begin(), allowed.end(), [&out] (const char* item) { return out == item; });
    };
    auto result = std::make_shared<RuntimeLibraryPreparation>();
    result->phase = phase;
    for (const auto& value : *songs)
    {
        const auto* item = value.getDynamicObject();
        RuntimeSongPreparation song;
        std::int64_t ahead = 0;
        if (item == nullptr || ! exactProperties (*item, { "candidate_id", "state", "step", "reason", "retry", "ahead" })
            || ! uuidV4 (value["candidate_id"].toString())
            || ! word (value["state"], { "ready", "playable", "pending" }, false, song.state)
            || ! word (value["step"], { "queued", "resolving", "measuring" }, true, song.step)
            || ! word (value["reason"], { "source_unavailable", "analysis_failed" }, true, song.reason)
            || ! word (value["retry"], { "automatic", "manual" }, true, song.retry)
            || ! exactInteger (value["ahead"], 0, 4096, ahead))
            continue;  // 読めない曲は飛ばす（その曲は今までどおり「準備中」）
        song.ahead = static_cast<int> (ahead);
        song.phase = phase;
        result->songs[value["candidate_id"].toString()] = std::move (song);
    }
    return result;
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
