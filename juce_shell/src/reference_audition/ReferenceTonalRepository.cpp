#include "ReferenceTonalRepository.h"
#include "ReferenceRuntimeRepositoryParsing.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

bool exactNumber (const juce::var& value, double& result)
{
    if (! value.isDouble() && ! value.isInt() && ! value.isInt64()) return false;
    result = static_cast<double> (value);
    return std::isfinite (result);
}

bool decodeBase64 (const juce::var& value, const char* encoding, size_t bytes,
                   juce::MemoryBlock& result)
{
    const auto* object = value.getDynamicObject();
    std::int64_t declared = 0;
    if (object == nullptr || ! exactProperties (*object, { "encoding", "byte_count", "data" })
        || object->getProperty ("encoding") != encoding
        || ! exactInteger (object->getProperty ("byte_count"), 0, artifactMaximumBytes, declared)
        || static_cast<size_t> (declared) != bytes || ! object->getProperty ("data").isString()) return false;
    juce::MemoryOutputStream output (result, false);
    return juce::Base64::convertFromBase64 (output, object->getProperty ("data").toString())
        && result.getSize() == bytes;
}

float percentileDb (std::vector<double>& powers, double quantile)
{
    std::sort (powers.begin(), powers.end());
    const auto position = quantile * static_cast<double> (powers.size() - 1);
    const auto lower = static_cast<size_t> (std::floor (position));
    const auto upper = static_cast<size_t> (std::ceil (position));
    const auto fraction = position - static_cast<double> (lower);
    const auto power = powers[lower] + (powers[upper] - powers[lower]) * fraction;
    return static_cast<float> (10.0 * std::log10 (power));
}

bool parsePlane (const juce::var& value, int planeIndex, const std::vector<int>& expectedBands,
                 std::int64_t rate, std::int64_t start, std::int64_t end,
                 ReferenceTonalCurve& output)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr || ! exactProperties (*object, { "name", "window_request_sec", "window_sec",
            "hop_sec", "fft_size", "bin_hz", "band_indices", "frame_times_sec", "cell_count",
            "valid_count", "values", "validity" })) return false;
    const char* names[] = { "low", "low_mid", "high" };
    if (object->getProperty ("name") != names[planeIndex]) return false;
    std::int64_t fftSize = 0, cellCount = 0, validCount = 0;
    const auto* bandValues = object->getProperty ("band_indices").getArray();
    const auto* frameTimes = object->getProperty ("frame_times_sec").getArray();
    if (! exactInteger (object->getProperty ("fft_size"), 256, 262144, fftSize)
        || ! exactInteger (object->getProperty ("cell_count"), 0, 4'000'000, cellCount)
        || ! exactInteger (object->getProperty ("valid_count"), 0, cellCount, validCount)
        || bandValues == nullptr || frameTimes == nullptr
        || bandValues->size() != static_cast<int> (expectedBands.size())
        || cellCount != static_cast<std::int64_t> (expectedBands.size()) * frameTimes->size()) return false;
    for (int index = 0; index < bandValues->size(); ++index)
    {
        std::int64_t band = 0;
        if (! exactInteger ((*bandValues)[index], 0, 59, band) || band != expectedBands[size_t (index)]) return false;
    }
    juce::MemoryBlock values, validity;
    if (! decodeBase64 (object->getProperty ("values"), "float32le-base64", size_t (cellCount) * 4, values)
        || ! decodeBase64 (object->getProperty ("validity"), "bitset-lsb0-base64", size_t ((cellCount + 7) / 8), validity)) return false;
    const auto* rawValues = static_cast<const std::uint8_t*> (values.getData());
    const auto* rawValidity = static_cast<const std::uint8_t*> (validity.getData());
    std::vector<std::vector<double>> powers (expectedBands.size());
    std::int64_t actualValid = 0;
    for (int frame = 0; frame < frameTimes->size(); ++frame)
    {
        double centerSeconds = 0.0;
        if (! exactNumber ((*frameTimes)[frame], centerSeconds) || centerSeconds < 0.0) return false;
        const auto center = static_cast<std::int64_t> (std::llround (centerSeconds * rate));
        const bool inside = center - fftSize / 2 >= start && center + fftSize / 2 <= end;
        for (size_t local = 0; local < expectedBands.size(); ++local)
        {
            const auto cell = size_t (frame) * expectedBands.size() + local;
            const bool valid = (rawValidity[cell / 8] & (std::uint8_t (1) << (cell % 8))) != 0;
            if (! valid)
            {
                if (rawValues[cell * 4] || rawValues[cell * 4 + 1] || rawValues[cell * 4 + 2] || rawValues[cell * 4 + 3]) return false;
                continue;
            }
            ++actualValid;
            std::uint32_t word = std::uint32_t (rawValues[cell * 4])
                | std::uint32_t (rawValues[cell * 4 + 1]) << 8
                | std::uint32_t (rawValues[cell * 4 + 2]) << 16
                | std::uint32_t (rawValues[cell * 4 + 3]) << 24;
            float db = 0.0f; std::memcpy (&db, &word, sizeof db);
            if (! std::isfinite (db) || db < -120.0f || db > 0.001f) return false;
            if (inside) powers[local].push_back (std::pow (10.0, static_cast<double> (db) / 10.0));
        }
    }
    if (actualValid != validCount) return false;
    for (size_t local = 0; local < expectedBands.size(); ++local)
        if (! powers[local].empty())
        {
            const auto band = expectedBands[local];
            output.p10[size_t (band)] = percentileDb (powers[local], 0.10);
            output.median[size_t (band)] = percentileDb (powers[local], 0.50);
            output.p90[size_t (band)] = percentileDb (powers[local], 0.90);
            output.validBits |= std::uint64_t (1) << band;
        }
    return true;
}

