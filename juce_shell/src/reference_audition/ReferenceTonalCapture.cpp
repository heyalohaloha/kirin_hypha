#include "ReferenceTonalCapture.h"
#include "ReferenceCaptureTonalStore.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hypha::reference_audition
{
namespace
{
int planeForBand (int band)
{
    const auto center = 20.0 * std::pow (1000.0, (static_cast<double> (band) + 0.5) / 60.0);
    return center < 160.0 ? 0 : center < 640.0 ? 1 : 2;
}

float percentileDb (const std::vector<float>& source, double quantile)
{
    std::vector<double> power;
    power.reserve (source.size());
    for (const auto db : source)
        if (std::isfinite (db))
            power.push_back (std::pow (10.0, static_cast<double> (db) / 10.0));
    if (power.empty()) return std::numeric_limits<float>::quiet_NaN();
    std::sort (power.begin(), power.end());
    const auto at = quantile * static_cast<double> (power.size() - 1);
    const auto lower = static_cast<size_t> (std::floor (at));
    const auto upper = static_cast<size_t> (std::ceil (at));
    const auto value = power[lower] + (power[upper] - power[lower]) * (at - lower);
    return static_cast<float> (10.0 * std::log10 (value));
}
}

TonalCapture::TonalCapture (int sampleRate, int channels)
{
    if (sampleRate >= 40'000 && sampleRate <= 768'000 && channels >= 1 && channels <= 2)
    {
        meter = kirin_reference_tonal_create (static_cast<std::uint32_t> (sampleRate),
                                              static_cast<std::uint32_t> (channels));
        if (meter != nullptr)
        {
            latest.sample_rate = static_cast<std::uint32_t> (sampleRate);
            latest.channels = static_cast<std::uint32_t> (channels);
            kirin_reference_tonal_snapshot (meter, &latest);
            std::copy (std::begin (latest.fft_size), std::end (latest.fft_size), fftSize.begin());
            std::copy (std::begin (latest.hop_samples), std::end (latest.hop_samples), hopSamples.begin());
            if (std::any_of (hopSamples.begin(), hopSamples.end(), [] (auto hop) { return hop == 0; }))
            {
                kirin_reference_tonal_drop (meter);
                meter = nullptr;
                return;
            }
            for (int band = 0; band < 60; ++band)
            {
                const auto plane = planeForBand (band);
                values[size_t (band)].reserve (
                    static_cast<size_t> (7'200) * static_cast<size_t> (sampleRate)
                    / hopSamples[size_t (plane)] + 1);
            }
        }
    }
}

TonalCapture::~TonalCapture() { kirin_reference_tonal_drop (meter); }

bool TonalCapture::push (const float* interleaved, size_t sampleCount)
{
    if (meter == nullptr || ! kirin_reference_tonal_push (meter, interleaved, sampleCount)
        || ! kirin_reference_tonal_snapshot (meter, &latest)) return false;
    for (int plane = 0; plane < 3; ++plane)
    {
        const auto end = latest.window_end_samples[plane];
        if (end == 0 || end == acceptedWindowEnd[size_t (plane)]) continue;
        acceptedWindowEnd[size_t (plane)] = end;
        for (int band = 0; band < 60; ++band)
            if (planeForBand (band) == plane)
                values[size_t (band)].push_back (
                    (latest.valid_bits & (std::uint64_t (1) << band)) != 0
                        ? latest.values_db[band] : std::numeric_limits<float>::quiet_NaN());
    }
    return true;
}

CaptureTonalSummary TonalCapture::finishAndStore (
    const juce::File& transportRoot, const juce::String& captureId,
    const juce::String& baseCaptureSha256, std::int64_t hostStart) const
{
    const auto summary = finish();
    CaptureTonalArtifactInput input;
    input.captureId = captureId;
    input.baseCaptureSha256 = baseCaptureSha256;
    input.hostStart = hostStart;
    input.sampleRate = summary.sampleRate;
    input.channels = summary.channels;
    input.frames = summary.frames;
    input.fftSize = fftSize;
    input.hopSamples = hopSamples;
    input.values = &values;
    return storeCaptureTonalArtifact (transportRoot, input, summary);
}

CaptureTonalSummary TonalCapture::finish() const
{
    CaptureTonalSummary result;
    result.sampleRate = static_cast<int> (latest.sample_rate);
    result.channels = static_cast<int> (latest.channels);
    result.frames = latest.frames_seen;
    for (size_t band = 0; band < values.size(); ++band)
        if (! values[band].empty())
        {
            result.p10[band] = percentileDb (values[band], 0.10);
            result.median[band] = percentileDb (values[band], 0.50);
            result.p90[band] = percentileDb (values[band], 0.90);
            if (std::isfinite (result.median[band]))
                result.validBits |= std::uint64_t (1) << band;
        }
    return result.valid() ? result : CaptureTonalSummary {};
}

size_t TonalCapture::allocatedBytes() const noexcept
{
    size_t result = kirin_reference_tonal_allocated_bytes (meter);
    for (const auto& band : values) result += band.capacity() * sizeof (float);
    return result;
}

size_t TonalCapture::projectedArtifactBytes() const noexcept
{
    size_t result = 1024;
    for (int plane = 0; plane < 3; ++plane)
    {
        size_t bands = 0, windows = 0;
        for (int band = 0; band < 60; ++band)
            if (planeForBand (band) == plane)
            {
                ++bands;
                windows = std::max (windows, values[size_t (band)].capacity());
            }
        const auto cells = bands * windows;
        result += cells * sizeof (float) + (cells + 7) / 8;
    }
    return result;
}

void writeCaptureTonalSummary (juce::MemoryOutputStream& output, const CaptureTonalSummary& value)
{
    output.writeInt (0x544f4e31);
    output.writeInt (value.sampleRate); output.writeInt (value.channels);
    output.writeInt64 (static_cast<juce::int64> (value.frames));
    output.writeInt64 (static_cast<juce::int64> (value.validBits));
    output.writeString (value.artifactSha256);
    output.writeString (value.recoveryKey);
    output.writeInt64 (value.artifactBytes);
    for (const auto* series : { &value.p10, &value.median, &value.p90 })
        for (const auto item : *series) output.writeFloat (item);
}

bool readCaptureTonalSummary (
    juce::MemoryInputStream& input, CaptureTonalSummary& value, bool receiptEncoded)
{
    const std::int64_t minimumBytes = 4 + 4 + 4 + 8 + 8
        + (receiptEncoded ? 1 + 1 + 8 : 0) + 3 * 60 * 4;
    if (input.getNumBytesRemaining() < minimumBytes || input.readInt() != 0x544f4e31) return false;
    value.sampleRate = input.readInt(); value.channels = input.readInt();
    value.frames = static_cast<std::uint64_t> (input.readInt64());
    value.validBits = static_cast<std::uint64_t> (input.readInt64());
    if (receiptEncoded)
    {
        value.artifactSha256 = input.readString();
        value.recoveryKey = input.readString();
        value.artifactBytes = input.readInt64();
    }
    for (auto* series : { &value.p10, &value.median, &value.p90 })
        for (auto& item : *series) item = input.readFloat();
    if (! input.isExhausted()) return false;
    if (value.validBits == 0)
        return value.sampleRate == 0 && value.channels == 0 && value.frames == 0
            && value.artifactSha256.isEmpty() && value.recoveryKey.isEmpty()
            && value.artifactBytes == 0;
    return value.valid();
}
}
