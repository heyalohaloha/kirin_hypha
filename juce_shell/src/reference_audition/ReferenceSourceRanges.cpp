#include "ReferenceSourceRanges.h"
#include "ReferenceRuntimeRepositoryParsing.h"

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
using namespace runtime_repository_parsing;

namespace
{
constexpr std::int64_t maximumRangesBytes = 1024 * 1024;
constexpr std::int64_t maximumSafeInteger = 9'007'199'254'740'991;
constexpr int maximumRanges = 256;
constexpr int maximumSectionFamilies = 16;

bool optionalInteger (const juce::var& value, std::int64_t minimum, std::int64_t maximum,
                      std::optional<std::int64_t>& result)
{
    if (value.isVoid()) { result.reset(); return true; }
    std::int64_t parsed = 0;
    if (! exactInteger (value, minimum, maximum, parsed)) return false;
    result = parsed;
    return true;
}

bool integerSeries (const juce::var& value, std::size_t expectedSize, std::vector<std::int64_t>& result)
{
    const auto* array = value.getArray();
    if (array == nullptr || static_cast<std::size_t> (array->size()) != expectedSize) return false;
    for (const auto& item : *array)
    {
        std::int64_t parsed = 0;
        if (! exactInteger (item, -300'000, 24'000, parsed)) return false;
        result.push_back (parsed);
    }
    return true;
}

bool parseRange (const juce::var& value, const RuntimeSourceRanges& source, RuntimeRangeValues& result)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr || ! exactProperties (*object, { "start_sample", "end_sample", "lufs_i_millilu",
            "max_true_peak_millidbtp", "spectrum", "balance_millidbfs" })
        || ! exactInteger (value["start_sample"], 0, maximumSafeInteger, result.startSample)
        || ! exactInteger (value["end_sample"], 1, maximumSafeInteger, result.endSample)
        || result.endSample <= result.startSample || result.endSample > source.totalSampleFrames
        || ! optionalInteger (value["lufs_i_millilu"], -70'000, 24'000, result.lufsIMilliLu)
        || ! optionalInteger (value["max_true_peak_millidbtp"], -300'000, 24'000, result.maxTruePeakMilliDbtp))
        return false;
    if (! value["balance_millidbfs"].isVoid())
    {
        std::vector<std::int64_t> balance;
        if (! integerSeries (value["balance_millidbfs"], 4, balance)) return false;
        result.balanceMilliDbfs = std::array<std::int64_t, 4> { balance[0], balance[1], balance[2], balance[3] };
    }
    const auto spectrum = value["spectrum"];
    if (spectrum.isVoid()) return true;
    const auto* spectrumObject = spectrum.getDynamicObject();
    const auto bands = source.spectrumBandCentersHz.size();
    if (spectrumObject == nullptr || bands == 0
        || ! exactProperties (*spectrumObject, { "frame_count", "p10_millidbfs", "median_millidbfs", "p90_millidbfs" })
        || ! exactInteger (spectrum["frame_count"], 1, maximumSafeInteger, result.spectrumFrameCount)
        || ! integerSeries (spectrum["p10_millidbfs"], bands, result.spectrumP10MilliDbfs)
        || ! integerSeries (spectrum["median_millidbfs"], bands, result.spectrumMedianMilliDbfs)
        || ! integerSeries (spectrum["p90_millidbfs"], bands, result.spectrumP90MilliDbfs))
        return false;
    for (std::size_t band = 0; band < bands; ++band)
        if (result.spectrumP10MilliDbfs[band] > result.spectrumMedianMilliDbfs[band]
            || result.spectrumMedianMilliDbfs[band] > result.spectrumP90MilliDbfs[band])
            return false;
    return true;
}

bool reasonOf (const juce::var& value, std::initializer_list<const char*> allowed, juce::String& result)
{
    if (value.isVoid()) { result = {}; return true; }
    if (! value.isString()) return false;
    result = value.toString();
    for (const auto* item : allowed)
        if (result == item) return true;
    return false;
}

