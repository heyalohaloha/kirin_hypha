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

std::vector<float> monoSum (const std::vector<float>& interleaved)
{
    std::vector<float> mono (interleaved.size() / 2);
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (interleaved[i * 2] + interleaved[i * 2 + 1]);
    return mono;
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
    // A narrowband window (a held tone) repeats every period: nothing to say. Its energy sits in a
    // few bins; broadband music spreads it.
    std::vector<double> energy (size / 2);
    double total = 0.0;
    for (std::size_t k = 0; k < size / 2; ++k)
        total += energy[k] = std::norm (x[k]);
    std::nth_element (energy.begin(), energy.begin() + static_cast<std::ptrdiff_t> (energy.size() / 100), energy.end(),
                      std::greater<double>());
    double top = 0.0;
    for (std::size_t k = 0; k <= energy.size() / 100; ++k)
        top += energy[k];
    if (total <= 0.0 || top > 0.8 * total)
        return estimate;
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
    return estimate;
}

OffsetEstimate measureOffset (const Ring& ring, const PostRenderer& renderer)
{
    const auto view = renderer.historyView();
    if (view.samples == nullptr || ! view.kValid)
        return {};
    const auto frames = offsetWindowFrames;
    const auto lag = offsetSearchFrames;
    if (std::min (view.end - view.start, view.frames - 1) < frames + lag)
        return {};
    const auto postStart = view.end - lag - frames;
    std::vector<float> post (static_cast<std::size_t> (frames) * 2);
    std::vector<float> pre (static_cast<std::size_t> (frames + 2 * lag) * 2);
    copyPostHistory (view, postStart, frames, post);
    if (renderer.historyWriteEnd() - postStart > view.frames
        || ! copyPreRing (ring, postStart - view.k - lag, frames + 2 * lag, pre))
        return {};
    return estimateOffset (monoSum (post), monoSum (pre), lag);
}
}
