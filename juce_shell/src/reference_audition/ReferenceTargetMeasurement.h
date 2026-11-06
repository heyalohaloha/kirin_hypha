#pragma once
#include "ReferenceMeasuredTarget.h"
#include "ReferenceLiveWindowLoudness.h"
#include "kirin_hypha_reference_visual_ffi.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace hypha::reference_audition
{
struct TargetMeasurement
{
    MeasuredTarget target;
    juce::String issue;
};

// Streaming, worker-only analysis. Keep facts for exact ranks; discard decoded audio every read.
inline TargetMeasurement measureTargetFile (const juce::File& file, const std::function<bool()>& cancelled,
                                            const std::function<void(double)>& progress = {})
{
    const juce::ScopedNoDenormals noDenormals;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader) return { {}, "FILE UNREADABLE" };
    if (reader->numChannels < 1 || reader->numChannels > 2) return { {}, "MONO / STEREO" };
    if (! std::isfinite (reader->sampleRate) || reader->sampleRate < 40000.0 || reader->sampleRate > 768000.0
        || std::abs (reader->sampleRate - std::round (reader->sampleRate)) > 1.0e-6) return { {}, "SAMPLE RATE" };
    const auto rate = static_cast<int> (reader->sampleRate);
    const auto channels = static_cast<int> (reader->numChannels);
    KirinSpectrumMeter spectrum;
    if (! spectrum.configure (rate, channels, 1)) return { {}, "SAMPLE RATE" };
    const int tick = rate / 10;
    if (reader->lengthInSamples < tick) return { {}, "FILE TOO SHORT" };
    const auto originalSize = file.getSize();
    const auto originalTime = file.getLastModificationTime();
    std::unique_ptr<KirinReferenceVisualMeter, decltype (&kirin_reference_visual_drop)> level (
        kirin_reference_visual_capture_create (static_cast<std::uint32_t> (rate), static_cast<std::uint32_t> (channels)),
        kirin_reference_visual_drop);
    if (! level) return { {}, "MEASUREMENT UNAVAILABLE" };
    std::array<std::vector<float>, 64> bands;
    std::array<double, 4> balancePower {};
    double peak = 0.0;
    std::int64_t ticks = 0;
    juce::AudioBuffer<float> decoded (channels, tick);
    std::vector<float> pcm (static_cast<size_t> (tick * channels));
    for (std::int64_t position = 0; position < reader->lengthInSamples;)
    {
        if (cancelled()) return { {}, {} };
        const auto count = static_cast<int> (std::min<std::int64_t> (tick, reader->lengthInSamples - position));
        if (! reader->read (&decoded, 0, count, position, true, channels == 2)) return { {}, "FILE UNREADABLE" };
        for (int frame = 0; frame < count; ++frame)
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto value = decoded.getSample (channel, frame);
                if (! std::isfinite (value)) return { {}, "AUDIO INVALID" };
                pcm[static_cast<size_t> (frame * channels + channel)] = value;
            }
        spectrum.push (pcm.data(), count);
        for (int done = 0; done < count;)
        {
            const auto chunk = std::min (count - done, 4096);
            if (! kirin_reference_visual_push (level.get(), pcm.data() + done * channels, static_cast<size_t> (chunk * channels)))
                return { {}, "MEASUREMENT UNAVAILABLE" };
            done += chunk;
        }
        KirinReferenceVisualBin bin {};
        if (! kirin_reference_visual_finish (level.get(), &bin)) return { {}, "MEASUREMENT UNAVAILABLE" };
        peak = std::max (peak, bin.true_peak);
        if (count == tick)
        {
            std::array<float, 64> frameBands {};
            std::array<double, 4> frameBalance {};
            if (! spectrum.lastFrame (frameBands, frameBalance)) return { {}, "MEASUREMENT UNAVAILABLE" };
            for (size_t i = 0; i < 64; ++i) bands[i].push_back (frameBands[i]);
            for (size_t i = 0; i < 4; ++i) balancePower[i] += std::pow (10.0, frameBalance[i] / 10.0);
            ++ticks;
        }
        position += count;
        if (progress) progress (static_cast<double> (position) / static_cast<double> (reader->lengthInSamples));
    }
    if (cancelled()) return { {}, {} };
    if (file.getSize() != originalSize || file.getLastModificationTime() != originalTime) return { {}, "FILE CHANGED" };
    MeasuredTarget target;
    target.id = "target-" + juce::Uuid().toString().removeCharacters ("-");
    target.name = file.getFileNameWithoutExtension().substring (0, 120);
    if (target.name.isEmpty()) target.name = "TARGET";
    target.channels = channels;
    target.duration = static_cast<double> (reader->lengthInSamples) / rate;
    double integrated = 0.0;
    if (! kirin_reference_visual_seal (level.get(), &integrated, &peak)) return { {}, "MEASUREMENT UNAVAILABLE" };
    target.integrated = std::isfinite (integrated) ? integrated : std::numeric_limits<double>::quiet_NaN();
    target.maximumTruePeak = 20.0 * std::log10 (std::max (peak, 1.0e-15));
    target.setCenters();
    target.spectrum.p10Db.resize (64); target.spectrum.medianDb.resize (64); target.spectrum.p90Db.resize (64);
    for (size_t i = 0; i < 64; ++i)
    {
        if (cancelled()) return { {}, {} };
        auto& values = bands[i];
        const auto rank = [&] (double ratio) {
            const auto at = static_cast<size_t> (std::llround (double (values.size() - 1) * ratio));
            std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (at), values.end());
            return values[at];
        };
        target.spectrum.p10Db[i] = rank (0.1); target.spectrum.medianDb[i] = rank (0.5); target.spectrum.p90Db[i] = rank (0.9);
    }
    for (size_t i = 0; i < 4; ++i) target.spectrum.balanceDb[i] = 10.0 * std::log10 (std::max (balancePower[i] / double (ticks), 1.0e-30));
    return target.valid() ? TargetMeasurement { std::move (target), {} } : TargetMeasurement { {}, "MEASUREMENT UNAVAILABLE" };
}
}
