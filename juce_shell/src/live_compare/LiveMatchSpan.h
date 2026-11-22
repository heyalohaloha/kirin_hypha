#pragma once
#include "LiveMatchSpanQueue.h"
#include "../../../crates/kirin_hypha_ffi/include/kirin_hypha_live_match_span.h"
#include <array>
#include <utility>

namespace hypha::live_compare
{
// Non-RT lifetime is owned by PostRenderer's existing publication lease. The realtime leg
// only transfers validated raw PCM; the integrated histogram and true peak are processed here.
class LiveMatchSpan
{
public:
    void prepare(bool enabled, std::uint32_t newRate)
    {
        clear();
        rate = newRate;
        queue = enabled ? std::make_unique<LiveMatchSpanQueue>(std::max<std::uint64_t>(32768, newRate * 2ull)) : nullptr;
        if (enabled) restart();
    }

    void restart() noexcept
    {
        meter.reset(); readEpoch = 0; meterChannels = 0; consumed = 0; invalid = false;
        epoch.store(queue ? ++serial : 0, std::memory_order_release);
    }

    void suspend() noexcept { epoch.store(0, std::memory_order_release); }

    void clear() noexcept
    {
        suspend();
        meter.reset(); readEpoch = 0; meterChannels = 0; consumed = 0; invalid = false;
    }

    bool enabled() const noexcept { return epoch.load(std::memory_order_acquire) != 0; }

    void capture(const Ring& ring, std::uint64_t run, std::int64_t start, float* const* post,
                 int channels, int frames) noexcept
    {
        const auto captureEpoch = epoch.load(std::memory_order_acquire);
        if (captureEpoch != 0)
            queue->push(ring, run, start, post, channels, frames, captureEpoch);
    }

    void service(int channels)
    {
        const auto ready = epoch.load(std::memory_order_acquire);
        if (ready == 0 || channels < 1 || channels > 2) return;
        if (readEpoch != ready)
        {
            readEpoch = ready; meterChannels = channels;
            meter = Meter(kirin_hypha_live_match_span_create(rate, static_cast<std::uint32_t>(channels)),
                          &kirin_hypha_live_match_span_destroy);
        }
        if (channels != meterChannels || !meter || queue->incomplete(ready)) invalid = true;
        std::size_t frames = 0;
        auto flush = [&] {
            if (frames == 0) return;
            if (!invalid && !kirin_hypha_live_match_span_feed(meter.get(), pre.data(), post.data(), frames)) invalid = true;
            consumed += frames; frames = 0;
        };
        queue->drain(maxDrain, [&](const LiveMatchSpanQueue::Frame& frame) {
            if (invalid || frame.epoch != ready) return;
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto at = frames * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch);
                pre[at] = frame.pre[ch]; post[at] = frame.post[ch];
            }
            if (++frames == chunkFrames) flush();
        });
        flush();
    }

    bool lost() const noexcept { return invalid || (queue && queue->incomplete(epoch.load(std::memory_order_acquire))); }
    bool enough() const noexcept { return consumed >= static_cast<std::uint64_t>(rate) * 3; }
    std::uint64_t frames() const noexcept { return consumed; }
    bool measure(KirinLiveMatchSpanFacts& facts) const noexcept
    {
        return !lost() && queue && queue->empty() && meter && kirin_hypha_live_match_span_measure(meter.get(), &facts);
    }

private:
    static constexpr std::size_t chunkFrames = 2048;
    static constexpr std::size_t maxDrain = 65536;
    using Meter = std::unique_ptr<KirinLiveMatchSpan, void (*)(KirinLiveMatchSpan*)>;
    std::unique_ptr<LiveMatchSpanQueue> queue;
    Meter meter { nullptr, nullptr };
    std::atomic<std::uint64_t> epoch {0};
    std::uint64_t serial = 0, readEpoch = 0, consumed = 0;
    std::uint32_t rate = 0;
    int meterChannels = 0;
    bool invalid = false;
    std::array<float,chunkFrames * 2> pre {}, post {};
};
}
