#include "../src/local_blind/CapturePairComparison.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using namespace hypha::local_blind;
static void require (bool value) { if (! value) std::abort(); }

static std::vector<float> deterministicSignal (
    std::int64_t frames, int channels, std::uint32_t seed = 0x12345678u)
{
    std::vector<float> result (static_cast<std::size_t> (frames * channels));
    std::uint32_t state = seed;
    for (auto& sample : result)
    {
        state = state * 1'664'525u + 1'013'904'223u;
        sample = static_cast<float> (static_cast<std::int32_t> (state))
            / static_cast<float> (std::numeric_limits<std::int32_t>::max());
    }
    return result;
}

int main()
{
    constexpr std::int64_t frames = 32'768;
    constexpr int channels = 2;
    const CaptureRange range { 7, 48'000, channels, 100'000, frames };
    const auto pre = deterministicSignal (frames, channels);

    const auto exact = compareCapturePair (range, pre, pre);
    require (exact.valid && exact.exactAtZero && exact.lagEstimated);
    require (exact.bestLagFrames == 0 && exact.bestCorrelation > 0.999999);
    require (exact.normalizedZeroLagRmsError == 0.0);

    constexpr std::int64_t delay = 96;
    std::vector<float> delayed (pre.size(), 0.0f);
    for (std::int64_t frame = 0; frame < frames - delay; ++frame)
        for (int channel = 0; channel < channels; ++channel)
            delayed[static_cast<std::size_t> ((frame + delay) * channels + channel)]
                = pre[static_cast<std::size_t> (frame * channels + channel)];
    const auto shifted = compareCapturePair (range, pre, delayed);
    require (shifted.valid && ! shifted.exactAtZero && shifted.lagEstimated);
    require (shifted.bestLagFrames == delay && shifted.bestCorrelation > 0.999999);
    require (shifted.zeroLagCorrelation < 0.1);

    auto gained = pre;
    for (auto& sample : gained) sample *= 0.5f;
    const auto scaled = compareCapturePair (range, pre, gained);
    require (scaled.valid && ! scaled.exactAtZero && scaled.lagEstimated);
    require (scaled.bestLagFrames == 0 && scaled.bestCorrelation > 0.999999);
    require (std::abs (scaled.normalizedZeroLagRmsError - 0.5) < 1.0e-6);

    const auto unrelated = compareCapturePair (
        range, pre, deterministicSignal (frames, channels, 0x87654321u));
    require (unrelated.valid && ! unrelated.exactAtZero && ! unrelated.lagEstimated);
    require (unrelated.bestCorrelation < 0.9);

    auto nonFinite = pre;
    nonFinite[123] = std::numeric_limits<float>::quiet_NaN();
    require (! compareCapturePair (range, pre, nonFinite).valid);
    require (! compareCapturePair (range, pre, std::vector<float> {}).valid);

    std::cout << "Capture pair comparison: exact, delayed, gained and invalid PCM PASS\n";
}
