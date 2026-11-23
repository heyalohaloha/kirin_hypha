#pragma once
#include "LiveMatchSpanQueue.h"
#include "../../../crates/kirin_hypha_ffi/include/kirin_hypha_live_match_span.h"
#include <array>
#include <utility>

namespace hypha::live_compare
{
#ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
inline void (*afterWholeMatchTokenForTest)() = nullptr;
#endif
// Non-RT lifetime is owned by PostRenderer's existing publication lease. The realtime leg
// only transfers validated raw PCM; the integrated histogram and true peak are processed here.
class LiveMatchSpan
{
public:
    // The channel count is the format LISTEN started in. It stays with this accumulation until the
    // next prepare: a bus the host renegotiates before its own prepareToPlay never relabels it.
    void prepare(bool enabled, std::uint32_t newRate, int newChannels)
    {
        clear();
        rate = newRate; channels = newChannels;
        enabled = enabled && channels >= 1 && channels <= 2;
        queue = enabled ? std::make_unique<LiveMatchSpanQueue>(std::max<std::uint64_t>(32768, newRate * 2ull)) : nullptr;
        if (enabled) restart();
    }

    void restart() noexcept
    {
        meter.reset(); readEpoch = 0; consumed = 0; invalid = false;
        if (queue) queue->discardPublished();
        epoch.store(queue ? ++serial : 0, std::memory_order_release);
    }

    void suspend() noexcept { epoch.store(0, std::memory_order_release); }

    void clear() noexcept
    {
        suspend();
        meter.reset(); readEpoch = 0; consumed = 0; invalid = false;
    }

    bool enabled() const noexcept { return epoch.load(std::memory_order_acquire) != 0; }

    struct CaptureToken { std::uint64_t epoch = 0; };
    CaptureToken captureToken() const noexcept
    {
        return {epoch.load(std::memory_order_acquire)};
    }

    bool current(CaptureToken token) const noexcept
    { return token.epoch != 0 && token.epoch == epoch.load(std::memory_order_acquire); }

    // Only dereference queue while the realtime ring lease protects its non-RT replacement.
    LiveMatchSpanQueue::Staged stage(CaptureToken token, const Ring& ring, std::uint64_t run, std::int64_t start,
          float* const* post, int blockChannels, int frames, bool discontinuity, std::uint64_t expectedClock = UINT64_MAX) noexcept
    {
        return !current(token) || !queue || blockChannels != channels ? LiveMatchSpanQueue::Staged {} : queue->stage(ring, run, start, post,
            channels, frames, token.epoch, skipped || lastCapturedEpoch != token.epoch || discontinuity, expectedClock);
    }

    void finish(CaptureToken token, LiveMatchSpanQueue::Staged staged, bool eligible) noexcept
    {
        if (!current(token)) return;
        skipped = !eligible || !staged.copied;
        if (eligible && queue)
        {
            queue->finish(staged, token.epoch);
            lastCapturedEpoch = token.epoch;
        }
    }

    CaptureToken admitted(CaptureToken token, bool eligible) noexcept
    {
        if (eligible && current(token) && queue)
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
    void service(Cut cut = nullptr) { serviceThrough(snapshot(), maxDrain, cut); }

    bool serviceThrough(std::uint64_t target, std::uint64_t limit, Cut cut = nullptr)
    {
        const auto ready = epoch.load(std::memory_order_acquire);
        if (ready == 0) return false;
        if (readEpoch != ready)
        {
            readEpoch = ready;
            meter = Meter(kirin_hypha_live_match_span_create(rate, static_cast<std::uint32_t>(channels)),
                          &kirin_hypha_live_match_span_destroy);
        }
        if (!meter || queue->incomplete(ready)) invalid = true;
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
    std::atomic<std::uint64_t> epoch {0};
    std::uint64_t lastCapturedEpoch = 0; // audio writer only
    bool skipped = false;
    std::uint64_t serial = 0, readEpoch = 0, consumed = 0;
    std::uint32_t rate = 0;
    int channels = 0; // fixed by prepare for the whole accumulation
    bool invalid = false;
    std::array<float,chunkFrames * 2> pre {}, post {};
};
}