bool parseSections (const juce::var& value, RuntimeSourceRanges& result)
{
    if (value.isVoid()) return true;
    const auto* object = value.getDynamicObject();
    if (object == nullptr || ! exactProperties (*object, { "method", "status", "reason", "chorus_reason", "families" })
        || value["method"] != "kirin_self_similarity_v1"
        || ! reasonOf (value["status"], { "found", "not_found", "unavailable" }, result.sectionsStatus)
        || result.sectionsStatus.isEmpty()
        || ! reasonOf (value["reason"], { "no_repetition", "source_too_short", "source_too_long", "analysis_unavailable" },
                       result.sectionsReason)
        || ! reasonOf (value["chorus_reason"], { "no_repetition", "source_too_short", "source_too_long",
                                                 "analysis_unavailable", "weak_repetition" }, result.chorusReason))
        return false;
    const auto* families = value["families"].getArray();
    if (families == nullptr || families->size() > maximumSectionFamilies) return false;
    for (const auto& item : *families)
    {
        const auto* family = item.getDynamicObject();
        RuntimeSectionFamily parsed;
        std::int64_t representative = 0;
        if (family == nullptr || ! exactProperties (*family, { "chorus_candidate", "representative", "lift_millilu",
                "similarity_milli", "occurrences" })
            || ! item["chorus_candidate"].isBool()
            || ! exactInteger (item["representative"], 0, 15, representative)
            || ! exactInteger (item["lift_millilu"], -100'000, 100'000, parsed.liftMilliLu)
            || ! exactInteger (item["similarity_milli"], -1'000, 1'000, parsed.similarityMilli))
            return false;
        parsed.chorusCandidate = static_cast<bool> (item["chorus_candidate"]);
        parsed.representative = static_cast<int> (representative);
        const auto* occurrences = item["occurrences"].getArray();
        if (occurrences == nullptr || occurrences->size() < 2 || occurrences->size() > 16
            || parsed.representative >= occurrences->size())
            return false;
        for (const auto& occurrence : *occurrences)
        {
            const auto* range = occurrence.getDynamicObject();
            std::int64_t start = 0, end = 0;
            if (range == nullptr || ! exactProperties (*range, { "start_sample", "end_sample" })
                || ! exactInteger (occurrence["start_sample"], 0, maximumSafeInteger, start)
                || ! exactInteger (occurrence["end_sample"], 1, maximumSafeInteger, end)
                || end <= start || end > result.totalSampleFrames)
                return false;
            parsed.occurrences.emplace_back (start, end);
        }
        result.sectionFamilies.push_back (std::move (parsed));
    }
    result.hasSections = true;
    return true;
}

bool decodeBase64 (const juce::var& value, std::size_t expectedBytes, juce::MemoryBlock& result)
{
    if (! value.isString()) return false;
    juce::MemoryOutputStream decoded (result, false);
    return juce::Base64::convertFromBase64 (decoded, value.toString())
        && (decoded.flush(), result.getSize() == expectedBytes);
}

// 契約の定数（0.1 秒・帯域の境界）と JSON の数がそのまま同じか。
bool sameValue (double value, double expected) noexcept { return value >= expected && value <= expected; }

bool parseFingerprint (const juce::var& value, RuntimeSourceRanges& result)
{
    if (value.isVoid()) return true;
    const auto* object = value.getDynamicObject();
    if (object == nullptr || ! exactProperties (*object, { "method", "tick_seconds", "ticks", "chroma_signs_b64", "loudness_b64" })
        || value["method"] != "kirin_chroma_sign_v1" || ! value["tick_seconds"].isDouble()
        || ! sameValue (static_cast<double> (value["tick_seconds"]), 0.1)
        || ! exactInteger (value["ticks"], 1, maximumSafeInteger, result.fingerprintTicks))
        return false;
    const auto ticks = static_cast<std::size_t> (result.fingerprintTicks);
    return decodeBase64 (value["chroma_signs_b64"], ticks * 2, result.fingerprintChromaSigns)
        && decodeBase64 (value["loudness_b64"], ticks, result.fingerprintLoudness);
}

