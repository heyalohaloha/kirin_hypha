#include "ReferenceLibrarySets.h"
#include "ReferenceRuntimeRepositoryParsing.h"

#include <juce_cryptography/juce_cryptography.h>
#include <set>

namespace hypha::reference_audition
{
using namespace runtime_repository_parsing;

namespace
{
constexpr std::int64_t maximumSetsBytes = 1024 * 1024;
constexpr std::int64_t maximumRangesBytes = 1024 * 1024;
constexpr std::int64_t maximumSafeInteger = 9'007'199'254'740'991;
constexpr int maximumRankedSets = 3;
constexpr int maximumSongsPerSet = 16;
constexpr int maximumSourceRanges = 1024;

// Kirin OS の name80：前後に空白がなく、制御文字を含まない 1〜80 文字。
bool setName (const juce::var& value, juce::String& result)
{
    if (! value.isString()) return false;
    result = value.toString();
    if (result.isEmpty() || result.length() > 80 || result != result.trim()) return false;
    for (auto character : result)
        if (character < 0x20 || (character >= 0x7f && character <= 0x9f) || character == 0x2028 || character == 0x2029)
            return false;
    return true;
}

bool rangesReceipt (const juce::var& value, RuntimeContentReceipt& result)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr || ! exactProperties (*object, { "relative_path", "sha256", "bytes" })
        || ! value["relative_path"].isString() || ! value["sha256"].isString())
        return false;
    result.relativePath = value["relative_path"].toString();
    result.sha256 = value["sha256"].toString();
    return sha256 (result.sha256)
        && result.relativePath == "plugin_data/reference/v2/ranges/" + result.sha256 + ".json"
        && exactInteger (value["bytes"], 1, maximumRangesBytes, result.bytes);
}

bool parseSongSet (const juce::var& value, int expectedRank, RuntimeSongSet& result)
{
    const auto* object = value.getDynamicObject();
    std::int64_t rank = 0;
    if (object == nullptr || ! exactProperties (*object, { "song_set_id", "revision_id", "rank", "name", "songs" })
        || ! value["song_set_id"].isString() || ! value["revision_id"].isString())
        return false;
    result.songSetId = value["song_set_id"].toString();
    result.revisionId = value["revision_id"].toString();
    if (! uuidV4 (result.songSetId) || ! uuidV4 (result.revisionId)
        || ! exactInteger (value["rank"], 1, maximumRankedSets, rank) || rank != expectedRank
        || ! setName (value["name"], result.name))
        return false;
    result.rank = static_cast<int> (rank);
    const auto* songs = value["songs"].getArray();
    if (songs == nullptr || songs->size() > maximumSongsPerSet) return false;
    std::set<juce::String> candidateIds;
    for (const auto& song : *songs)
    {
        RuntimeCandidate candidate;
        if (! parseLibraryVersionCandidate (song, candidate) || ! candidateIds.insert (candidate.candidateId).second)
            return false;
        result.songs.push_back (std::move (candidate));
    }
    return true;
}

bool parseCheckSet (const juce::var& value, int expectedRank, const RuntimeManifest& manifest,
                    RuntimeCheckSetRank& result)
{
    const auto* object = value.getDynamicObject();
    std::int64_t rank = 0;
    if (object == nullptr || ! exactProperties (*object, { "preset_id", "revision_id", "rank" })
        || ! value["preset_id"].isString() || ! value["revision_id"].isString()
        || ! exactInteger (value["rank"], 1, maximumRankedSets, rank) || rank != expectedRank)
        return false;
    result.presetId = value["preset_id"].toString();
    result.revisionId = value["revision_id"].toString();
    result.rank = static_cast<int> (rank);
    // 中身は同じ manifest の Preset を読む。manifest に無い Preset や別の revision は指せない。
    return std::any_of (manifest.presetArtifacts.begin(), manifest.presetArtifacts.end(), [&] (const auto& receipt) {
        return receipt.presetId == result.presetId && receipt.revisionId == result.revisionId;
    });
}
}

std::optional<RuntimeLibrarySets> readReferenceLibrarySets (const juce::File& root,
                                                            const RuntimeWorkspace& workspace,
                                                            juce::String& rejection)
{
    rejection = {};
    const auto file = root.getChildFile ("library/sets.json");
    if (! file.exists()) return std::nullopt;
    const auto reject = [&rejection] (const char* code) {
        rejection = code;
        return std::optional<RuntimeLibrarySets> {};
    };
    juce::MemoryBlock bytes;
    juce::var json;
    RuntimeLibrarySets sets;
    std::int64_t manifestRevision = 0;
    const auto* object = readJson (file, maximumSetsBytes, bytes, json) ? json.getDynamicObject() : nullptr;
    if (object == nullptr
        || ! exactProperties (*object, { "format", "version", "revision", "manifest_revision",
                                         "song_sets", "check_sets", "source_ranges" })
        || json["format"] != "kirin_hypha_reference_library_sets" || json["version"] != "1.0"
        || ! exactInteger (json["revision"], 1, maximumSafeInteger, sets.revision)
        || ! exactInteger (json["manifest_revision"], 1, maximumSafeInteger, manifestRevision))
        return reject ("reference_library_sets_rejected");
    // Kirin OS は manifest を先に書き、sets はその直後に続く。別の manifest の sets はまだ今のものではない。
    if (manifestRevision != workspace.manifest.revision) return reject ("reference_library_sets_stale");
    sets.hash = juce::SHA256 (bytes).toHexString();

    const auto* songSets = json["song_sets"].getArray();
    const auto* checkSets = json["check_sets"].getArray();
    const auto* sourceRanges = json["source_ranges"].getArray();
    if (songSets == nullptr || songSets->size() > maximumRankedSets
        || checkSets == nullptr || checkSets->size() > maximumRankedSets
        || sourceRanges == nullptr || sourceRanges->size() > maximumSourceRanges)
        return reject ("reference_library_sets_rejected");
    std::set<juce::String> setIds, presetIds, sources;
    for (int index = 0; index < songSets->size(); ++index)
    {
        RuntimeSongSet set;
        if (! parseSongSet (songSets->getReference (index), index + 1, set) || ! setIds.insert (set.songSetId).second)
            return reject ("reference_library_song_set_rejected");
        sets.songSets.push_back (std::move (set));
    }
    for (int index = 0; index < checkSets->size(); ++index)
    {
        RuntimeCheckSetRank rank;
        if (! parseCheckSet (checkSets->getReference (index), index + 1, workspace.manifest, rank)
            || ! presetIds.insert (rank.presetId).second)
            return reject ("reference_library_check_set_rejected");
        sets.checkSets.push_back (std::move (rank));
    }
    for (const auto& value : *sourceRanges)
    {
        const auto* item = value.getDynamicObject();
        RuntimeSourceRangesReceipt entry;
        if (item == nullptr || ! exactProperties (*item, { "source_artifact_sha256", "ranges_artifact" })
            || ! value["source_artifact_sha256"].isString())
            return reject ("reference_library_source_ranges_rejected");
        entry.sourceArtifactSha256 = value["source_artifact_sha256"].toString();
        if (! sha256 (entry.sourceArtifactSha256) || ! sources.insert (entry.sourceArtifactSha256).second
            || ! rangesReceipt (value["ranges_artifact"], entry.rangesArtifact))
            return reject ("reference_library_source_ranges_rejected");
        sets.sourceRanges.push_back (std::move (entry));
    }
    return sets;
}
}
