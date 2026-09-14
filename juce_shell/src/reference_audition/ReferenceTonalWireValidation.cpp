#include "ReferenceTonalWireValidation.h"
#include "ReferenceRuntimeRepositoryParsing.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace hypha::reference_audition
{
namespace
{
using namespace runtime_repository_parsing;
constexpr std::int64_t maximumPayloadBytes = 16 * 1024 * 1024;
constexpr double requestWindows[] { 2.0, 0.5, 0.2 };
constexpr double requestHops[] { 0.5, 0.2, 0.1 };
constexpr const char* planeNames[] { "low", "low_mid", "high" };

bool exactNumber (const juce::var& value, double& result)
{
    if (! value.isDouble() && ! value.isInt() && ! value.isInt64()) return false;
    result = static_cast<double> (value);
    return std::isfinite (result);
}

bool closeTo (double actual, double expected) noexcept
{
    return std::abs (actual - expected) <= std::max (1.0e-10, std::abs (expected) * 1.0e-9);
}

bool sameDouble (double actual, double expected) noexcept
{
    return std::memcmp (&actual, &expected, sizeof actual) == 0;
}

bool exactFinite (const juce::DynamicObject& object, const char* property, double expected)
{
    double actual = 0.0;
    return exactNumber (object.getProperty (property), actual) && sameDouble (actual, expected);
}

bool decodeBase64 (const juce::var& value, const char* encoding, size_t expectedBytes,
                   juce::MemoryBlock& result)
{
    const auto* object = value.getDynamicObject();
    std::int64_t declared = 0;
    if (object == nullptr || ! exactProperties (*object, { "encoding", "byte_count", "data" })
        || object->getProperty ("encoding") != encoding
        || ! exactInteger (object->getProperty ("byte_count"), 0, maximumPayloadBytes, declared)
        || static_cast<size_t> (declared) != expectedBytes
        || ! object->getProperty ("data").isString()) return false;
    const auto encoded = object->getProperty ("data").toString();
    bool decoded = false;
    {
        juce::MemoryOutputStream output (result, false);
        decoded = juce::Base64::convertFromBase64 (output, encoded);
    }
    const auto canonical = juce::Base64::toBase64 (result.getData(), result.getSize());
    return decoded && result.getSize() == expectedBytes && canonical == encoded;
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

bool validatePlane (const juce::var& value, int planeIndex,
                    const std::vector<int>& expectedBands, std::int64_t rate,
                    std::int64_t sampleCount, std::int64_t rangeStart,
                    std::int64_t rangeEnd, std::int64_t& payloadBytes,
                    ReferenceTonalCurve& output)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr || ! exactProperties (*object, { "name", "window_request_sec", "window_sec",
            "hop_sec", "fft_size", "bin_hz", "band_indices", "frame_times_sec", "cell_count",
            "valid_count", "values", "validity" })
        || object->getProperty ("name") != planeNames[planeIndex]) return false;

    std::int64_t fftSize = 0, cellCount = 0, declaredValid = 0;
    double windowRequest = 0.0, window = 0.0, hop = 0.0, bin = 0.0;
    const auto* bandValues = object->getProperty ("band_indices").getArray();
    const auto* frameTimes = object->getProperty ("frame_times_sec").getArray();
    if (! exactInteger (object->getProperty ("fft_size"), 256, 262144, fftSize)
        || (fftSize & (fftSize - 1)) != 0
        || ! exactInteger (object->getProperty ("cell_count"), 0, 4'000'000, cellCount)
        || ! exactInteger (object->getProperty ("valid_count"), 0, cellCount, declaredValid)
        || ! exactNumber (object->getProperty ("window_request_sec"), windowRequest)
        || ! exactNumber (object->getProperty ("window_sec"), window)
        || ! exactNumber (object->getProperty ("hop_sec"), hop)
        || ! exactNumber (object->getProperty ("bin_hz"), bin)
        || bandValues == nullptr || frameTimes == nullptr
        || bandValues->size() != static_cast<int> (expectedBands.size())) return false;

    const auto hopSamples = std::max<std::int64_t> (
        1, static_cast<std::int64_t> (std::floor (requestHops[planeIndex] * rate + 0.5)));
    const auto expectedFrames = sampleCount < fftSize
        ? 0 : (sampleCount - fftSize) / hopSamples + 1;
    if (expectedFrames > std::numeric_limits<int>::max()
        || frameTimes->size() != expectedFrames
        || cellCount != expectedFrames * static_cast<std::int64_t> (expectedBands.size())
        || ! sameDouble (windowRequest, requestWindows[planeIndex])
        || ! closeTo (hop, static_cast<double> (hopSamples) / rate)
        || ! closeTo (window, static_cast<double> (fftSize) / rate)
        || ! closeTo (bin, static_cast<double> (rate) / fftSize)) return false;

    for (int index = 0; index < bandValues->size(); ++index)
    {
        std::int64_t band = 0;
        if (! exactInteger ((*bandValues)[index], 0, 59, band)
            || band != expectedBands[static_cast<size_t> (index)]) return false;
    }

    const auto valueBytes = static_cast<size_t> (cellCount) * 4;
    const auto validityBytes = static_cast<size_t> ((cellCount + 7) / 8);
    juce::MemoryBlock values, validity;
    if (! decodeBase64 (object->getProperty ("values"), "float32le-base64", valueBytes, values)
        || ! decodeBase64 (object->getProperty ("validity"), "bitset-lsb0-base64", validityBytes, validity)
        || valueBytes + validityBytes > static_cast<size_t> (maximumPayloadBytes - payloadBytes)) return false;
    payloadBytes += static_cast<std::int64_t> (valueBytes + validityBytes);

    const auto* rawValues = static_cast<const std::uint8_t*> (values.getData());
    const auto* rawValidity = static_cast<const std::uint8_t*> (validity.getData());
    for (std::int64_t cell = cellCount; cell < static_cast<std::int64_t> (validityBytes * 8); ++cell)
        if ((rawValidity[cell / 8] & (std::uint8_t (1) << (cell % 8))) != 0) return false;

    std::vector<std::vector<double>> powers (expectedBands.size());
    std::int64_t actualValid = 0;
    for (int frame = 0; frame < frameTimes->size(); ++frame)
    {
        const auto frameStart = static_cast<std::int64_t> (frame) * hopSamples;
        const auto centerSample = frameStart + fftSize / 2;
        double centerSeconds = 0.0;
        if (! exactNumber ((*frameTimes)[frame], centerSeconds)
            || ! closeTo (centerSeconds, static_cast<double> (centerSample) / rate)) return false;
        const bool inside = frameStart >= rangeStart && frameStart + fftSize <= rangeEnd;
        for (size_t local = 0; local < expectedBands.size(); ++local)
        {
            const auto cell = static_cast<size_t> (frame) * expectedBands.size() + local;
            const bool valid = (rawValidity[cell / 8] & (std::uint8_t (1) << (cell % 8))) != 0;
            if (! valid)
            {
                if (rawValues[cell * 4] || rawValues[cell * 4 + 1]
                    || rawValues[cell * 4 + 2] || rawValues[cell * 4 + 3]) return false;
                continue;
            }
            ++actualValid;
            const std::uint32_t word = std::uint32_t (rawValues[cell * 4])
                | std::uint32_t (rawValues[cell * 4 + 1]) << 8
                | std::uint32_t (rawValues[cell * 4 + 2]) << 16
                | std::uint32_t (rawValues[cell * 4 + 3]) << 24;
            float db = 0.0f;
            std::memcpy (&db, &word, sizeof db);
            if (! std::isfinite (db) || db < -120.0f || db > 0.001f) return false;
            if (inside) powers[local].push_back (std::pow (10.0, static_cast<double> (db) / 10.0));
        }
    }
    if (actualValid != declaredValid) return false;
    for (size_t local = 0; local < expectedBands.size(); ++local)
        if (! powers[local].empty())
        {
            const auto band = static_cast<size_t> (expectedBands[local]);
            output.p10[band] = percentileDb (powers[local], 0.10);
            output.median[band] = percentileDb (powers[local], 0.50);
            output.p90[band] = percentileDb (powers[local], 0.90);
            output.validBits |= std::uint64_t (1) << band;
        }
    return true;
}
}

