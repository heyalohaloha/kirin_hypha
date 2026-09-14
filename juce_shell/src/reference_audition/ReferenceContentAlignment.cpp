#include "ReferenceContentAlignment.h"
#include "ReferenceContentCorrelation.h"
#include "ReferenceProbeAudio.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace hypha::reference_audition
{
    namespace
    {
        double correlation (const std::vector<double>& a, const std::vector<double>& b, size_t start)
        {
            double meanA = 0.0, meanB = 0.0;
            for (size_t i = 0; i < a.size(); ++i) { meanA += a[i]; meanB += b[start + i]; }
            meanA /= static_cast<double> (a.size());
            meanB /= static_cast<double> (a.size());
            double dot = 0.0, energyA = 0.0, energyB = 0.0;
            for (size_t i = 0; i < a.size(); ++i)
            {
                const auto left = a[i] - meanA;
                const auto right = b[start + i] - meanB;
                dot += left * right; energyA += left * left; energyB += right * right;
            }
            return energyA > 1.0e-6 && energyB > 1.0e-6 ? dot / std::sqrt (energyA * energyB) : -1.0;
        }

        std::vector<std::int64_t> coarsePositions (const RuntimeACaptureAudio& a,
                                                  const RuntimeDetailedMeasurement& measured)
        {
            const auto& waveform = *measured.waveform;
            const auto hop = waveform.framesPerBin;
            const auto aHop = static_cast<std::int64_t> (std::llround (
                static_cast<double> (hop) * a.sampleRateHz / measured.audio.sampleRateHz));
            std::vector<double> observed, registered;
            if (aHop < 1) return {};
            for (std::int64_t first = 0; first + aHop <= a.frameCount; first += aHop)
            {
                double power = 0.0;
                for (auto frame = first; frame < first + aHop; ++frame)
                    for (int channel = 0; channel < a.channels; ++channel)
                    {
                        const auto value = a.interleaved[static_cast<size_t> (frame * a.channels + channel)];
                        power += static_cast<double> (value) * value;
                    }
                observed.push_back (10.0 * std::log10 (juce::jmax (1.0e-14, power / (aHop * a.channels))));
            }
            const auto bins = waveform.rmsMillidbfs.front().size();
            for (size_t bin = 0; bin < bins; ++bin)
            {
                double power = 0.0;
                for (const auto& channel : waveform.rmsMillidbfs)
                    power += std::pow (10.0, channel[bin] / 10000.0);
                registered.push_back (10.0 * std::log10 (juce::jmax (1.0e-14,
                    power / static_cast<double> (waveform.rmsMillidbfs.size()))));
            }
            std::vector<std::pair<double, std::int64_t>> ranked;
            if (observed.size() >= 8 && observed.size() <= registered.size())
                for (size_t offset = 0; offset + observed.size() <= registered.size(); ++offset)
                {
                    const auto score = correlation (observed, registered, offset);
                    if (score >= 0.45) ranked.emplace_back (score, static_cast<std::int64_t> (offset) * hop);
                }
            std::sort (ranked.begin(), ranked.end(), [] (const auto& left, const auto& right) {
                return left.first > right.first;
            });
            std::vector<std::int64_t> positions;
            const auto spacing = juce::jmax (hop * 2, measured.audio.sampleRateHz / 2);
            for (const auto& candidate : ranked)
            {
                if (std::none_of (positions.begin(), positions.end(), [&] (auto previous) {
                    return std::abs (previous - candidate.second) < spacing;
                })) positions.push_back (candidate.second);
                if (positions.size() == 6) break;
            }
            // A host timeline is only a search hint. The same acoustic tests must
            // pass; time alone never establishes song or position identity.
            const auto timeline = static_cast<std::int64_t> (std::llround (
                static_cast<double> (a.startSample) * measured.audio.sampleRateHz / a.sampleRateHz));
            if (timeline >= 0 && timeline < measured.audio.totalSampleFrames
                && std::none_of (positions.begin(), positions.end(), [&] (auto previous) {
                    return std::abs (previous - timeline) < spacing;
                })) positions.push_back (timeline);
            return positions;
        }
    }

    RuntimeContentAlignment alignReferenceContent (
        const RuntimeACaptureAudio& a, const RuntimeSource& source,
        const RuntimeDetailedMeasurement& measured, bool conversionApproved,
        std::optional<std::int64_t> expectedSourceStart)
    {
        RuntimeContentAlignment result;
        result.reason = "reference_alignment_waiting_for_content";
        // A short worker job never blocks another instance or the audio callback.
        static std::atomic<int> activeJobs { 0 };
        const auto previousJobs = activeJobs.fetch_add (1, std::memory_order_acq_rel);
        struct Release { std::atomic<int>& count; ~Release() { count.fetch_sub (1, std::memory_order_release); } } release { activeJobs };
        if (previousJobs >= 2) { result.reason = "reference_alignment_busy"; return result; }
        if (a.sampleRateHz < 8000 || a.sampleRateHz > 768000 || a.channels < 1 || a.channels > 2
            || a.frameCount < a.sampleRateHz * 3 || a.frameCount > 4'194'304
            || a.interleaved.size() != static_cast<size_t> (a.frameCount * a.channels)
            || source.audio.sampleRateHz < 8000 || source.audio.sampleRateHz > 768000
            || source.audio.channels != a.channels
            || source.sourceFileSha256.isEmpty() || source.sourcePcmSha256.isEmpty()
            || source.sourceFileSha256 != measured.sourceFileSha256
            || source.sourcePcmSha256 != measured.sourcePcmSha256
            || source.audio.sampleRateHz != measured.audio.sampleRateHz
            || source.audio.channels != measured.audio.channels
            || source.audio.totalSampleFrames != measured.audio.totalSampleFrames
            || !measured.waveform || measured.waveform->framesPerBin < 1
            || measured.waveform->rmsMillidbfs.size() != static_cast<size_t> (a.channels)
            || measured.waveform->rmsMillidbfs.front().empty()) return result;
        const auto bins = measured.waveform->rmsMillidbfs.front().size();
        if (bins > 4096) return result;
        for (const auto& channel : measured.waveform->rmsMillidbfs)
            if (channel.size() != bins) return result;
        for (const auto value : a.interleaved) if (!std::isfinite (value)) return result;
        if (source.audio.sampleRateHz != a.sampleRateHz && !conversionApproved) return result;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (source.absolutePath)));
        if (!reader || reader->lengthInSamples != source.audio.totalSampleFrames
            || std::abs (reader->sampleRate - source.audio.sampleRateHz) > 0.001
            || reader->numChannels != static_cast<unsigned> (a.channels)) return result;
        const auto positions = expectedSourceStart ? std::vector<std::int64_t> { *expectedSourceStart }
                                                   : coarsePositions (a, measured);
        const auto window = juce::jmin<std::int64_t> (a.frameCount / 4, 1'048'576);
        const auto lag = static_cast<int> (juce::jmin<std::int64_t> (window / 3 - 1,
            static_cast<std::int64_t> (std::ceil (measured.waveform->framesPerBin * 1.5
                * a.sampleRateHz / source.audio.sampleRateHz))));
        std::array<std::int64_t, 3> starts { a.frameCount / 8, a.frameCount * 3 / 8, a.frameCount * 5 / 8 };
        int channel = 0;
        if (a.channels == 2)
        {
            std::array<double, 2> energy {};
            for (std::int64_t frame = 0; frame < a.frameCount; ++frame)
                for (int c = 0; c < 2; ++c) {
                    const auto value = a.interleaved[static_cast<size_t> (frame * 2 + c)];
                    energy[static_cast<size_t> (c)] += static_cast<double> (value) * value;
                }
            channel = energy[1] > energy[0] ? 1 : 0;
        }
        std::vector<std::pair<std::int64_t, RuntimeContentAlignment>> matches;
        for (const auto position : positions)
        {
            std::array<std::int64_t, 3> offsets {};
            std::array<double, 3> ambiguity {};
            double minimumScore = 1.0, minimumAmbiguity = 300.0;
            int unrelatedWindows = 0;
            bool accepted = true;
            for (size_t index = 0; index < starts.size(); ++index)
            {
                const auto first = starts[index];
                const auto sourceFirst = position + static_cast<std::int64_t> (std::llround (
                    static_cast<double> (first) * source.audio.sampleRateHz / a.sampleRateHz));
                std::vector<float> raw, left (static_cast<size_t> (window)), right (static_cast<size_t> (window));
                if (!readReferenceProbe (*reader, sourceFirst, window, static_cast<int> (a.sampleRateHz), a.channels, raw))
                { accepted = false; break; }
                for (std::int64_t frame = 0; frame < window; ++frame)
                {
                    left[static_cast<size_t> (frame)] = a.interleaved[static_cast<size_t> ((first + frame) * a.channels + channel)];
                    right[static_cast<size_t> (frame)] = raw[static_cast<size_t> (frame * a.channels + channel)];
                }
                const auto estimate = correlateReferenceContent (left, right, static_cast<int> (a.sampleRateHz), lag);
                // Three separated, agreeing windows jointly establish the map.
                // One tonal window may be weaker; it cannot decide on its own.
                if (estimate.correlation < 0.75 || estimate.ambiguityDb < 1.5
                    || std::abs (estimate.offsetSamples) >= lag)
                {
                    accepted = false;
                    double energy = 0.0;
                    for (const auto value : left) energy += static_cast<double> (value) * value;
                    if (estimate.correlation < 0.35 && energy / window > 1.0e-8) ++unrelatedWindows;
                    if (!expectedSourceStart) break;
                    continue;
                }
                offsets[index] = estimate.offsetSamples;
                // Content identity uses the broader band. Only after it agrees,
                // refine timing above low-end EQ phase without accepting a
                // repeated high-frequency drum pattern as song identity.
                const auto detail = correlateReferenceContent (left, right, static_cast<int> (a.sampleRateHz), lag, true);
                if (detail.accepted && std::abs (detail.offsetSamples - estimate.offsetSamples) <= 2)
                    offsets[index] = detail.offsetSamples;
                ambiguity[index] = estimate.ambiguityDb;
                minimumScore = juce::jmin (minimumScore, estimate.correlation);
                minimumAmbiguity = juce::jmin (minimumAmbiguity, estimate.ambiguityDb);
            }
            if (!accepted) {
                if (expectedSourceStart && unrelatedWindows == 3) result.reason = "reference_alignment_content_changed";
                continue;
            }
            std::sort (ambiguity.begin(), ambiguity.end());
            if (ambiguity[1] < 3.0) continue;
            std::sort (offsets.begin(), offsets.end());
            const auto spread = offsets.back() - offsets.front();
            if (spread > juce::jmax<std::int64_t> (2, a.sampleRateHz / 20000)) {
                if (expectedSourceStart) result.reason = "reference_alignment_timing_changed";
                continue;
            }
            const auto exact = position + static_cast<std::int64_t> (std::llround (
                static_cast<double> (offsets[1]) * source.audio.sampleRateHz / a.sampleRateHz));
            RuntimeContentAlignment match;
            match.sourceStartSample = exact;
            match.minimumCorrelation = minimumScore;
            match.minimumAmbiguityDb = minimumAmbiguity;
            match.windowSpreadSamples = spread;
            matches.emplace_back (exact, std::move (match));
        }
        if (matches.empty()) return result;
        // A repeated matching passage cannot choose its own occurrence. Continue
        // observing instead of silently choosing the earliest or loudest chorus.
        for (const auto& match : matches)
            if (std::abs (match.first - matches.front().first) > source.audio.sampleRateHz / 100)
            { result.reason = "reference_alignment_ambiguous"; return result; }
        result = std::move (matches.front().second);
        if (!readReferenceProbe (*reader, result.sourceStartSample, a.frameCount,
                static_cast<int> (a.sampleRateHz), a.channels, result.alignedProbe))
        { result.reason = "reference_alignment_source_unavailable"; return result; }
        result.established = true;
        return result;
    }
}
