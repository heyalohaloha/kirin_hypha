#include "LiveCompareOffset.h"
#include "LiveCompareWindows.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <limits>

namespace hypha::live_compare
{
namespace
{
using Complex = std::complex<double>;

// Iterative radix-2 FFT; size is a power of two. The inverse is scaled by 1 / size. One twiddle
// table per call keeps it to a few milliseconds at 65536 points.
void transform (std::vector<Complex>& a, bool inverse)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    constexpr double pi = 3.14159265358979323846;
    std::vector<Complex> twiddles (n / 2);
    for (std::size_t k = 0; k < n / 2; ++k)
        twiddles[k] = std::polar (1.0, (inverse ? 2.0 : -2.0) * pi * static_cast<double> (k) / static_cast<double> (n));
    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        const std::size_t stride = n / length;
        for (std::size_t start = 0; start < n; start += length)
            for (std::size_t k = 0; k < length / 2; ++k)
            {
                const Complex u = a[start + k];
                const Complex v = a[start + k + length / 2] * twiddles[k * stride];
                a[start + k] = u + v;
                a[start + k + length / 2] = u - v;
            }
    }
    if (inverse)
        for (auto& value : a)
            value /= static_cast<double> (n);
}

double rms (const std::vector<float>& x)
{
    double sum = 0.0;
    for (const float value : x)
        sum += static_cast<double> (value) * value;
    return x.empty() ? 0.0 : std::sqrt (sum / static_cast<double> (x.size()));
}

std::vector<float> channelSamples (const std::vector<float>& interleaved, std::size_t channel)
{
    std::vector<float> mono (interleaved.size() / 2);
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = interleaved[i * 2 + channel];
    return mono;
}

// A narrow spectrum can still match sampled PCM. A unique full-window match establishes
// its position; repeated alternatives instead require the renderer to check every output
// block independently. Processing differences and silence remain undetermined. Non-RT.
OffsetEstimate scaledCopyOffset (const std::vector<float>& post, const std::vector<float>& pre,
                                 std::int64_t maxLag)
{
    constexpr double precision = 8.0 * std::numeric_limits<float>::epsilon();
    constexpr double tolerance = precision * precision;
    double energy = 0.0, probeEnergy = 0.0;
    for (float value : post)
    {
        if (! std::isfinite (value)) return {};
        energy += static_cast<double> (value) * value;
    }
    for (int i = 0; i < 32; ++i)
    {
        const auto value = static_cast<double> (post[static_cast<std::size_t> (i) * (post.size() - 1) / 31]);
        probeEnergy += value * value;
    }
    if (energy <= 0.0 || probeEnergy <= 0.0) return {};
    std::int64_t found = -1;
    bool repeated = false;
    for (std::int64_t probe = 0; probe <= 2 * maxLag; ++probe)
    {
        // Prefer the closest already available address. Two alternatives suffice to require
        // block checks; do not rescan a full window at every lag of a constant/periodic signal.
        const auto start = probe <= maxLag ? maxLag - probe : probe;
        double cross = 0.0, sourceEnergy = 0.0;
        for (int i = 0; i < 32; ++i)
        {
            const auto index = static_cast<std::size_t> (i) * (post.size() - 1) / 31;
            const auto value = static_cast<double> (pre[static_cast<std::size_t> (start) + index]);
            cross += value * post[index];
            sourceEnergy += value * value;
        }
        if (! std::isfinite (sourceEnergy) || sourceEnergy <= 0.0) continue;
        const auto gain = cross / sourceEnergy;
        if (gain <= 0.0) continue; // a repeated half-period with inverted polarity is not equivalent output
        double error = 0.0;
        for (int i = 0; i < 32; ++i)
        {
            const auto index = static_cast<std::size_t> (i) * (post.size() - 1) / 31;
            const auto delta = post[index] - gain * pre[static_cast<std::size_t> (start) + index];
            error += delta * delta;
        }
        if (! (error <= tolerance * probeEnergy)) continue;
        error = 0.0;
        for (std::size_t i = 0; i < post.size(); ++i)
        {
            const auto delta = post[i] - gain * pre[static_cast<std::size_t> (start) + i];
            error += delta * delta;
        }
        if (! (error <= tolerance * energy)) continue;
        if (found >= 0 && std::llabs (start - found) > 1) { repeated = true; break; }
        if (found < 0) found = start;
    }
    OffsetEstimate result;
    if (found <= 32 || found + 32 >= 2 * maxLag) return result;
    result.determined = true;
    result.verifySamples = repeated;
    result.lagFrames = found - maxLag;
    result.peak = 1.0; // full-window normalized sample agreement, not a spectral peak
    result.dominance = std::numeric_limits<double>::infinity(); // full-window sample agreement
    return result;
}
}