bool parseBandCenters (const juce::var& value, RuntimeSourceRanges& result)
{
    if (value.isVoid()) return true;
    const auto* array = value.getArray();
    if (array == nullptr || array->size() < 12 || array->size() > 256) return false;
    for (const auto& item : *array)
    {
        if (! (item.isDouble() || item.isInt() || item.isInt64())) return false;
        const auto hz = static_cast<double> (item);
        if (! (hz > 0.0 && hz <= 384'000.0)) return false;
        result.spectrumBandCentersHz.push_back (hz);
    }
    return true;
}
}

bool readReferenceSourceRanges (const juce::File& root, const RuntimeContentReceipt& receipt, RuntimeSourceRanges& result)
{
    result = {};
    if (! sha256 (receipt.sha256) || receipt.relativePath != "plugin_data/reference/v2/ranges/" + receipt.sha256 + ".json"
        || receipt.bytes < 1 || receipt.bytes > maximumRangesBytes)
        return false;
    juce::MemoryBlock bytes;
    juce::var json;
    if (! readJson (root.getChildFile ("ranges/" + receipt.sha256 + ".json"), maximumRangesBytes, bytes, json)
        || bytes.getSize() != static_cast<std::size_t> (receipt.bytes)
        || juce::SHA256 (bytes).toHexString() != receipt.sha256)
        return false;
    const auto* object = json.getDynamicObject();
    const auto* content = json["source_content"].getDynamicObject();
    const auto* audio = json["audio"].getDynamicObject();
    const auto* edges = json["balance_edges_hz"].getArray();
    if (object == nullptr || ! exactProperties (*object, { "format", "version", "loudness_standard", "source_content",
            "audio", "spectrum_band_centers_hz", "balance_edges_hz", "sections", "fingerprint", "ranges" })
        || json["format"] != "kirin_hypha_reference_ranges" || json["version"] != "1.3"
        || json["loudness_standard"] != "itu_r_bs_1770"
        || content == nullptr || ! exactProperties (*content, { "sha256_file", "sha256_pcm" })
        || audio == nullptr || ! exactProperties (*audio, { "sample_rate_hz", "channels", "total_sample_frames" })
        || ! exactInteger (json["audio"]["sample_rate_hz"], 8'000, 768'000, result.sampleRateHz)
        || ! exactInteger (json["audio"]["channels"], 1, 64, result.channels)
        || ! exactInteger (json["audio"]["total_sample_frames"], 1, maximumSafeInteger, result.totalSampleFrames)
        || edges == nullptr || edges->size() != 5)
        return false;
    result.sha256File = json["source_content"]["sha256_file"].toString();
    result.sha256Pcm = json["source_content"]["sha256_pcm"].toString();
    const double expectedEdges[] { 20.0, 250.0, 2'000.0, 8'000.0, 20'000.0 };
    for (int index = 0; index < 5; ++index)
        if (! sameValue (static_cast<double> (edges->getReference (index)), expectedEdges[index])) return false;
    if (! sha256 (result.sha256File) || ! sha256 (result.sha256Pcm)
        || ! parseBandCenters (json["spectrum_band_centers_hz"], result)
        || ! parseSections (json["sections"], result)
        || ! parseFingerprint (json["fingerprint"], result))
        return false;
    const auto* ranges = json["ranges"].getArray();
    if (ranges == nullptr || ranges->isEmpty() || ranges->size() > maximumRanges) return false;
    for (const auto& item : *ranges)
    {
        RuntimeRangeValues range;
        if (! parseRange (item, result, range) || result.find (range.startSample, range.endSample) != nullptr)
            return false;
        result.ranges.push_back (std::move (range));
    }
    return true;
}
}
