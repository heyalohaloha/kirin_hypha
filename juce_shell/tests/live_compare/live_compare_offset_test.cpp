#include "live_compare_offset_test.h"

#include "../../src/live_compare/LiveCompareOffset.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

// INV-LC7: the content offset between POST and PRE as the clocks map them. A delayed copy is found
// to the sample; EQ, level and polarity do not move it; silence, a pure tone and an echo claim
// nothing wrong.
namespace
{
using namespace hypha::live_compare;

void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "FAIL: " << message << '\n'; std::abort(); }
}

float noise (std::int64_t index)
{
    auto z = static_cast<std::uint64_t> (index) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (static_cast<float> (z >> 40) / 16777216.0f - 0.5f) * 0.2f;
}

constexpr std::int64_t lag = 1024;
constexpr std::int64_t frames = 8192;

// POST is `content` at n; PRE's window starts `lag` before POST and runs `d` samples ahead of it.
template <typename Content>
OffsetEstimate estimate (Content content, std::int64_t d, float postScale = 1.0f)
{
    std::vector<float> post (static_cast<std::size_t> (frames)), pre (static_cast<std::size_t> (frames + 2 * lag));
    for (std::int64_t n = 0; n < frames; ++n)
        post[static_cast<std::size_t> (n)] = postScale * content (n);
    for (std::int64_t i = 0; i < frames + 2 * lag; ++i)
        pre[static_cast<std::size_t> (i)] = content (i - lag + d);
    return estimateOffset (post, pre, lag);
}
}

namespace hypha::tests
{
void verifyLiveCompareOffset()
{
    const auto broadband = [] (std::int64_t n) { return noise (n); };
    for (const std::int64_t d : { std::int64_t (0), std::int64_t (240), std::int64_t (-700), std::int64_t (900) })
    {
        const auto e = estimate (broadband, d);
        std::printf ("offset: PRE ahead by %lld -> lag %lld (peak %.3f, dominance %.1f)\n",
                     static_cast<long long> (d), static_cast<long long> (e.lagFrames), e.peak, e.dominance);
        require (e.determined && e.lagFrames == -d, "a delayed copy is found to the sample");
    }

    // A zero-phase EQ, a level change and a polarity inversion keep the peak at the mapping.
    const auto smoothed = [] (std::int64_t n) { return 0.25f * noise (n - 1) + 0.5f * noise (n) + 0.25f * noise (n + 1); };
    std::vector<float> post (static_cast<std::size_t> (frames)), pre (static_cast<std::size_t> (frames + 2 * lag));
    for (std::int64_t n = 0; n < frames; ++n)
        post[static_cast<std::size_t> (n)] = -0.3f * smoothed (n);
    for (std::int64_t i = 0; i < frames + 2 * lag; ++i)
        pre[static_cast<std::size_t> (i)] = noise (i - lag);
    const auto processed = estimateOffset (post, pre, lag);
    require (processed.determined && processed.lagFrames == 0, "EQ, level and polarity do not move the peak");

    // Nothing to say: silence, a pure tone (every period looks alike), an echo as strong as the direct.
    require (! estimate ([] (std::int64_t) { return 0.0f; }, 0).determined, "silence is undetermined");
    const auto tone = estimate ([] (std::int64_t n) { return 0.3f * std::sin (0.1308996939f * static_cast<float> (n)); }, 300);
    std::printf ("offset: pure tone -> determined %d, lag %lld (peak %.3f, dominance %.2f)\n", tone.determined ? 1 : 0,
                 static_cast<long long> (tone.lagFrames), tone.peak, tone.dominance);
    require (! tone.determined, "a pure tone is undetermined");
    const auto echo = [] (std::int64_t n) { return noise (n) + noise (n - 500); };
    for (std::int64_t n = 0; n < frames; ++n)
        post[static_cast<std::size_t> (n)] = echo (n);
    for (std::int64_t i = 0; i < frames + 2 * lag; ++i)
        pre[static_cast<std::size_t> (i)] = noise (i - lag);
    const auto echoed = estimateOffset (post, pre, lag);
    require (! echoed.determined || echoed.lagFrames == 0 || echoed.lagFrames == 500,
             "an echo never claims an offset it does not have");

    // The product window: a 32768-frame window and a +/-8192 search on the message thread.
    std::vector<float> longPost (static_cast<std::size_t> (offsetWindowFrames));
    std::vector<float> longPre (static_cast<std::size_t> (offsetWindowFrames + 2 * offsetSearchFrames));
    for (std::int64_t n = 0; n < offsetWindowFrames; ++n)
        longPost[static_cast<std::size_t> (n)] = noise (n);
    for (std::int64_t i = 0; i < offsetWindowFrames + 2 * offsetSearchFrames; ++i)
        longPre[static_cast<std::size_t> (i)] = noise (i - offsetSearchFrames + 4096);
    const auto started = std::chrono::steady_clock::now();
    const auto full = estimateOffset (longPost, longPre, offsetSearchFrames);
    const auto elapsed = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count();
    std::printf ("offset: product window -> lag %lld in %.1f ms\n", static_cast<long long> (full.lagFrames), elapsed);
    require (full.determined && full.lagFrames == -4096, "the product window finds 85 ms at 48 kHz");
    std::printf ("live compare offset: all checks passed\n");
}
}
