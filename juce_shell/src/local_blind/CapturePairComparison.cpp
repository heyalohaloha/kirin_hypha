#include "CapturePairComparison.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace hypha::local_blind
{
namespace
{
struct Correlation
{
    bool valid = false;
    double value = 0.0;
};

Correlation correlationAt (const std::vector<float>& pre, const std::vector<float>& post,
                           std::int64_t frames, int channels, std::int64_t lag,
                           std::int64_t targetFrames) noexcept
{
    const auto preStart = std::max<std::int64_t> (0, -lag);
    const auto postStart = std::max<std::int64_t> (0, lag);
    const auto overlap = frames - std::abs (lag);
    if (overlap < 32)
        return {};
    const auto stride = std::max<std::int64_t> (1, overlap / targetFrames);
    long double sumPre = 0.0, sumPost = 0.0;
    long double sumPreSquared = 0.0, sumPostSquared = 0.0, sumProduct = 0.0;
    std::uint64_t count = 0;
    for (std::int64_t frame = 0; frame < overlap; frame += stride)
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto preValue = static_cast<long double> (
                pre[static_cast<std::size_t> ((preStart + frame) * channels + channel)]);
            const auto postValue = static_cast<long double> (
                post[static_cast<std::size_t> ((postStart + frame) * channels + channel)]);
            sumPre += preValue;
            sumPost += postValue;
            sumPreSquared += preValue * preValue;
            sumPostSquared += postValue * postValue;
            sumProduct += preValue * postValue;
            ++count;
        }
    if (count < 32)
        return {};
    const auto n = static_cast<long double> (count);
    const auto covariance = sumProduct - (sumPre * sumPost / n);
    const auto preVariance = sumPreSquared - (sumPre * sumPre / n);
    const auto postVariance = sumPostSquared - (sumPost * sumPost / n);
    const auto denominator = std::sqrt (preVariance * postVariance);
    if (! std::isfinite (static_cast<double> (denominator))
        || denominator <= std::numeric_limits<long double>::epsilon())
        return {};
    const auto value = static_cast<double> (covariance / denominator);
    return { std::isfinite (value), std::clamp (value, -1.0, 1.0) };
}

double normalizedZeroLagRms (const std::vector<float>& pre,
                             const std::vector<float>& post) noexcept
{
    long double error = 0.0, reference = 0.0;
    for (std::size_t index = 0; index < pre.size(); ++index)
    {
        const auto a = static_cast<long double> (pre[index]);
        const auto b = static_cast<long double> (post[index]);
        const auto difference = b - a;
        error += difference * difference;
        reference += a * a;
    }
    if (reference <= std::numeric_limits<long double>::epsilon())
        return 0.0;
    return static_cast<double> (std::sqrt (error / reference));
}
}

CapturePairComparison compareCapturePair (
    CaptureRange range, const std::vector<float>& pre,
    const std::vector<float>& post) noexcept
{
    CapturePairComparison result;
    result.generation = range.generation;
    result.sampleRate = range.sampleRate;
    result.channels = range.channels;
    result.start = range.start;
    result.frames = range.frames;
    if (range.generation == 0 || range.frames < 1
        || (range.channels != 1 && range.channels != 2)
        || range.sampleRate < 8'000 || range.sampleRate > 768'000)
        return result;
    const auto expected = static_cast<std::uint64_t> (range.frames)
        * static_cast<std::uint64_t> (range.channels);
    if (expected > std::numeric_limits<std::size_t>::max()
        || pre.size() != static_cast<std::size_t> (expected) || post.size() != pre.size()
        || ! std::all_of (pre.begin(), pre.end(), [] (float value) { return std::isfinite (value); })
        || ! std::all_of (post.begin(), post.end(), [] (float value) { return std::isfinite (value); }))
        return result;

    result.valid = true;
    result.exactAtZero = std::memcmp (pre.data(), post.data(), pre.size() * sizeof (float)) == 0;
    result.normalizedZeroLagRmsError = normalizedZeroLagRms (pre, post);
    const auto zero = correlationAt (pre, post, range.frames, range.channels, 0, 16'384);
    result.zeroLagCorrelation = zero.valid ? zero.value : 0.0;
    if (result.exactAtZero)
    {
        result.lagEstimated = zero.valid;
        result.bestCorrelation = zero.valid ? zero.value : 1.0;
        return result;
    }

    const auto maximumLag = std::min<std::int64_t> (16'384, range.frames / 4);
    if (maximumLag < 8)
        return result;
    constexpr std::int64_t coarseStep = 8;
    std::int64_t coarseBestLag = 0;
    double coarseBest = -2.0, coarseSecond = -2.0;
    for (std::int64_t lag = -maximumLag; lag <= maximumLag; lag += coarseStep)
    {
        const auto current = correlationAt (pre, post, range.frames, range.channels, lag, 4'096);
        if (! current.valid)
            continue;
        if (current.value > coarseBest)
        {
            coarseSecond = coarseBest;
            coarseBest = current.value;
            coarseBestLag = lag;
        }
        else if (current.value > coarseSecond)
            coarseSecond = current.value;
    }
    if (coarseBest < -1.0)
        return result;

    std::int64_t bestLag = coarseBestLag;
    double best = -2.0;
    for (std::int64_t lag = std::max (-maximumLag, coarseBestLag - coarseStep);
         lag <= std::min (maximumLag, coarseBestLag + coarseStep); ++lag)
    {
        const auto current = correlationAt (pre, post, range.frames, range.channels, lag, 16'384);
        const bool tied = current.valid
            && std::abs (current.value - best) <= std::numeric_limits<double>::epsilon();
        if (current.valid && (current.value > best
            || (tied && std::abs (lag) < std::abs (bestLag))))
        {
            best = current.value;
            bestLag = lag;
        }
    }
    if (best < -1.0)
        return result;
    result.bestLagFrames = bestLag;
    result.bestCorrelation = best;
    result.correlationMargin = std::max (0.0, coarseBest - coarseSecond);
    // A maximum always exists for unrelated finite PCM too. Do not present that location as a
    // measured residual unless the two captured signals actually have strong linear agreement.
    result.lagEstimated = best >= 0.9;
    return result;
}
}
