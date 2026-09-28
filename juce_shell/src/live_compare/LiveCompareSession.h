#pragma once

#include "LiveCompareCorrespondence.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>

namespace hypha::live_compare
{
// PRE, Audio Thread: publishes the block only while a POST session demands PRE input. When the
// demand ends, the next demanded block opens a new run.
class PreFeeder
{
public:
    bool feed (Ring& ring, const BlockClock& block, const float* const* input, int channels) noexcept
    {
        if (ring.header.demand.load (std::memory_order_acquire) == 0)
        {
            publisher.reset();
            return false;
        }
        publisher.publish (ring, block, input, channels);
        return true;
    }

private:
    Publisher publisher;
};

struct RenderReport
{
    Verdict verdict = Verdict::noClock;
    bool preAudible = false; // PRE weight above zero at the end of the block
    bool preWaiting = false; // PRE is selected but POST sounds because the block is not proven
};

// POST, Audio Thread output (INV-LC4). PRE sounds only in blocks whose every frame is proven.
// Returning to PRE fades symmetrically over proven samples; losing the proof switches to POST at
// the block start and never uses unproven PRE samples. The approved gain applies to the PRE copy.
class PostRenderer
{
public:
    // Non-RT: sizes the scratch for the largest realtime block, the fade length and the POST input
    // history that MATCH reads (a power of two of at least historySeconds).
    void prepare (int maximumFrames, double sampleRate)
    {
        capacity = std::max (0, maximumFrames);
        left = std::make_unique<float[]> (static_cast<std::size_t> (capacity));
        right = std::make_unique<float[]> (static_cast<std::size_t> (capacity));
        fadeFrames = std::max (1, static_cast<int> (sampleRate * fadeSeconds + 0.5));
        std::int64_t frames = 1;
        while (static_cast<double> (frames) < sampleRate * historySeconds)
            frames <<= 1;
        historyFrames = frames;
        history = std::make_unique<std::atomic<float>[]> (static_cast<std::size_t> (frames) * 2);
        historyStart.store (0, std::memory_order_relaxed);
        historyEnd.store (0, std::memory_order_relaxed);
        matchOffsetValid.store (false, std::memory_order_relaxed);
        consumer.reset();
        weight = 0.0f;
    }

    // MATCH (non-RT) reads the POST input history, contiguous over [start, end) of POST's clock,
    // and the K that mapped the latest block, then verifies the history was not overwritten.
    struct HistoryView
    {
        const std::atomic<float>* samples = nullptr; // interleaved stereo, historyFrames long
        std::int64_t frames = 0;
        std::int64_t start = 0, end = 0, k = 0;
        bool kValid = false;
    };

    HistoryView historyView() const noexcept
    {
        HistoryView view;
        view.samples = history.get();
        view.frames = historyFrames;
        view.end = historyEnd.load (std::memory_order_acquire);
        view.start = historyStart.load (std::memory_order_acquire);
        view.k = matchOffset.load (std::memory_order_acquire);
        view.kValid = matchOffsetValid.load (std::memory_order_acquire);
        return view;
    }

    std::int64_t historyWriteEnd() const noexcept { return historyEnd.load (std::memory_order_acquire); }

    RenderReport render (const Ring& ring, std::uint64_t pairKey, std::uint32_t sampleRate,
                         const BlockClock& block, float* const* io, int channels,
                         bool preSelected, float gain) noexcept
    {
        RenderReport report;
        if (block.frames <= 0 || block.frames > capacity || channels <= 0 || channels > 2 || io == nullptr)
        {
            weight = 0.0f;
            report.preWaiting = preSelected;
            return report;
        }
        recordHistory (block, io, channels);
        float* scratch[] = { left.get(), right.get() };
        const auto decision = consumer.process (ring, pairKey, sampleRate, block, scratch, 2);
        matchOffset.store (decision.k, std::memory_order_release);
        matchOffsetValid.store (decision.kValid, std::memory_order_release);
        report.verdict = decision.verdict;
        if (decision.verdict != Verdict::accepted)
        {
            weight = 0.0f; // switch to POST at the block start
            report.preWaiting = preSelected;
            return report;
        }
        const float target = preSelected ? 1.0f : 0.0f;
        if (weight == 0.0f && target == 0.0f)
            return report;
        const float step = 1.0f / static_cast<float> (fadeFrames);
        for (std::int32_t i = 0; i < block.frames; ++i)
        {
            weight = weight < target ? std::min (target, weight + step) : std::max (target, weight - step);
            for (int channel = 0; channel < channels; ++channel)
            {
                const float pre = scratch[std::min (channel, 1)][i] * gain;
                io[channel][i] = io[channel][i] * (1.0f - weight) + pre * weight;
            }
        }
        report.preAudible = weight > 0.0f;
        return report;
    }

    // Audio Thread: the host stopped processing the session (end, format change).
    void silenceTransition() noexcept { weight = 0.0f; }

private:
    static constexpr double fadeSeconds = 0.005; // the symmetric transition of Local Blind (INV-S25)
    static constexpr double historySeconds = 8.0; // MATCH reads up to 4 s of it

    // Audio Thread: the POST input (A, before any output mixing) indexed by POST's continuous clock.
    void recordHistory (const BlockClock& block, float* const* io, int channels) noexcept
    {
        if (history == nullptr || ! block.clockValid)
            return;
        const auto end = historyEnd.load (std::memory_order_relaxed);
        if (block.afterGap || ! block.playing || block.clock != end || end == 0)
            historyStart.store (block.clock, std::memory_order_release);
        const auto mask = historyFrames - 1;
        for (std::int32_t i = 0; i < block.frames; ++i)
        {
            const auto slot = static_cast<std::size_t> ((block.clock + i) & mask) * 2;
            history[slot].store (io[0][i], std::memory_order_relaxed);
            history[slot + 1].store (io[std::min (1, channels - 1)][i], std::memory_order_relaxed);
        }
        historyEnd.store (block.clock + block.frames, std::memory_order_release);
    }

    Consumer consumer;
    std::unique_ptr<float[]> left, right;
    int capacity = 0;
    int fadeFrames = 240;
    float weight = 0.0f;
    std::unique_ptr<std::atomic<float>[]> history;
    std::int64_t historyFrames = 0;
    std::atomic<std::int64_t> historyStart { 0 }, historyEnd { 0 }, matchOffset { 0 };
    std::atomic<bool> matchOffsetValid { false };
};
}
