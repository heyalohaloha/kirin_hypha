#include "ReferenceTonalRepository.h"
#include "ReferenceRuntimeRepositoryParsing.h"
#include "ReferenceTonalWireValidation.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
namespace
{
using namespace runtime_repository_parsing;
constexpr std::int64_t artifactMaximumBytes = 16 * 1024 * 1024;

bool containedRegularFile (const juce::File& root, const juce::File& file)
{
    if (! file.isAChildOf (root) || ! file.existsAsFile() || file.isSymbolicLink()) return false;
    for (auto current = file.getParentDirectory(); current != root; current = current.getParentDirectory())
        if (! current.isAChildOf (root) || ! current.isDirectory() || current.isSymbolicLink()) return false;
    return root.isDirectory() && ! root.isSymbolicLink();
}

bool readVerifiedJson (const juce::File& root, const juce::File& file, std::int64_t maximum,
                       std::int64_t expectedBytes, const juce::String& expectedHash,
                       juce::var& json)
{
    if (! containedRegularFile (root, file) || file.getSize() < 1 || file.getSize() > maximum
        || (expectedBytes > 0 && file.getSize() != expectedBytes)) return false;
    juce::MemoryBlock bytes;
    if (! readJson (file, maximum, bytes, json)) return false;
    return expectedHash.isEmpty() || juce::SHA256 (bytes).toHexString() == expectedHash;
}

void requestArtifactRepair (const juce::File& root, const juce::String& kind,
                            const juce::String& sourceFileSha256,
                            const juce::String& artifactSha256)
{
    if (! sha256 (artifactSha256) || (kind == "source" && ! sha256 (sourceFileSha256))
        || (kind != "source" && kind != "genre")) return;
    const auto directory = root.getChildFile ("tonal-v1/repair-requests");
    if (! root.isDirectory() || root.isSymbolicLink() || ! directory.isAChildOf (root)) return;
    for (auto current = directory.getParentDirectory(); current != root;
         current = current.getParentDirectory())
        if (! current.isAChildOf (root) || ! current.isDirectory() || current.isSymbolicLink()) return;
    if (directory.isSymbolicLink()
        || (! directory.isDirectory() && ! directory.createDirectory().wasOk())) return;
    const auto target = directory.getChildFile (artifactSha256 + ".json");
    if (! target.isAChildOf (root) || target.isSymbolicLink()
        || (target.exists() && ! target.existsAsFile())) return;
    const auto now = juce::Time::currentTimeMillis();
    auto request = new juce::DynamicObject();
    request->setProperty ("format", "kirin_hypha_reference_tonal_repair_request");
    request->setProperty ("version", "1.0");
    request->setProperty ("kind", kind);
    request->setProperty ("source_file_sha256", kind == "source" ? sourceFileSha256 : juce::String {});
    request->setProperty ("artifact_sha256", artifactSha256);
    request->setProperty ("created_at_ms", now);
    request->setProperty ("expires_at_ms", now + 60'000);
    const auto body = juce::JSON::toString (juce::var (request), false);
    if (body.getNumBytesAsUTF8() > 4096) return;
    juce::TemporaryFile temporary (target);
    if (temporary.getFile().replaceWithText (body, false, false, "\n")
        && temporary.overwriteTargetFileWithTemporary()) return;
}

bool exactNumber (const juce::var& value, double& result)
{
    if (! value.isDouble() && ! value.isInt() && ! value.isInt64()) return false;
    result = static_cast<double> (value);
    return std::isfinite (result);
}

bool parseReceipt (const juce::var& value, const juce::String& directory,
                   juce::String& relative, juce::String& hash, std::int64_t& bytes)
{
    const auto* receipt = value.getDynamicObject();
    if (receipt == nullptr || ! exactProperties (*receipt, { "relative_path", "sha256", "bytes" })) return false;
    relative = receipt->getProperty ("relative_path").toString();
    hash = receipt->getProperty ("sha256").toString();
    return sha256 (hash) && relative == directory + hash + ".json"
        && exactInteger (receipt->getProperty ("bytes"), 1, artifactMaximumBytes, bytes);
}

bool readTonalManifest (const juce::File& root, juce::var& manifest, juce::MemoryBlock& bytes)
{
    const auto file = root.getChildFile ("tonal-v1/manifest.json");
    if (! containedRegularFile (root, file) || file.getSize() < 1 || file.getSize() > 256 * 1024
        || ! readJson (file, 256 * 1024, bytes, manifest)) return false;
    const auto* object = manifest.getDynamicObject();
    const auto* sources = manifest["sources"].getArray();
    const auto* checks = manifest["checks"].getArray();
    const auto* genre = manifest["genre_reference"].getDynamicObject();
    std::int64_t revision = 0;
    if (object == nullptr || ! exactProperties (*object, { "format", "version", "sources", "checks",
            "genre_reference", "revision" })
        || object->getProperty ("format") != "kirin_hypha_reference_tonal_manifest"
        || object->getProperty ("version") != "1.0"
        || ! exactInteger (object->getProperty ("revision"), 1, 9'007'199'254'740'991LL, revision)
        || sources == nullptr || sources->size() > 4096 || checks == nullptr || checks->size() > 512
        || genre == nullptr || ! exactProperties (*genre, { "artifact", "content_digest" })) return false;

    std::set<juce::String> sourceIdentities;
    for (const auto& value : *sources)
    {
        const auto* entry = value.getDynamicObject();
        const auto* identity = value["source_content"].getDynamicObject();
        juce::String relative, hash; std::int64_t artifactBytes = 0;
        if (entry == nullptr || ! exactProperties (*entry, { "source_content", "artifact" })
            || identity == nullptr || ! exactProperties (*identity, { "sha256_file", "sha256_pcm" })
            || ! sha256 (identity->getProperty ("sha256_file").toString())
            || ! sha256 (identity->getProperty ("sha256_pcm").toString())
            || ! parseReceipt (entry->getProperty ("artifact"), "tonal-v1/artifacts/",
                               relative, hash, artifactBytes)
            || ! sourceIdentities.insert (identity->getProperty ("sha256_file").toString()).second) return false;
    }

    std::set<juce::String> checkIdentities;
    for (const auto& value : *checks)
    {
        const auto* check = value.getDynamicObject();
        if (check == nullptr || ! exactProperties (*check, { "preset_id", "preset_revision_id",
                "check_id", "enabled", "genre_id" })
            || ! uuidV4 (check->getProperty ("preset_id").toString())
            || ! uuidV4 (check->getProperty ("preset_revision_id").toString())
            || ! uuidV4 (check->getProperty ("check_id").toString())
            || ! check->getProperty ("enabled").isBool()) return false;
        const auto genreId = check->getProperty ("genre_id");
        if (! genreId.isVoid() && (! genreId.isString() || genreId.toString().trim().isEmpty()
            || genreId.toString().length() > 64 || genreId.toString().trim() != genreId.toString())) return false;
        const auto identity = check->getProperty ("preset_id").toString() + ":"
            + check->getProperty ("preset_revision_id").toString() + ":"
            + check->getProperty ("check_id").toString();
        if (! checkIdentities.insert (identity).second) return false;
    }

    juce::String relative, hash; std::int64_t artifactBytes = 0;
    return sha256 (genre->getProperty ("content_digest").toString())
        && parseReceipt (genre->getProperty ("artifact"), "tonal-v1/genres/",
                         relative, hash, artifactBytes);
}

std::shared_ptr<const ReferenceTonalCurve> parseGenreArtifact (
    const juce::var& json, const juce::String& genreId,
    const juce::String& contentDigest, const juce::String& artifactHash)
{
    const auto* object = json.getDynamicObject();
    const auto bundleValue = object == nullptr ? juce::var {} : object->getProperty ("bundle");
    const auto* bundle = bundleValue.getDynamicObject();
    if (object == nullptr || ! exactProperties (*object, { "format", "version", "bundle" })
        || object->getProperty ("format") != "kirin_hypha_reference_genres"
        || object->getProperty ("version") != "1.0" || bundle == nullptr
        || ! exactProperties (*bundle, { "band_count", "band_edges", "content_digest",
            "frequency_scale", "groups", "method_version", "profiles", "schema_version" })
        || bundle->getProperty ("schema_version") != "kirin_spectral_balance_genre_reference_runtime.v1"
        || bundle->getProperty ("method_version") != "kirin_multires_relative_power.v1"
        || bundle->getProperty ("frequency_scale") != "log_20Hz_20kHz_60_band"
        || bundle->getProperty ("content_digest") != contentDigest) return {};
    std::int64_t bandCount = 0;
    const auto* edges = bundle->getProperty ("band_edges").getArray();
    const auto* groups = bundle->getProperty ("groups").getArray();
    const auto* profiles = bundle->getProperty ("profiles").getDynamicObject();
    if (! exactInteger (bundle->getProperty ("band_count"), 60, 60, bandCount)
        || edges == nullptr || edges->size() != 61 || groups == nullptr || groups->size() != 4
        || profiles == nullptr || profiles->getProperties().size() < 1
        || profiles->getProperties().size() > 32) return {};
    const auto profileValue = profiles->getProperty (genreId);
    const auto* profile = profileValue.getDynamicObject();
    if (profile == nullptr || ! exactProperties (*profile, { "display_label", "genre", "groups",
        "p10", "p50", "p90", "profile_version" })
        || profile->getProperty ("genre") != genreId || ! profile->getProperty ("display_label").isString()
        || profile->getProperty ("display_label").toString().trim().isEmpty()) return {};
    const auto* p10 = profile->getProperty ("p10").getArray();
    const auto* p50 = profile->getProperty ("p50").getArray();
    const auto* p90 = profile->getProperty ("p90").getArray();
    if (p10 == nullptr || p50 == nullptr || p90 == nullptr
        || p10->size() != 60 || p50->size() != 60 || p90->size() != 60) return {};
    auto result = std::make_shared<ReferenceTonalCurve>();
    result->genreId = genreId;
    result->displayLabel = profile->getProperty ("display_label").toString();
    result->artifactSha256 = artifactHash;
    const auto ratio = std::pow (20000.0 / 20.0, 1.0 / 60.0);
    for (int index = 0; index < 60; ++index)
    {
        double low = 0.0, high = 0.0, lowDb = 0.0, centerDb = 0.0, highDb = 0.0;
        if (! exactNumber ((*edges)[index], low) || ! exactNumber ((*edges)[index + 1], high)
            || ! exactNumber ((*p10)[index], lowDb) || ! exactNumber ((*p50)[index], centerDb)
            || ! exactNumber ((*p90)[index], highDb) || low <= 0.0 || high <= low
            || lowDb < -120.0 || highDb > 0.001 || lowDb > centerDb || centerDb > highDb) return {};
        const auto expectedLow = 20.0 * std::pow (ratio, index);
        const auto expectedHigh = index == 59 ? 20000.0 : 20.0 * std::pow (ratio, index + 1);
        if (std::abs (low - expectedLow) > expectedLow * 1.0e-10
            || std::abs (high - expectedHigh) > expectedHigh * 1.0e-10) return {};
        result->centersHz[size_t (index)] = std::sqrt (low * high);
        result->p10[size_t (index)] = static_cast<float> (lowDb);
        result->median[size_t (index)] = static_cast<float> (centerDb);
        result->p90[size_t (index)] = static_cast<float> (highDb);
        result->validBits |= std::uint64_t (1) << index;
    }
    return result;
}
}

bool ReferenceTonalRepository::artifactRejected (const juce::String& hash) const
{
    return std::find (rejectedArtifacts.begin(), rejectedArtifacts.end(), hash)
        != rejectedArtifacts.end();
}

void ReferenceTonalRepository::rejectArtifact (const juce::String& hash) const
{
    if (artifactRejected (hash)) return;
    rejectedArtifacts.push_back (hash);
    if (rejectedArtifacts.size() > 32) rejectedArtifacts.pop_front();
}

std::shared_ptr<const ReferenceTonalCurve> ReferenceTonalRepository::load (
    const RuntimeSource& source, std::int64_t rangeStartSample, std::int64_t rangeEndSample,
    bool* retryable) const
{
    if (retryable != nullptr) *retryable = false;
    juce::var manifest; juce::MemoryBlock manifestBytes;
    if (! readTonalManifest (root, manifest, manifestBytes)) return {};
    const auto* entries = manifest["sources"].getArray();
    if (entries == nullptr || entries->size() > 4096) return {};
    for (const auto& entryValue : *entries)
    {
        const auto* entry = entryValue.getDynamicObject();
        const auto* identity = entryValue["source_content"].getDynamicObject();
        std::int64_t bytes = 0;
        juce::String relative, hash;
        if (entry == nullptr || ! exactProperties (*entry, { "source_content", "artifact" })
            || identity == nullptr
            || identity->getProperty ("sha256_file") != source.sourceFileSha256) continue;
        if (identity->getProperty ("sha256_pcm") != source.sourcePcmSha256
            || ! parseReceipt (entryValue["artifact"], "tonal-v1/artifacts/",
                               relative, hash, bytes)) return {};
        if (artifactRejected (hash)) return {};
        juce::var artifact;
        if (! readVerifiedJson (root, root.getChildFile (relative), artifactMaximumBytes, bytes, hash, artifact))
        {
            if (retryable != nullptr) *retryable = true;
            requestArtifactRepair (root, "source", source.sourceFileSha256, hash);
            return {};
        }
        const auto start = juce::jlimit<std::int64_t> (0, source.audio.totalSampleFrames, rangeStartSample);
        const auto requestedEnd = rangeEndSample > start ? rangeEndSample : source.audio.totalSampleFrames;
        const auto end = juce::jlimit<std::int64_t> (start, source.audio.totalSampleFrames, requestedEnd);
        auto result = validateReferenceTonalArtifact (artifact, source, start, end, hash);
        if (result == nullptr) rejectArtifact (hash);
        return result;
    }
    return {};
}

std::shared_ptr<const ReferenceTonalCurve> ReferenceTonalRepository::loadGenre (
    const juce::String& presetId, const juce::String& presetRevisionId,
    const juce::String& checkId, bool* retryable) const
{
    if (retryable != nullptr) *retryable = false;
    if (! uuidV4 (presetId) || ! uuidV4 (presetRevisionId) || ! uuidV4 (checkId)) return {};
    juce::var manifest; juce::MemoryBlock manifestBytes;
    if (! readTonalManifest (root, manifest, manifestBytes)) return {};
    const auto* checks = manifest["checks"].getArray();
    if (checks == nullptr || checks->size() > 512) return {};
    juce::String genreId;
    int matches = 0;
    for (const auto& value : *checks)
    {
        const auto* item = value.getDynamicObject();
        if (item == nullptr || ! exactProperties (*item, { "preset_id", "preset_revision_id",
            "check_id", "enabled", "genre_id" })) return {};
        const auto enabled = item->getProperty ("enabled");
        if (! enabled.isBool()) return {};
        if (item->getProperty ("preset_id") == presetId
            && item->getProperty ("preset_revision_id") == presetRevisionId
            && item->getProperty ("check_id") == checkId)
        {
            ++matches;
            if (! static_cast<bool> (enabled)) return {};
            if (! item->getProperty ("genre_id").isVoid())
                genreId = item->getProperty ("genre_id").toString();
        }
    }
    if (matches != 1 || genreId.isEmpty()) return {};
    const auto genreValue = manifest["genre_reference"];
    const auto* genre = genreValue.getDynamicObject();
    if (genre == nullptr || ! exactProperties (*genre, { "artifact", "content_digest" })) return {};
    const auto contentDigest = genre->getProperty ("content_digest").toString();
    juce::String relative, hash; std::int64_t bytes = 0;
    if (! sha256 (contentDigest) || ! parseReceipt (genre->getProperty ("artifact"),
        "tonal-v1/genres/", relative, hash, bytes)) return {};
    if (artifactRejected (hash)) return {};
    juce::var artifact;
    if (! readVerifiedJson (root, root.getChildFile (relative), artifactMaximumBytes, bytes, hash, artifact))
    {
        if (retryable != nullptr) *retryable = true;
        requestArtifactRepair (root, "genre", {}, hash);
        return {};
    }
    auto result = parseGenreArtifact (artifact, genreId, contentDigest, hash);
    if (result == nullptr) rejectArtifact (hash);
    return result;
}

juce::String ReferenceTonalRepository::publicationKey() const
{
    juce::MemoryBlock bytes; juce::var manifest;
    if (! readTonalManifest (root, manifest, bytes)) return {};
    return juce::SHA256 (bytes).toHexString();
}
}
