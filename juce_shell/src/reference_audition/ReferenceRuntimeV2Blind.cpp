#include "ReferenceRuntimeV2Blind.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include "kirin_hypha_ffi.h"

namespace hypha::reference_audition
{
    namespace
    {
        constexpr int envelopeMilliseconds = 20;
        constexpr int envelopeBatchWindows = 128;
        constexpr int sincHalfTaps = 24;
        constexpr double minimumCorrelation = 0.72;
        constexpr double minimumUnambiguousGap = 0.015;
        constexpr std::int64_t maximumSearchWindows = 180'000;

        juce::String pcmHash (const std::vector<float>& samples)
        {
            juce::MemoryBlock canonical (samples.size() * sizeof (float), true);
            auto* output = static_cast<std::uint8_t*> (canonical.getData());
            for (size_t index = 0; index < samples.size(); ++index)
            {
                const float normalized = samples[index] == 0.0f ? 0.0f : samples[index];
                std::uint32_t bits = 0;
                std::memcpy (&bits, &normalized, sizeof (bits));
                output[index * 4] = static_cast<std::uint8_t> (bits & 0xffu);
                output[index * 4 + 1] = static_cast<std::uint8_t> ((bits >> 8u) & 0xffu);
                output[index * 4 + 2] = static_cast<std::uint8_t> ((bits >> 16u) & 0xffu);
                output[index * 4 + 3] = static_cast<std::uint8_t> ((bits >> 24u) & 0xffu);
            }
            return juce::SHA256 (canonical).toHexString();
        }

