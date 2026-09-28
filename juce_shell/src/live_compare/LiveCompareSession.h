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
    // Non-RT: sizes the scratch for the largest realtime block and the fade length.
    void prepare (int maximumFrames, double sampleRate)
    {
        capacity = std::max (0, maximumFrames);
        left = std::make_unique<float[]> (static_cast<std::size_t> (capacity));
        right = std::make_unique<float[]> (static_cast<std::size_t> (capacity));
        fadeFrames = std::max (1, static_cast<int> (sampleRate * fadeSeconds + 0.5));
        consumer.reset();
        weight = 0.0f;
    }

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
        float* scratch[] = { left.get(), right.get() };
        const auto decision = consumer.process (ring, pairKey, sampleRate, block, scratch, 2);
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

    Consumer consumer;
    std::unique_ptr<float[]> left, right;
    int capacity = 0;
    int fadeFrames = 240;
    float weight = 0.0f;
};
}