OffsetEstimate estimateOffset (const std::vector<float>& post, const std::vector<float>& pre, std::int64_t maxLag)
{
    OffsetEstimate estimate;
    const auto frames = post.size();
    const auto span = static_cast<std::size_t> (2 * maxLag);
    if (frames < 1024 || maxLag < 1 || pre.size() != frames + span)
        return estimate;
    // About -70 dBFS RMS or less on either side says nothing about an offset.
    if (rms (post) < 3.0e-4 || rms (pre) < 3.0e-4)
        return estimate;
    // Lags 0 to 2 * maxLag never wrap once the transform is as long as PRE's window.
    std::size_t size = 1;
    while (size < pre.size())
        size <<= 1;
    // A Hann window on POST keeps the window's own edges from forming a peak.
    constexpr double pi = 3.14159265358979323846;
    std::vector<Complex> x (size), y (size);
    for (std::size_t i = 0; i < frames; ++i)
        x[i] = post[i] * (0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i) / static_cast<double> (frames - 1)));
    for (std::size_t i = 0; i < pre.size(); ++i)
        y[i] = pre[i];
    transform (x, false);
    transform (y, false);
    // A narrow spectrum needs a full-window sample check instead of a spectral peak.
    std::vector<double> energy (size / 2);
    double total = 0.0;
    for (std::size_t k = 0; k < size / 2; ++k)
        total += energy[k] = std::norm (x[k]);
    std::nth_element (energy.begin(), energy.begin() + static_cast<std::ptrdiff_t> (energy.size() / 100), energy.end(),
                      std::greater<double>());
    double top = 0.0;
    for (std::size_t k = 0; k <= energy.size() / 100; ++k)
        top += energy[k];
    if (total <= 0.0) return estimate;
    if (top > 0.8 * total) return scaledCopyOffset (post, pre, maxLag);
    double meanMagnitude = 0.0;
    for (std::size_t k = 0; k < size; ++k)
    {
        x[k] = std::conj (x[k]) * y[k];
        meanMagnitude += std::abs (x[k]);
    }
    const double floor = 1.0e-3 * meanMagnitude / static_cast<double> (size) + 1.0e-30;
    for (auto& value : x)
        value /= std::abs (value) + floor;
    transform (x, true);
    // x[j] = sum over n of post[n] pre[n + j]; lag = j - maxLag.
    std::size_t best = 0;
    double peak = 0.0;
    for (std::size_t j = 0; j <= span; ++j)
        if (std::fabs (x[j].real()) > peak)
        {
            peak = std::fabs (x[j].real());
            best = j;
        }
    double runnerUp = 0.0;
    for (std::size_t j = 0; j <= span; ++j)
        if ((j > best ? j - best : best - j) > 32)
            runnerUp = std::max (runnerUp, std::fabs (x[j].real()));
    estimate.peak = peak;
    estimate.dominance = runnerUp > 0.0 ? peak / runnerUp : std::numeric_limits<double>::infinity();
    estimate.lagFrames = static_cast<std::int64_t> (best) - maxLag;
    // A peak at the edge of the search is out of range, not an offset.
    const bool inside = best > 32 && best + 32 < span;
    estimate.determined = inside && peak >= 0.05 && estimate.dominance >= 3.0;
    return estimate.determined ? estimate : scaledCopyOffset (post, pre, maxLag);
}

OffsetEstimate measureOffset (const Ring& ring, const PostRenderer& renderer)
{
   #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
    if (beforeOffsetWindowForTest != nullptr) beforeOffsetWindowForTest();
   #endif
    const auto view = renderer.historyView();
    if (view.samples == nullptr || ! view.kValid)
        return {};
    const auto lag = offsetSearchFrames;
    const auto preStart = ring.header.runStart.load (std::memory_order_acquire);
    std::int64_t postEnd = 0, mappedStart = 0, retainedStart = 0, available = 0;
    if (! checkedClockSubtract (view.end, lag, postEnd)
        || ! checkedClockAdd (preStart, view.k, mappedStart)
        || ! checkedClockAdd (mappedStart, lag, mappedStart)
        || ! checkedClockSubtract (view.end, view.frames - 1, retainedStart)) return {};
    const auto availableStart = std::max ({ view.start, retainedStart, mappedStart });
    if (! checkedClockSubtract (postEnd, availableStart, available)) return {};
    const auto frames = std::min (offsetWindowFrames, available);
    if (frames < 4096) return {};
    const auto postStart = postEnd - frames;
    std::int64_t readStart = 0;
    if (! checkedClockSubtract (postStart, view.k, readStart)
        || ! checkedClockSubtract (readStart, lag, readStart)) return {};
    std::vector<float> post (static_cast<std::size_t> (frames) * 2);
    std::vector<float> pre (static_cast<std::size_t> (frames + 2 * lag) * 2);
    copyPostHistory (view, postStart, frames, post);
    if (! copyPreRing (ring, readStart, frames + 2 * lag, pre, view.preRun)
        || ! renderer.historyStillValid (view, postStart))
        return {};
    // Preserve information in either channel, including opposite-polarity stereo. If both
    // channels provide strong but different positions, neither position is safe to adopt.
    const auto rate = static_cast<double> (ring.header.sampleRate.load (std::memory_order_relaxed));
    auto left = estimateOffset (channelSamples (post, 0), channelSamples (pre, 0), lag);
    auto right = estimateOffset (channelSamples (post, 1), channelSamples (pre, 1), lag);
    left.sampleRate = right.sampleRate = rate;
    const bool leftVotes = offsetCanVote (left), rightVotes = offsetCanVote (right);
    auto estimate = rightVotes && !leftVotes ? right : left;
    if (leftVotes && rightVotes
        && std::llabs (left.lagFrames - right.lagFrames) > std::floor (rate * offsetAgreementSeconds))
        estimate.determined = false;
    if (!leftVotes && !rightVotes)
    {
        // Retain the original common-channel evidence for crossfeed or exchanged channels.
        // It can add evidence when neither channel votes, but cannot overrule a disagreement.
        const auto sum = [] (const std::vector<float>& stereo)
        {
            std::vector<float> mono (stereo.size() / 2);
            for (std::size_t i = 0; i < mono.size(); ++i)
                mono[i] = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
            return mono;
        };
        estimate = estimateOffset (sum (post), sum (pre), lag);
    }
    estimate.window = { postEnd, view.proof, view.preRun };
    estimate.windowReady = true;
    estimate.clockK = view.k;
    estimate.blockFrames = view.blockFrames;
    estimate.sampleRate = rate;
    return estimate;
}
}