std::shared_ptr<const ReferenceTonalCurve> validateReferenceTonalArtifact (
    const juce::var& json, const RuntimeSource& source, std::int64_t rangeStart,
    std::int64_t rangeEnd, const juce::String& artifactSha256)
{
    const auto* object = json.getDynamicObject();
    const auto* identity = object == nullptr
        ? nullptr : object->getProperty ("source_content").getDynamicObject();
    const auto* tonal = object == nullptr ? nullptr : object->getProperty ("tonal").getDynamicObject();
    if (object == nullptr || ! exactProperties (*object, { "format", "version", "source_content", "tonal" })
        || object->getProperty ("format") != "kirin_hypha_reference_tonal"
        || object->getProperty ("version") != "1.0"
        || identity == nullptr || ! exactProperties (*identity, { "sha256_file", "sha256_pcm" })
        || identity->getProperty ("sha256_file") != source.sourceFileSha256
        || identity->getProperty ("sha256_pcm") != source.sourcePcmSha256
        || tonal == nullptr || ! exactProperties (*tonal, { "schema_version", "method_version",
            "value_unit", "value_definition", "channel_aggregation", "window_type",
            "frequency_scale", "missing_policy", "silence_gate_dbfs", "band_power_floor_db",
            "source", "payload_bytes", "bands", "planes" })
        || tonal->getProperty ("schema_version") != "kirin_spectral_balance.v1"
        || tonal->getProperty ("method_version") != "kirin_multires_relative_power.v1"
        || tonal->getProperty ("value_unit") != "dB"
        || tonal->getProperty ("value_definition") != "10log10(band_power/same_aperture_20Hz_20kHz_power)"
        || tonal->getProperty ("channel_aggregation") != "channel_power_mean"
        || tonal->getProperty ("window_type") != "hann"
        || tonal->getProperty ("frequency_scale") != "log_20Hz_20kHz_60_band"
        || tonal->getProperty ("missing_policy") != "validity_bitmap_no_interpolation"
        || ! exactFinite (*tonal, "silence_gate_dbfs", -100.0)
        || ! exactFinite (*tonal, "band_power_floor_db", -120.0)) return {};

    const auto* tonalSource = tonal->getProperty ("source").getDynamicObject();
    const auto* bands = tonal->getProperty ("bands").getArray();
    const auto* planes = tonal->getProperty ("planes").getArray();
    std::int64_t rate = 0, channels = 0, samples = 0, declaredPayload = 0;
    double duration = 0.0;
    if (tonalSource == nullptr || ! exactProperties (*tonalSource, {
            "sample_rate", "channels", "sample_count", "duration_sec", "measurement_path" })
        || ! exactInteger (tonalSource->getProperty ("sample_rate"), 8000, 768000, rate)
        || ! exactInteger (tonalSource->getProperty ("channels"), 1, 2, channels)
        || ! exactInteger (tonalSource->getProperty ("sample_count"), 1, 9'007'199'254'740'991LL, samples)
        || ! exactNumber (tonalSource->getProperty ("duration_sec"), duration) || duration <= 0.0
        || ! tonalSource->getProperty ("measurement_path").isString()
        || tonalSource->getProperty ("measurement_path").toString().trim().isEmpty()
        || ! closeTo (duration, static_cast<double> (samples) / rate)
        || rate != source.audio.sampleRateHz || channels != source.audio.channels
        || samples != source.audio.totalSampleFrames
        || ! exactInteger (tonal->getProperty ("payload_bytes"), 0, maximumPayloadBytes, declaredPayload)
        || bands == nullptr || bands->size() != 60 || planes == nullptr || planes->size() != 3) return {};

    auto result = std::make_shared<ReferenceTonalCurve>();
    result->sourceFileSha256 = source.sourceFileSha256;
    result->sourcePcmSha256 = source.sourcePcmSha256;
    result->artifactSha256 = artifactSha256;
    result->rangeStartSample = rangeStart;
    result->rangeEndSample = rangeEnd;
    std::array<std::vector<int>, 3> planeBands;
    const auto ratio = std::pow (1000.0, 1.0 / 60.0);
    for (int index = 0; index < 60; ++index)
    {
        const auto* band = (*bands)[index].getDynamicObject();
        std::int64_t declaredIndex = -1, planeIndex = -1;
        double lower = 0.0, upper = 0.0, center = 0.0;
        const auto expectedLower = 20.0 * std::pow (ratio, index);
        const auto expectedUpper = index == 59 ? 20000.0 : 20.0 * std::pow (ratio, index + 1);
        const auto expectedCenter = std::sqrt (expectedLower * expectedUpper);
        const auto expectedPlane = expectedCenter < 160.0 ? 0 : expectedCenter < 640.0 ? 1 : 2;
        if (band == nullptr || ! exactProperties (*band, {
                "index", "lower_hz", "upper_hz", "center_hz", "plane_index" })
            || ! exactInteger (band->getProperty ("index"), index, index, declaredIndex)
            || ! exactInteger (band->getProperty ("plane_index"), 0, 2, planeIndex)
            || ! exactNumber (band->getProperty ("lower_hz"), lower)
            || ! exactNumber (band->getProperty ("upper_hz"), upper)
            || ! exactNumber (band->getProperty ("center_hz"), center)
            || planeIndex != expectedPlane || ! closeTo (lower, expectedLower)
            || ! closeTo (upper, expectedUpper) || ! closeTo (center, expectedCenter)
            || ! (lower < center && center < upper)) return {};
        result->centersHz[static_cast<size_t> (index)] = center;
        planeBands[static_cast<size_t> (planeIndex)].push_back (index);
    }

    std::int64_t actualPayload = 0;
    for (int plane = 0; plane < 3; ++plane)
        if (! validatePlane ((*planes)[plane], plane, planeBands[static_cast<size_t> (plane)],
                            rate, samples, rangeStart, rangeEnd, actualPayload, *result)) return {};
    if (actualPayload != declaredPayload) return {};
    return std::shared_ptr<const ReferenceTonalCurve> (std::move (result));
}
}
