#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace hypha::hybrid_vu
{
// The Hybrid VU needle used to follow the meter publication that the Measure worker drains every
// 100 ms and the editor polls every 100 ms, so it sat 100 to 200 ms behind a standard VU meter.
// This tap integrates the same frames on the audio thread with the same 300 ms full-wave average and
// sine calibration as the Rust meter (stereo_meter.rs) and publishes the result through atomics
// after every block, so the needle can read it at frame rate. The published meter values, Capture,
// footer and calibration are untouched: this is only a faster copy for the needle.
//
// Audio thread: one abs/add/subtract per sample on a ring sized in prepare(); no lock, alloc or IO.
// Message thread: prepare() while the host has suspended processing, setEnabled() from the editor.
class LiveTap
{
public:
    static constexpr double windowSeconds = 0.3;
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate, int /*maxBlockFrames*/)
    {
        rate = sampleRate;
        windowFrames = sampleRate > 0.0 ? static_cast<int> (std::lround (sampleRate * windowSeconds)) : 0;
        tracking = false;
        owedNanos = 0;
        seenSpan = span.load (std::memory_order_acquire);
        for (auto& channel : rings) channel.assign (static_cast<std::size_t> (std::max (windowFrames, 1)), 0.0f);
        for (auto& sum : sums) sum = 0.0;
        cursor = 0;
        covered = 0;
        for (auto& value : published) value.store (nanBits, std::memory_order_relaxed);
        generation.store (0, std::memory_order_release);
    }

    // Every change starts a new span, so an off -> on between two callbacks still ends the window.
    void setEnabled (bool next) noexcept
    {
        if (wanted.load (std::memory_order_acquire) != next) span.fetch_add (1, std::memory_order_acq_rel);
        wanted.store (next, std::memory_order_release);
    }
    bool enabled() const noexcept { return wanted.load (std::memory_order_acquire); }

    // Audio thread only. `interleaved` holds `frames` x `channels` samples as handed to the meter.
    // A test feeds steady, continuous blocks through this form.
    void push (const float* interleaved, int frames, int channels) noexcept
    { push (interleaved, frames, channels, lastPlaying, false, 0, 0); }

    // Audio thread only. The window is one continuous span of the same measurement: a seek (the
    // position is not where the last block ended), a stop or start of playback, a gap between
    // callbacks (bypass, offline, a hidden host) or a change of channel count ends it, the level
    // goes back to NaN (the needle falls back to the 100 ms publication) and a whole new 300 ms must
    // pass before this tap publishes again. A non-finite sample does the same, so the running sum
    // and the ring can never disagree. `callbackNanos` is the callback's wall clock (0 = unknown).
    void push (const float* interleaved, int frames, int channels, bool playing, bool hasPosition,
               std::int64_t position, std::uint64_t callbackNanos) noexcept
    {
        if (! wanted.load (std::memory_order_acquire)) { interrupt(); return; }
        if (interleaved == nullptr || frames <= 0 || channels <= 0 || windowFrames <= 0) return;
        const int tapped = std::min (channels, maxChannels);
        const auto currentSpan = span.load (std::memory_order_acquire);
        bool continuous = tracking && currentSpan == seenSpan && tapped == activeChannels && playing == lastPlaying;
        if (continuous && playing && hasPosition && lastHasPosition && position != expectedPosition) continuous = false;
        if (continuous && callbackNanos != 0 && lastNanos != 0)
        {
            // The wall time owed is the previous block plus whatever earlier blocks ran ahead of real
            // time, so variable-length or bursty callbacks continue and only a real gap ends the window.
            const std::uint64_t allowance = owedNanos + lastBlockNanos;
            const std::uint64_t elapsed = callbackNanos > lastNanos ? callbackNanos - lastNanos : 0;
            if (elapsed > 2ull * allowance + 20'000'000ull) continuous = false;
            else owedNanos = std::min (allowance - std::min (allowance, elapsed), windowNanos);
        }
        if (! continuous) { reset(); owedNanos = 0; }
        tracking = true;
        seenSpan = currentSpan;
        activeChannels = tapped;
        lastPlaying = playing;
        lastHasPosition = hasPosition;
        expectedPosition = position + frames;
        lastNanos = callbackNanos;
        lastBlockNanos = toNanos (frames);
        for (int frame = 0; frame < frames; ++frame)
        {
            for (int channel = 0; channel < tapped; ++channel)
            {
                auto& ring = rings[static_cast<std::size_t> (channel)];
                const float magnitude = std::fabs (interleaved[static_cast<std::size_t> (frame) * static_cast<std::size_t> (channels) + static_cast<std::size_t> (channel)]);
                if (! std::isfinite (magnitude)) { reset(); publish(); return; }
                auto& sum = sums[static_cast<std::size_t> (channel)];
                sum = std::max (0.0, sum - static_cast<double> (ring[static_cast<std::size_t> (cursor)]) + static_cast<double> (magnitude));
                ring[static_cast<std::size_t> (cursor)] = magnitude;
            }
            if (++cursor >= windowFrames) cursor = 0;
            if (covered < windowFrames) ++covered;
        }
        publish();
    }

    // Audio thread only. A callback that does not feed the meter (stopped without monitoring,
    // bypassed, held or oversized) ends the window at once, so a later block cannot continue it
    // even at the same position. Only the first such callback clears and publishes.
    void interrupt() noexcept
    {
        if (covered != 0 || tracking) { reset(); publish(); }
        tracking = false;
        owedNanos = 0;
    }

    // Any thread. dBFS per channel; NaN while disabled, until a whole window was integrated,
    // for a silent window, or for a channel the input does not carry.
    std::array<double, maxChannels> read() const noexcept
    {
        std::array<double, maxChannels> result {};
        for (int channel = 0; channel < maxChannels; ++channel)
            result[static_cast<std::size_t> (channel)] = fromBits (published[static_cast<std::size_t> (channel)].load (std::memory_order_acquire));
        return result;
    }

    double level (int channel) const noexcept
    {
        return channel >= 0 && channel < maxChannels
            ? fromBits (published[static_cast<std::size_t> (channel)].load (std::memory_order_acquire))
            : std::numeric_limits<double>::quiet_NaN();
    }

    // Advances once per published block (a test can confirm the publication interval).
    std::uint64_t publications() const noexcept { return generation.load (std::memory_order_acquire); }

private:
    static std::uint64_t toBits (double value) noexcept
    {
        std::uint64_t bits = 0;
        std::memcpy (&bits, &value, sizeof (bits));
        return bits;
    }
    static double fromBits (std::uint64_t bits) noexcept
    {
        double value = 0.0;
        std::memcpy (&value, &bits, sizeof (value));
        return value;
    }
    std::uint64_t toNanos (int frames) const noexcept
    {
        return rate > 0.0 ? static_cast<std::uint64_t> (static_cast<double> (frames) * 1.0e9 / rate) : 0;
    }
    void reset() noexcept
    {
        for (auto& channel : rings) std::fill (channel.begin(), channel.end(), 0.0f);
        for (auto& sum : sums) sum = 0.0;
        cursor = 0;
        covered = 0;
    }
    void publish() noexcept
    {
        for (int channel = 0; channel < maxChannels; ++channel)
        {
            double dbfs = std::numeric_limits<double>::quiet_NaN();
            if (covered >= windowFrames && channel < activeChannels)
            {
                // Full-wave average calibrated so a sine reads its peak: mean(|sine|) * pi/2 == peak.
                const double calibrated = sums[static_cast<std::size_t> (channel)] / static_cast<double> (windowFrames) * 1.5707963267948966;
                if (calibrated > 0.0)
                {
                    const double candidate = 20.0 * std::log10 (calibrated);
                    if (std::isfinite (candidate)) dbfs = candidate;
                }
            }
            published[static_cast<std::size_t> (channel)].store (toBits (dbfs), std::memory_order_release);
        }
        generation.fetch_add (1, std::memory_order_acq_rel);
    }

    static constexpr std::uint64_t nanBits = 0x7ff8000000000000ull;
    std::array<std::vector<float>, maxChannels> rings;   // sized in prepare(); audio thread owned
    std::array<double, maxChannels> sums {};
    int windowFrames = 0, cursor = 0, covered = 0, activeChannels = 0;
    double rate = 0.0;
    bool tracking = false, lastPlaying = false, lastHasPosition = false; // audio thread owned continuity facts
    std::int64_t expectedPosition = 0;
    std::uint64_t lastNanos = 0, lastBlockNanos = 0, owedNanos = 0, seenSpan = 0;
    static constexpr std::uint64_t windowNanos = 300'000'000ull; // cap on wall time owed
    std::atomic<bool> wanted { false };
    std::atomic<std::uint64_t> span { 0 };          // bumped by every enable change (message thread)
    std::array<std::atomic<std::uint64_t>, maxChannels> published { { { nanBits }, { nanBits } } };
    std::atomic<std::uint64_t> generation { 0 };
};
static_assert (std::atomic<std::uint64_t>::is_always_lock_free, "VU live publication must be lock-free");
}