std::shared_ptr<const ReferenceTonalCurve> parseArtifact (
    const juce::var& json, const RuntimeSource& source, std::int64_t start, std::int64_t end,
    const juce::String& artifactHash)
{
    const auto* object = json.getDynamicObject();
    const auto tonalValue = object == nullptr ? juce::var {} : object->getProperty ("tonal");
    const auto* tonal = tonalValue.getDynamicObject();
    const auto sourceValue = object == nullptr ? juce::var {} : object->getProperty ("source_content");
    const auto* identity = sourceValue.getDynamicObject();
    if (object == nullptr || ! exactProperties (*object, { "format", "version", "source_content", "tonal" })
        || object->getProperty ("format") != "kirin_hypha_reference_tonal" || object->getProperty ("version") != "1.0"
        || identity == nullptr || ! exactProperties (*identity, { "sha256_file", "sha256_pcm" })
        || identity->getProperty ("sha256_file") != source.sourceFileSha256
        || identity->getProperty ("sha256_pcm") != source.sourcePcmSha256
        || tonal == nullptr || tonal->getProperty ("schema_version") != "kirin_spectral_balance.v1"
        || tonal->getProperty ("method_version") != "kirin_multires_relative_power.v1"
        || tonal->getProperty ("value_definition") != "10log10(band_power/same_aperture_20Hz_20kHz_power)"
        || tonal->getProperty ("channel_aggregation") != "channel_power_mean"
        || tonal->getProperty ("window_type") != "hann"
        || tonal->getProperty ("frequency_scale") != "log_20Hz_20kHz_60_band"
        || tonal->getProperty ("missing_policy") != "validity_bitmap_no_interpolation") return {};
    const auto* bands = tonal->getProperty ("bands").getArray();
    const auto* planes = tonal->getProperty ("planes").getArray();
    const auto* tonalSource = tonal->getProperty ("source").getDynamicObject();
    std::int64_t rate = 0, channels = 0, samples = 0;
    if (bands == nullptr || bands->size() != 60 || planes == nullptr || planes->size() != 3
        || tonalSource == nullptr
        || ! exactInteger (tonalSource->getProperty ("sample_rate"), 8000, 768000, rate)
        || ! exactInteger (tonalSource->getProperty ("channels"), 1, 2, channels)
        || ! exactInteger (tonalSource->getProperty ("sample_count"), 1, 9'007'199'254'740'991, samples)
        || rate != source.audio.sampleRateHz || channels != source.audio.channels
        || samples != source.audio.totalSampleFrames) return {};
    auto result = std::make_shared<ReferenceTonalCurve>();
    result->sourceFileSha256 = source.sourceFileSha256;
    result->sourcePcmSha256 = source.sourcePcmSha256;
    result->artifactSha256 = artifactHash;
    result->rangeStartSample = start;
    result->rangeEndSample = end;
    std::array<std::vector<int>, 3> planeBands;
    const auto ratio = std::pow (20000.0 / 20.0, 1.0 / 60.0);
    for (int index = 0; index < 60; ++index)
    {
        const auto* band = (*bands)[index].getDynamicObject();
        double center = 0.0; std::int64_t plane = -1, declared = -1;
        if (band == nullptr || ! exactInteger (band->getProperty ("index"), index, index, declared)
            || ! exactInteger (band->getProperty ("plane_index"), 0, 2, plane)
            || ! exactNumber (band->getProperty ("center_hz"), center)) return {};
        const auto expectedCenter = 20.0 * std::pow (ratio, index + 0.5);
        const auto expectedPlane = expectedCenter < 160.0 ? 0 : expectedCenter < 640.0 ? 1 : 2;
        if (plane != expectedPlane || std::abs (center - expectedCenter) > expectedCenter * 1.0e-8) return {};
        result->centersHz[size_t (index)] = center;
        planeBands[size_t (plane)].push_back (index);
    }
    for (int plane = 0; plane < 3; ++plane)
        if (! parsePlane ((*planes)[plane], plane, planeBands[size_t (plane)], rate, start, end, *result)) return {};
    return result->validBits ? std::shared_ptr<const ReferenceTonalCurve> (std::move (result)) : nullptr;
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

std::shared_ptr<const ReferenceTonalCurve> ReferenceTonalRepository::load (
    const RuntimeSource& source, std::int64_t rangeStartSample, std::int64_t rangeEndSample) const
{
    juce::var manifest;
    if (! readVerifiedJson (root, root.getChildFile ("tonal-v1/manifest.json"), 256 * 1024, 0, {}, manifest)
        || manifest["format"] != "kirin_hypha_reference_tonal_manifest" || manifest["version"] != "1.0") return {};
    const auto* entries = manifest["sources"].getArray();
    if (entries == nullptr || entries->size() > 4096) return {};
    for (const auto& entryValue : *entries)
    {
        const auto* entry = entryValue.getDynamicObject();
        const auto* identity = entryValue["source_content"].getDynamicObject();
        const auto* receipt = entryValue["artifact"].getDynamicObject();
        std::int64_t bytes = 0;
        const auto hash = entryValue["artifact"]["sha256"].toString();
        const auto relative = entryValue["artifact"]["relative_path"].toString();
        if (entry == nullptr || ! exactProperties (*entry, { "source_content", "artifact" })
            || identity == nullptr || receipt == nullptr
            || identity->getProperty ("sha256_file") != source.sourceFileSha256) continue;
        if (identity->getProperty ("sha256_pcm") != source.sourcePcmSha256
            || ! sha256 (hash) || relative != "tonal-v1/artifacts/" + hash + ".json"
            || ! exactInteger (receipt->getProperty ("bytes"), 1, artifactMaximumBytes, bytes)) return {};
        juce::var artifact;
        if (! readVerifiedJson (root, root.getChildFile (relative), artifactMaximumBytes, bytes, hash, artifact)) return {};
        const auto start = juce::jlimit<std::int64_t> (0, source.audio.totalSampleFrames, rangeStartSample);
        const auto requestedEnd = rangeEndSample > start ? rangeEndSample : source.audio.totalSampleFrames;
        const auto end = juce::jlimit<std::int64_t> (start, source.audio.totalSampleFrames, requestedEnd);
        return parseArtifact (artifact, source, start, end, hash);
    }
    return {};
}

std::shared_ptr<const ReferenceTonalCurve> ReferenceTonalRepository::loadGenre (
    const juce::String& presetId, const juce::String& presetRevisionId,
    const juce::String& checkId) const
{
    if (! uuidV4 (presetId) || ! uuidV4 (presetRevisionId) || ! uuidV4 (checkId)) return {};
    juce::var manifest;
    if (! readVerifiedJson (root, root.getChildFile ("tonal-v1/manifest.json"), 256 * 1024, 0, {}, manifest)
        || manifest["format"] != "kirin_hypha_reference_tonal_manifest" || manifest["version"] != "1.0") return {};
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
    juce::var artifact;
    if (! readVerifiedJson (root, root.getChildFile (relative), artifactMaximumBytes, bytes, hash, artifact)) return {};
    return parseGenreArtifact (artifact, genreId, contentDigest, hash);
}

juce::String ReferenceTonalRepository::publicationKey() const
{
    const auto file = root.getChildFile ("tonal-v1/manifest.json");
    if (! containedRegularFile (root, file) || file.getSize() < 1 || file.getSize() > 256 * 1024)
        return {};
    juce::MemoryBlock bytes; juce::var manifest;
    if (! readJson (file, 256 * 1024, bytes, manifest)) return {};
    const auto* object = manifest.getDynamicObject(); std::int64_t revision = 0;
    if (object == nullptr || ! exactProperties (*object, { "format", "version", "sources", "checks",
            "genre_reference", "revision" })
        || object->getProperty ("format") != "kirin_hypha_reference_tonal_manifest"
        || object->getProperty ("version") != "1.0"
        || ! exactInteger (object->getProperty ("revision"), 1, 9'007'199'254'740'991, revision))
        return {};
    return juce::SHA256 (bytes).toHexString();
}
}