        std::vector<double> memoryEnvelope (const RuntimeACaptureAudio& audio)
        {
            const auto windowFrames = juce::jmax<std::int64_t> (
                1, audio.sampleRateHz * envelopeMilliseconds / 1'000);
            const auto windows = audio.frameCount / windowFrames;
            std::vector<double> result;
            result.reserve (static_cast<size_t> (windows));
            for (std::int64_t window = 0; window < windows; ++window)
            {
                double sum = 0.0;
                for (std::int64_t frame = 0; frame < windowFrames; ++frame)
                    for (int channel = 0; channel < audio.channels; ++channel)
                    {
                        const auto value = audio.interleaved[static_cast<size_t> (
                            ((window * windowFrames + frame) * audio.channels) + channel)];
                        sum += static_cast<double> (value) * value;
                    }
                const auto mean = sum / static_cast<double> (windowFrames * audio.channels);
                result.push_back (10.0 * std::log10 (juce::jmax (1.0e-14, mean)));
            }
            return result;
        }

        bool readerEnvelope (juce::AudioFormatReader& reader, std::int64_t start,
                             std::int64_t end, std::vector<double>& result)
        {
            const auto windowFrames = juce::jmax<std::int64_t> (
                1, static_cast<std::int64_t> (std::llround (
                    reader.sampleRate * envelopeMilliseconds / 1'000.0)));
            const auto windows = juce::jmin<std::int64_t> (
                maximumSearchWindows, juce::jmax<std::int64_t> (0, end - start) / windowFrames);
            if (windows < 2)
                return false;
            result.clear();
            result.reserve (static_cast<size_t> (windows));
            juce::AudioBuffer<float> block (
                static_cast<int> (reader.numChannels),
                static_cast<int> (windowFrames * envelopeBatchWindows));
            for (std::int64_t first = 0; first < windows; first += envelopeBatchWindows)
            {
                const auto count = static_cast<int> (juce::jmin<std::int64_t> (
                    envelopeBatchWindows, windows - first));
                const auto frames = static_cast<int> (windowFrames * count);
                block.clear();
                if (! reader.read (&block, 0, frames, start + first * windowFrames,
                                   true, reader.numChannels > 1))
                    return false;
                for (int window = 0; window < count; ++window)
                {
                    double sum = 0.0;
                    for (std::int64_t frame = 0; frame < windowFrames; ++frame)
                        for (int channel = 0; channel < static_cast<int> (reader.numChannels);
                             ++channel)
                        {
                            const auto value = block.getSample (
                                channel, static_cast<int> (window * windowFrames + frame));
                            sum += static_cast<double> (value) * value;
                        }
                    const auto mean = sum / static_cast<double> (
                        windowFrames * static_cast<std::int64_t> (reader.numChannels));
                    result.push_back (10.0 * std::log10 (juce::jmax (1.0e-14, mean)));
                }
            }
            return true;
        }

        double correlation (const std::vector<double>& needle,
                            const std::vector<double>& haystack, size_t offset)
        {
            double needleMean = 0.0;
            double haystackMean = 0.0;
            for (size_t index = 0; index < needle.size(); ++index)
            {
                needleMean += needle[index];
                haystackMean += haystack[offset + index];
            }
            needleMean /= static_cast<double> (needle.size());
            haystackMean /= static_cast<double> (needle.size());
            double dot = 0.0;
            double left = 0.0;
            double right = 0.0;
            for (size_t index = 0; index < needle.size(); ++index)
            {
                const auto a = needle[index] - needleMean;
                const auto b = haystack[offset + index] - haystackMean;
                dot += a * b;
                left += a * a;
                right += b * b;
            }
            return left > 1.0e-9 && right > 1.0e-9
                ? dot / std::sqrt (left * right) : -1.0;
        }

        std::optional<std::int64_t> alignedSourceStart (
            const RuntimeACaptureAudio& a, juce::AudioFormatReader& reader,
            std::int64_t cueStart, std::int64_t cueEnd)
        {
            const auto aEnvelope = memoryEnvelope (a);
            std::vector<double> bEnvelope;
            if (aEnvelope.size() < 100 || ! readerEnvelope (reader, cueStart, cueEnd, bEnvelope)
                || bEnvelope.size() < aEnvelope.size())
                return std::nullopt;
            double best = -1.0;
            double second = -1.0;
            size_t bestOffset = 0;
            for (size_t offset = 0; offset + aEnvelope.size() <= bEnvelope.size(); ++offset)
            {
                const auto score = correlation (aEnvelope, bEnvelope, offset);
                if (score > best)
                {
                    if (offset + aEnvelope.size() <= bestOffset
                        || bestOffset + aEnvelope.size() <= offset)
                        second = best;
                    best = score;
                    bestOffset = offset;
                }
                else if ((offset + aEnvelope.size() <= bestOffset
                          || bestOffset + aEnvelope.size() <= offset)
                         && score > second)
                    second = score;
            }
            const auto sourceWindowFrames = juce::jmax<std::int64_t> (
                1, static_cast<std::int64_t> (std::llround (
                    reader.sampleRate * envelopeMilliseconds / 1'000.0)));
            const auto timeline = static_cast<std::int64_t> (std::llround (
                static_cast<long double> (a.startSample) * reader.sampleRate / a.sampleRateHz));
            if (timeline >= cueStart
                && timeline + static_cast<std::int64_t> (aEnvelope.size()) * sourceWindowFrames
                    <= cueEnd)
            {
                const auto timelineOffset = static_cast<size_t> (
                    (timeline - cueStart) / sourceWindowFrames);
                const auto timelineScore = correlation (aEnvelope, bEnvelope, timelineOffset);
                if (timelineScore >= minimumCorrelation && timelineScore + 0.01 >= best)
                    return timeline;
            }
            if (best < minimumCorrelation || (second >= 0.0 && best - second < minimumUnambiguousGap))
                return std::nullopt;
            return cueStart + static_cast<std::int64_t> (bestOffset) * sourceWindowFrames;
        }

        bool readAligned (juce::AudioFormatReader& reader, std::int64_t sourceStart,
                          std::int64_t outputFrames, int outputRate, int channels,
                          std::vector<float>& interleaved)
        {
            if (static_cast<int> (reader.numChannels) != channels || outputFrames < 1)
                return false;
            if (std::abs (reader.sampleRate - outputRate) <= 0.001)
            {
                if (sourceStart < 0 || sourceStart + outputFrames > reader.lengthInSamples
                    || outputFrames > std::numeric_limits<int>::max())
                    return false;
                juce::AudioBuffer<float> exact (channels, static_cast<int> (outputFrames));
                if (! reader.read (&exact, 0, static_cast<int> (outputFrames), sourceStart,
                                   true, channels > 1))
                    return false;
                interleaved.resize (static_cast<size_t> (outputFrames * channels));
                for (std::int64_t frame = 0; frame < outputFrames; ++frame)
                    for (int channel = 0; channel < channels; ++channel)
                        interleaved[static_cast<size_t> (frame * channels + channel)]
                            = exact.getSample (channel, static_cast<int> (frame));
                return true;
            }
            const auto ratio = reader.sampleRate / outputRate;
            const auto rawStart = sourceStart - sincHalfTaps;
            const auto rawEnd = static_cast<std::int64_t> (std::ceil (
                sourceStart + static_cast<double> (outputFrames - 1) * ratio))
                + sincHalfTaps + 1;
            const auto readStart = juce::jmax<std::int64_t> (0, rawStart);
            const auto readEnd = juce::jmin<std::int64_t> (reader.lengthInSamples, rawEnd);
            const auto readFrames64 = juce::jmax<std::int64_t> (0, readEnd - readStart);
            if (readFrames64 < 1 || readFrames64 > std::numeric_limits<int>::max())
                return false;
            const auto readFrames = static_cast<int> (readFrames64);
            juce::AudioBuffer<float> input (channels, readFrames);
            if (! reader.read (&input, 0, readFrames, readStart, true, channels > 1))
                return false;
            interleaved.assign (static_cast<size_t> (outputFrames * channels), 0.0f);
            const auto cutoff = juce::jmin (1.0, outputRate / reader.sampleRate) * 0.94;
            for (std::int64_t outputFrame = 0; outputFrame < outputFrames; ++outputFrame)
            {
                const auto position = sourceStart + static_cast<double> (outputFrame) * ratio;
                const auto center = static_cast<std::int64_t> (std::floor (position));
                for (int channel = 0; channel < channels; ++channel)
                {
                    double weighted = 0.0;
                    double weightSum = 0.0;
                    for (int tap = -sincHalfTaps + 1; tap <= sincHalfTaps; ++tap)
                    {
                        const auto sourceFrame = center + tap;
                        const auto inputOffset = sourceFrame - readStart;
                        if (inputOffset < 0 || inputOffset >= readFrames)
                            continue;
                        const auto distance = position - static_cast<double> (sourceFrame);
                        const auto scaled = juce::MathConstants<double>::pi * cutoff * distance;
                        const auto sinc = std::abs (scaled) < 1.0e-12
                            ? cutoff : cutoff * std::sin (scaled) / scaled;
                        const auto normalized = distance / sincHalfTaps;
                        const auto window = std::abs (normalized) >= 1.0 ? 0.0
                            : 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * normalized)
                                   + 0.08 * std::cos (
                                       juce::MathConstants<double>::twoPi * normalized);
                        const auto weight = sinc * window;
                        weighted += input.getSample (channel, static_cast<int> (inputOffset)) * weight;
                        weightSum += weight;
                    }
                    if (weightSum == 0.0)
                        return false;
                    interleaved[static_cast<size_t> (outputFrame * channels + channel)]
                        = static_cast<float> (weighted / weightSum);
                }
            }
            return true;
        }
    }

    bool RuntimeV2Blind::enterPreparation() noexcept
    {
        auto current = lifecycle.load (std::memory_order_acquire);
        while (current != active && current != revealed && current != preparing)
        {
            if (lifecycle.compare_exchange_weak (current, preparing,
                                                 std::memory_order_acq_rel))
            {
                while (callbacksInFlight.load (std::memory_order_acquire) != 0
                       || snapshotReadersInFlight.load (std::memory_order_acquire) != 0)
                    juce::Thread::yield();
                return true;
            }
        }
        return false;
    }

    void RuntimeV2Blind::prepare (
        const std::shared_ptr<const RuntimeACaptureAudio>& a,
        const RuntimeCandidate& candidate, const RuntimeCue& cue,
        const std::shared_ptr<const RuntimeSource>& source,
        bool sampleRateConversionApproved)
    {
        if (! enterPreparation())
            return;
        frozenA.clear();
        frozenB.clear();
        dawRevisionId.clear();
        aCuePcmSha256.clear();
        bCuePcmSha256.clear();
        rejectionCode.clear();
        resetSession();
        const auto reject = [this] (const juce::String& code)
        {
            rejectionCode = code;
            lifecycle.store (unavailable, std::memory_order_release);
        };
        if (a == nullptr || source == nullptr || candidate.sourceKind != "work_version"
            || candidate.sourceWorkId.isEmpty() || candidate.sourceRecordingId.isEmpty()
            || candidate.sourceVersionId.isEmpty() || cue.sampleRateHz != source->audio.sampleRateHz
            || a->sampleRateHz < 8'000 || a->sampleRateHz > 768'000
            || (a->channels != 1 && a->channels != 2) || a->frameCount < a->sampleRateHz * 3
            || a->interleaved.size() != static_cast<size_t> (a->frameCount * a->channels))
        {
            reject ("reference_blind_identity_unavailable");
            return;
        }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto reader = std::unique_ptr<juce::AudioFormatReader> (
            formats.createReaderFor (juce::File (source->absolutePath)));
        if (reader == nullptr || static_cast<int> (reader->numChannels) != a->channels
            || (std::abs (reader->sampleRate - a->sampleRateHz) > 0.001
                && ! sampleRateConversionApproved))
        {
            reject ("reference_blind_source_unavailable");
            return;
        }
        const auto cueStart = juce::jlimit<std::int64_t> (
            0, reader->lengthInSamples, cue.startSample);
        const auto cueEnd = juce::jlimit<std::int64_t> (
            cueStart, reader->lengthInSamples, cue.endSample);
        const auto aligned = alignedSourceStart (*a, *reader, cueStart, cueEnd);
        if (! aligned || ! readAligned (*reader, *aligned, a->frameCount,
                                        static_cast<int> (a->sampleRateHz), a->channels, frozenB))
        {
            reject ("reference_blind_alignment_unavailable");
            return;
        }
        frozenA = a->interleaved;
        const auto frozenBHash = pcmHash (frozenB);
        if (frozenBHash == a->cuePcmSha256)
        {
            reject ("reference_blind_same_revision");
            return;
        }
        KirinReferenceGainFacts facts {};
        if (! kirin_hypha_analyze_reference_gain (
                frozenA.data(), frozenB.data(), static_cast<size_t> (a->frameCount),
                static_cast<std::uint32_t> (a->sampleRateHz),
                static_cast<std::uint32_t> (a->channels), &facts))
        {
            reject ("reference_blind_gain_unavailable");
            return;
        }
        aStartSample = a->startSample;
        frameCount = a->frameCount;
        channels = a->channels;
        sampleRateHz = static_cast<int> (a->sampleRateHz);
        loopEnabled = cue.loopEnabled;
        bStartSample = *aligned;
        bSampleRateHz = static_cast<int> (std::llround (reader->sampleRate));
        bEndSample = bStartSample + static_cast<std::int64_t> (std::ceil (
            static_cast<long double> (frameCount) * bSampleRateHz / sampleRateHz));
        dawRevisionId = a->dawRevisionId;
        aCuePcmSha256 = a->cuePcmSha256;
        bCuePcmSha256 = frozenBHash;
        pairedBlockCount = facts.paired_block_count;
        pairedLoudnessDeltaDb = facts.paired_loudness_delta_median_millilu / 1'000.0;
        aCueTruePeakDbtp = facts.a_cue_true_peak_millidbtp / 1'000.0;
        bCueTruePeakDbtp = facts.b_cue_true_peak_millidbtp / 1'000.0;
        const auto gainPlan = planRuntimeV2BlindGain (
            pairedLoudnessDeltaDb, aCueTruePeakDbtp, bCueTruePeakDbtp);
        aGainDb = gainPlan.aGainDb;
        bGainDb = gainPlan.bGainDb;
        requiredAAttenuationDb = gainPlan.requiredAAttenuationDb;
        preservedPeakCeilingDbtp = gainPlan.preservedPeakCeilingDbtp;
        if (gainPlan.lowerAApprovalRequired)
        {
            requiredAAttenuationDb = bGainDb;
            lifecycle.store (approvalRequired, std::memory_order_release);
            return;
        }
        lifecycle.store (prepared, std::memory_order_release);
    }

    bool RuntimeV2Blind::start (bool approveLowerA) noexcept
    {
        int expected = approveLowerA ? approvalRequired : prepared;
        if (! lifecycle.compare_exchange_strong (expected, preparing, std::memory_order_acq_rel))
            return false;
        while (snapshotReadersInFlight.load (std::memory_order_acquire) != 0)
            juce::Thread::yield();
        if (approveLowerA)
        {
            aGainDb = -requiredAAttenuationDb;
            bGainDb = 0.0;
        }
        resetSession();
        try
        {
            const auto commitment = createRuntimeV2BlindCommitment();
            trialId = commitment.trialId;
            assignmentNonceHex = commitment.nonceHex;
            assignmentCommitmentSha256 = commitment.commitmentSha256;
            stimulusOneIsB.store (commitment.stimulusOneIsB, std::memory_order_relaxed);
        }
        catch (...) { lifecycle.store (expected, std::memory_order_release); return false; }
        requestedStimulus.store (1, std::memory_order_relaxed);
        requestSequence.store (1, std::memory_order_release);
        lifecycle.store (active, std::memory_order_release);
        return true;
    }

    int RuntimeV2Blind::sideForStimulus (int stimulus) const noexcept
    {
        return ((stimulus == 1) == stimulusOneIsB.load (std::memory_order_relaxed)) ? 1 : 0;
    }

    bool RuntimeV2Blind::requestStimulus (int stimulus) noexcept
    {
        const auto state = lifecycle.load (std::memory_order_acquire);
        if ((state != active && state != revealed) || (stimulus != 1 && stimulus != 2))
            return false;
        requestedStimulus.store (stimulus, std::memory_order_relaxed);
        requestSequence.fetch_add (1, std::memory_order_release);
        return true;
    }

    bool RuntimeV2Blind::answer (int stimulus) noexcept
    {
        if (lifecycle.load (std::memory_order_acquire) != active
            || (stimulus != 1 && stimulus != 2)
            || stimulusOneFrames.load (std::memory_order_acquire) == 0
            || stimulusTwoFrames.load (std::memory_order_acquire) == 0)
            return false;
        answeredStimulus.store (stimulus, std::memory_order_release);
        return true;
    }

    bool RuntimeV2Blind::reveal() noexcept
    {
        if (answeredStimulus.load (std::memory_order_acquire) == 0)
            return false;
        int expected = active;
        return lifecycle.compare_exchange_strong (expected, revealed,
                                                   std::memory_order_acq_rel);
    }

    void RuntimeV2Blind::end() noexcept
    {
        const auto state = lifecycle.load (std::memory_order_acquire);
        if (state == active || state == revealed || state == invalidated)
        {
            lifecycle.store (preparing, std::memory_order_release);
            while (callbacksInFlight.load (std::memory_order_acquire) != 0
                   || snapshotReadersInFlight.load (std::memory_order_acquire) != 0)
                juce::Thread::yield();
            if (requiredAAttenuationDb > 0.0)
            {
                aGainDb = 0.0;
                bGainDb = requiredAAttenuationDb;
                lifecycle.store (approvalRequired, std::memory_order_release);
            }
            else
                lifecycle.store (prepared, std::memory_order_release);
        }
        resetSession();
    }

    void RuntimeV2Blind::invalidate() noexcept
    {
        const auto state = lifecycle.load (std::memory_order_acquire);
        if (state == active || state == revealed)
            lifecycle.store (invalidated, std::memory_order_release);
    }

    void RuntimeV2Blind::clear() noexcept
    {
        lifecycle.store (unavailable, std::memory_order_release);
        resetSession();
    }

    void RuntimeV2Blind::loseAudibleConfirmation() noexcept
    {
        activeStimulus.store (0, std::memory_order_release);
        confirmedSequence.store (0, std::memory_order_release);
    }

}
