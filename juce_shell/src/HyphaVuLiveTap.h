#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include "HyphaVuNeedleMotion.h"

namespace hypha::hybrid_vu
{
// Needle-only full-wave rectification and moving-coil response. Meter Session, Capture,
// Record, footer and calibration keep their existing Rust measurements and bytes.
// prepare() computes the sample transition; push() only reads input and publishes atomics.
class LiveTap
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate, int /*maxBlockFrames*/)
    {
        rate = sampleRate;
        sampleStep = NeedleStep::forSeconds (std::isfinite (rate) && rate >= 8'000.0 ? 1.0 / rate : 0.0);
        tracking = false;
        owedNanos = lastNanos = 0;
        lastHasPosition = false;
        seenSpan = span.load (std::memory_order_acquire);
        reset();
    }

    // Every change starts a new input span, including off -> on between two callbacks.
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

    // Discontinuities end the input span, never freeze the physical state or substitute a
    // stale 100 ms meter publication. A callback gap is zero input for its elapsed duration.
    void push (const float* interleaved, int frames, int channels, bool playing, bool hasPosition,
               std::int64_t position, std::uint64_t callbackNanos) noexcept
    {
        if (! wanted.load (std::memory_order_acquire)) { interrupt(); return; }
        if (interleaved == nullptr || frames <= 0 || channels <= 0 || ! std::isfinite (rate) || rate < 8'000.0) return;
        const int tapped = std::min (channels, maxChannels);
        const auto currentSpan = span.load (std::memory_order_acquire);
        bool continuous = tracking && currentSpan == seenSpan && tapped == activeChannels && playing == lastPlaying;
        if (continuous && playing && hasPosition && lastHasPosition && position != expectedPosition) continuous = false;
        if (callbackNanos != 0 && lastNanos != 0)
        {
            // The wall time owed is the previous block plus whatever earlier blocks ran ahead of real
            // time, so variable-length or bursty callbacks continue and only a real gap advances the zero-input response.
            const std::uint64_t allowance = owedNanos + lastBlockNanos;
            const std::uint64_t elapsed = callbackNanos > lastNanos ? callbackNanos - lastNanos : 0;
            // Beyond that endpoint the UI already coasts, even without a stop or position
            // notification. Integrate the same gap here so resumption cannot undo its return.
            if (elapsed > allowance) coast (elapsed - allowance);
            owedNanos = std::min (allowance - std::min (allowance, elapsed), windowNanos);
        }
        if (! continuous) owedNanos = 0;
        tracking = true;
        seenSpan = currentSpan;
        activeChannels = tapped;
        lastPlaying = playing;
        lastHasPosition = hasPosition;
        expectedPosition = position + frames;
        lastNanos = callbackNanos;
        lastBlockNanos = toNanos (frames);
        finiteInput = true;
        for (int frame = 0; frame < frames; ++frame)
            for (int channel = 0; channel < tapped; ++channel)
                finiteInput = finiteInput && std::isfinite (interleaved[(size_t) frame * (size_t) channels + (size_t) channel]);
        for (int frame = 0; frame < frames; ++frame)
        {
            for (int channel = 0; channel < tapped; ++channel)
            {
                // An invalid block is zero input for its FULL duration on both needles.
                // Never publish the unchanged last state as a new observation each callback.
                const float magnitude = finiteInput ? std::fabs (interleaved[static_cast<std::size_t> (frame) * static_cast<std::size_t> (channels) + static_cast<std::size_t> (channel)]) : 0.0f;
                const double input = magnitude < 1.0e-30f ? 0.0 : static_cast<double> (magnitude) * 1.5707963267948966;
                sampleStep.apply (states[static_cast<std::size_t> (channel)], input);
            }
        }
        for (int channel = tapped; channel < maxChannels; ++channel) states[(size_t) channel] = {};
        stateNanos = callbackNanos;
        stateAheadNanos = owedNanos + lastBlockNanos;
        publish();
    }

    // Publish the preserved position AND velocity once. The UI continues the same zero-input
    // response even in hosts which stop calling processBlock altogether.
    void interrupt() noexcept
    {
        const bool wasTracking = tracking;
        tracking = false;
        // Keep the processed audio horizon: resumption and the UI coast from the same endpoint.
        if (wasTracking) publish();
    }

    LiveReading reading() const noexcept
    {
        LiveReading result;
        const auto before = generation.load();
        if ((before & 1u) != 0) return result; // bounded read, never spin on the audio writer
        for (int channel = 0; channel < maxChannels; ++channel)
            result.needles[(size_t) channel] = {
                fromBits (published[(size_t) channel].load()),
                fromBits (velocities[(size_t) channel].load()) };
        result.channels = publishedChannels.load();
        result.feeding = publishedFeeding.load();
        result.aheadSeconds = fromBits (publishedDuration.load());
        result.wallNanos = publishedNanos.load();
        result.generation = before;
        result.coherent = before == generation.load();
        return result;
    }
    std::array<double, maxChannels> read() const noexcept
    {
        const auto snapshot = reading();
        std::array<double, maxChannels> result {};
        for (int channel = 0; channel < maxChannels; ++channel)
            result[(size_t) channel] = snapshot.coherent && snapshot.feeding && channel < snapshot.channels
                ? needleDbfs (snapshot.needles[(size_t) channel].value) : -INFINITY;
        return result;
    }

    double level (int channel) const noexcept
    {
        return channel >= 0 && channel < maxChannels ? read()[(size_t) channel] : -INFINITY;
    }

    // Advances once per published block (a test can confirm the publication interval).
    std::uint64_t publications() const noexcept { return generation.load (std::memory_order_acquire) / 2; }

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
    void coast (std::uint64_t nanos) noexcept
    {
        // Powers of the prepare-time transition: no transcendentals or new coefficients in RT.
        if (nanos > 10'000'000'000ull) { for (auto& state : states) state = {}; return; }
        auto frames = static_cast<std::uint64_t> (static_cast<double> (nanos) * rate * 1.0e-9);
        auto step = sampleStep;
        while (frames != 0)
        {
            if ((frames & 1u) != 0) for (auto& state : states) step.apply (state, 0.0);
            step = { step.a * step.a + step.b * step.c, step.b * (step.a + step.d),
                     step.c * (step.a + step.d), step.c * step.b + step.d * step.d };
            frames >>= 1u;
        }
    }
    void reset() noexcept
    {
        for (auto& state : states) state = {};
        activeChannels = 0;
        lastBlockNanos = 0;
        stateNanos = stateAheadNanos = 0;
        publish();
    }
    void publish() noexcept
    {
        generation.fetch_add (1);
        for (int channel = 0; channel < maxChannels; ++channel)
        {
            published[(size_t) channel].store (toBits (states[(size_t) channel].value));
            velocities[(size_t) channel].store (toBits (states[(size_t) channel].velocity));
        }
        publishedChannels.store (activeChannels);
        publishedFeeding.store (tracking && finiteInput);
        publishedDuration.store (toBits (static_cast<double> (stateAheadNanos) * 1.0e-9));
        publishedNanos.store (stateNanos);
        generation.fetch_add (1);
    }

    std::array<NeedleState, maxChannels> states {};
    NeedleStep sampleStep;
    int activeChannels = 0;
    double rate = 0.0;
    bool tracking = false, finiteInput = true, lastPlaying = false, lastHasPosition = false;
    std::int64_t expectedPosition = 0;
    std::uint64_t lastNanos = 0, lastBlockNanos = 0, owedNanos = 0, seenSpan = 0;
    std::uint64_t stateNanos = 0, stateAheadNanos = 0;
    static constexpr std::uint64_t windowNanos = 300'000'000ull; // cap on wall time owed
    std::atomic<bool> wanted { false };
    std::atomic<std::uint64_t> span { 0 };          // bumped by every enable change (message thread)
    std::array<std::atomic<std::uint64_t>, maxChannels> published {}, velocities {};
    std::atomic<int> publishedChannels { 0 };
    std::atomic<bool> publishedFeeding { false };
    std::atomic<std::uint64_t> publishedDuration { 0 }, publishedNanos { 0 };
    std::atomic<std::uint64_t> generation { 0 };
};
static_assert (std::atomic<std::uint64_t>::is_always_lock_free, "VU live publication must be lock-free");
}
