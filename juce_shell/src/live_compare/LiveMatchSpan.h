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

    void restart(std::uint64_t requiredBlindReceipt = 0) noexcept
    {
        meter.reset(); readEpoch = 0; meterChannels = 0; consumed = 0; invalid = false;
        if (queue) queue->discardPublished();
        blindReceipt.store(requiredBlindReceipt, std::memory_order_relaxed);
        epoch.store(queue ? ++serial : 0, std::memory_order_release);
    }

    void suspend() noexcept { epoch.store(0, std::memory_order_release); }

    void clear() noexcept
    {
        suspend();
        meter.reset(); readEpoch = 0; meterChannels = 0; consumed = 0; invalid = false;
    }

    bool enabled() const noexcept { return epoch.load(std::memory_order_acquire) != 0; }

    struct CaptureToken { std::uint64_t epoch = 0, receipt = 0; };
    CaptureToken captureToken() const noexcept
    {
        return {epoch.load(std::memory_order_acquire), blindReceipt.load(std::memory_order_relaxed)};
    }

    bool current(CaptureToken token) const noexcept
    { return token.epoch != 0 && token.epoch == epoch.load(std::memory_order_acquire); }

    // Only dereference queue while the realtime ring lease protects its non-RT replacement.
    LiveMatchSpanQueue::Staged stage(CaptureToken token, const Ring& ring, std::uint64_t run, std::int64_t start,
          float* const* post, int channels, int frames, bool discontinuity) noexcept
    {
        return !current(token) || !queue ? LiveMatchSpanQueue::Staged {} : queue->stage(ring, run, start, post,
            channels, frames, token.epoch, skipped || lastCapturedEpoch != token.epoch || discontinuity);
    }

    void finish(CaptureToken token, LiveMatchSpanQueue::Staged staged, bool eligible) noexcept
    {
        if (!current(token)) return;
        skipped = !eligible;
        if (eligible && queue)
        {
            queue->finish(staged, token.epoch);
            lastCapturedEpoch = token.epoch;
        }
    }

    CaptureToken admitted(CaptureToken token, bool eligible, std::uint64_t callbackRequest,
                          std::uint64_t acceptedRequest) noexcept
    {
        if (eligible && current(token) && queue && (token.receipt == 0 || (token.receipt == callbackRequest && token.receipt == acceptedRequest)))
            return token;
        finish(token, {}, false);
        return {};
    }

    struct BlockCapture
    {
        LiveMatchSpan& span;
        CaptureToken token;
        LiveMatchSpanQueue::Staged staged {};
        bool eligible = false;
        ~BlockCapture() { span.finish(token, staged, eligible); }
    };

    using Cut = bool (*)(KirinLiveMatchSpan*);
    std::uint64_t snapshot() const noexcept { return queue ? queue->published() : 0; }
    void service(int channels, Cut cut = nullptr) { serviceThrough(channels, snapshot(), maxDrain, cut); }

    bool serviceThrough(int channels, std::uint64_t target, std::uint64_t limit, Cut cut = nullptr)
    {
        const auto ready = epoch.load(std::memory_order_acquire);
        if (ready == 0 || channels < 1 || channels > 2) return false;
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
        const bool done = queue->drainThrough(target, limit, [&](const LiveMatchSpanQueue::Frame& frame) {
            if (invalid || frame.epoch != ready) return;
            if (frame.gapBefore && (consumed != 0 || frames != 0) && cut != nullptr)
            {
                flush();
                if (!invalid && !cut(meter.get())) invalid = true;
                if (invalid) return;
            }
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto at = frames * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch);
                pre[at] = frame.pre[ch]; post[at] = frame.post[ch];
            }
            if (++frames == chunkFrames) flush();
        });
        flush();
        return done;
    }

    bool lost() const noexcept { return invalid || (queue && queue->incomplete(epoch.load(std::memory_order_acquire))); }
    bool enough() const noexcept { return consumed >= static_cast<std::uint64_t>(rate) * 3; }
    std::uint64_t frames() const noexcept { return consumed; }
    bool measure(KirinLiveMatchSpanFacts& facts) const noexcept
    {
        return !lost() && meter && kirin_hypha_live_match_span_measure(meter.get(), &facts) && !lost();
    }

private:
    static constexpr std::size_t chunkFrames = 2048;
    static constexpr std::size_t maxDrain = 65536;
    using Meter = std::unique_ptr<KirinLiveMatchSpan, void (*)(KirinLiveMatchSpan*)>;
    std::unique_ptr<LiveMatchSpanQueue> queue;
    Meter meter { nullptr, nullptr };
    std::atomic<std::uint64_t> epoch {0}, blindReceipt {0};
    std::uint64_t lastCapturedEpoch = 0; // audio writer only
    bool skipped = false;
    std::uint64_t serial = 0, readEpoch = 0, consumed = 0;
    std::uint32_t rate = 0;
    int meterChannels = 0;
    bool invalid = false;
    std::array<float,chunkFrames * 2> pre {}, post {};
};
}
