#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace hypha::local_blind
{
// A single-use ingress building block, not permission to start an audition. A non-RT owner must
// first prove the exact pair, common capture barrier, scope and time mapping for each side.
// No trim, resampling, guessed PDC, gap filling or independent loop is performed here.
struct CaptureRange
{
    std::uint64_t generation = 0;
    std::uint32_t sampleRate = 0;
    int channels = 0;
    std::int64_t start = 0;
    std::int64_t frames = 0;
};

enum class CaptureState : unsigned char { pending, complete, invalid };
enum class CaptureFailure : unsigned char { none, discontinuity, format, generation, nonRealtime, nonFinite };

class ExactRangeCapture final
{
public:
    // Construct and destroy on non-RT only. Retirement requires the producer's exit acknowledgement;
    // cancel is not that acknowledgement. No reset/reallocation while a producer can reference this.
    ExactRangeCapture (CaptureRange range, std::size_t byteBudget)
        : expected (range), samples (checkedSamples (range, byteBudget), 0.0f) {}

    ExactRangeCapture (const ExactRangeCapture&) = delete;
    ExactRangeCapture& operator= (const ExactRangeCapture&) = delete;

    // One producer only; all arguments describe the native interval, including genuine zero PCM.
    // Returns no PCM to a consumer until the whole half-open range is complete and finite.
    void push (const float* const* input, int channels, int frames, std::int64_t position,
               std::uint64_t generation, bool realtime, std::uint32_t sampleRate) noexcept
    {
        if (state() != CaptureState::pending) return;
        if (! realtime) { fail (CaptureFailure::nonRealtime); return; }
        if (generation != expected.generation) { fail (CaptureFailure::generation); return; }
        if (channels != expected.channels || sampleRate != expected.sampleRate || frames < 1 || input == nullptr)
        { fail (CaptureFailure::format); return; }
        for (int c = 0; c < channels; ++c)
            if (input[c] == nullptr) { fail (CaptureFailure::format); return; }
        if (position > std::numeric_limits<std::int64_t>::max() - frames)
        { fail (CaptureFailure::discontinuity); return; }
        const auto end = position + frames;
        const auto next = expected.start + captured;
        if (end <= expected.start && captured == 0) return;
        if (position > next || (captured != 0 && position != next))
        { fail (CaptureFailure::discontinuity); return; }
        // position <= next < end, so this subtraction cannot exceed this bounded block's frames.
        const auto offset = static_cast<int> (next - position);
        const auto count = std::min<std::int64_t> (frames - offset, expected.frames - captured);
        for (std::int64_t f = 0; f < count; ++f)
            for (int c = 0; c < channels; ++c)
            {
                const auto value = input[c][offset + f];
                if (! std::isfinite (value)) { fail (CaptureFailure::nonFinite); return; }
                samples[static_cast<std::size_t> ((captured + f) * channels + c)] = value;
            }
        captured += count;
        if (captured == expected.frames)
        {
            auto pending = CaptureState::pending;
            // Cancellation wins even if it arrives in the middle of this bounded copy.
            status.compare_exchange_strong (pending, CaptureState::complete, std::memory_order_release);
        }
    }

    void cancel() noexcept { cancelled.store (true, std::memory_order_release); }
    CaptureState state() const noexcept
    { return cancelled.load (std::memory_order_acquire) ? CaptureState::invalid
                                                       : status.load (std::memory_order_acquire); }
    CaptureFailure failure() const noexcept { return reason.load (std::memory_order_acquire); }
    const CaptureRange& range() const noexcept { return expected; }
    std::size_t allocatedBytes() const noexcept { return samples.size() * sizeof (float); }
    const std::vector<float>* completedPcm() const noexcept
    { return state() == CaptureState::complete ? &samples : nullptr; }

private:
    const CaptureRange expected;
    std::vector<float> samples;
    std::int64_t captured = 0; // producer-owned; consumers only access after complete
    std::atomic<CaptureState> status { CaptureState::pending };
    std::atomic<CaptureFailure> reason { CaptureFailure::none };
    std::atomic<bool> cancelled { false };
    static_assert (std::atomic<CaptureState>::is_always_lock_free);
    static_assert (std::atomic<CaptureFailure>::is_always_lock_free);
    static_assert (std::atomic<bool>::is_always_lock_free);

    void fail (CaptureFailure value) noexcept
    {
        reason.store (value, std::memory_order_relaxed);
        status.store (CaptureState::invalid, std::memory_order_release);
    }
    static std::size_t checkedSamples (CaptureRange range, std::size_t budget)
    {
        if (range.generation == 0 || range.sampleRate < 8'000 || range.sampleRate > 768'000
            || (range.channels != 1 && range.channels != 2) || range.frames < 1
            || range.start > std::numeric_limits<std::int64_t>::max() - range.frames
            || static_cast<std::uint64_t> (range.frames) > budget / sizeof (float) / range.channels)
            throw std::invalid_argument ("Invalid capture range or capacity");
        return static_cast<std::size_t> (range.frames) * range.channels;
    }
};
}
