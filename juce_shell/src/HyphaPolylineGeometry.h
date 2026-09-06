#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace hypha::polyline_geometry
{
// Display geometry only. Every omitted vertex stays within tolerance of its retained segment.
// Bounded iterative Douglas-Peucker: no recursive stack, allocation, or amplitude resampling.
// Values used for measurement/readouts are never modified. Endpoints always survive.
template <size_t N>
std::array<bool, N> retainedVertices (const std::array<float, N>& x,
                                    const std::array<float, N>& y,
                                    size_t first = 0, size_t last = N - 1,
                                    double tolerance = 0.05) noexcept
{
    static_assert (N >= 2);
    std::array<bool, N> keep {};
    if (first > last || last >= N) return keep;
    std::array<std::pair<size_t, size_t>, N> pending {};
    size_t count = 0;
    pending[count++] = { first, last };
    keep[first] = keep[last] = true;
    while (count != 0)
    {
        const auto range = pending[--count];
        const auto a = range.first, b = range.second;
        const double dx = static_cast<double> (x[b]) - x[a];
        const double dy = static_cast<double> (y[b]) - y[a];
        const double length = dx * dx + dy * dy;
        double worst = tolerance * tolerance;
        size_t split = a;
        for (size_t i = a + 1; i < b; ++i)
        {
            const double px = static_cast<double> (x[i]) - x[a];
            const double py = static_cast<double> (y[i]) - y[a];
            // FFT x-coordinates are monotone, but use finite-segment distance even for tests
            // and future callers whose vertical endpoints coincide or fold back.
            const double t = length > 0 ? std::fmax (0.0, std::fmin (1.0, (px * dx + py * dy) / length)) : 0;
            const double ex = px - t * dx, ey = py - t * dy;
            const double distance = ex * ex + ey * ey;
            if (! std::isfinite (distance) || distance > worst)
            { worst = distance; split = i; }
        }
        if (split != a)
        {
            keep[split] = true;
            pending[count++] = { a, split };
            pending[count++] = { split, b };
        }
    }
    return keep;
}
}
