#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <juce_graphics/juce_graphics.h>
#include "HyphaPolylineGeometry.h"

namespace hypha::monotone_curve
{
// Cubic Hermite slopes limited to each segment's sign and magnitude. Every control point
// stays inside its two samples: a narrow peak cannot overshoot, and holes start new paths.
template <size_t Size>
void append (juce::Path& path, const std::array<float, Size>& x, const std::array<float, Size>& y,
             size_t first, size_t last, bool start = true, bool compactStraight = false)
{
    auto keep = polyline_geometry::retainedVertices (x, y, first, last);
    // Keep every change of direction, including small peaks; simplify only the invisible
    // detail along a monotone run. The 0.05 logical-pixel bound is shared with the old strokes.
    for (size_t i = first + 1; i < last; ++i)
        if ((y[i] > y[i - 1] && y[i] >= y[i + 1]) || (y[i] < y[i - 1] && y[i] <= y[i + 1])) keep[i] = true;
    std::array<size_t, Size> knots {};
    size_t count = 0;
    for (size_t i = first; i <= last; ++i) if (keep[i]) knots[count++] = i;
    std::array<float, Size> slope {};
    const auto secant = [&] (size_t i) { const auto a = knots[i], b = knots[i + 1]; return (y[b] - y[a]) / (x[b] - x[a]); };
    for (size_t i = 0; i < count && count > 1; ++i)
    {
        if (i == 0) slope[i] = secant (i);
        else if (i + 1 == count) slope[i] = secant (i - 1);
        else
        {
            const auto a = secant (i - 1), b = secant (i);
            const auto h0 = x[knots[i]] - x[knots[i - 1]], h1 = x[knots[i + 1]] - x[knots[i]];
            const auto w0 = 2 * h1 + h0, w1 = h1 + 2 * h0;
            slope[i] = a * b <= 0 ? 0 : (w0 + w1) / (w0 / a + w1 / b);
        }
    }
    if (start) path.startNewSubPath (x[first], y[first]);
    for (size_t i = 0; i + 1 < count; ++i)
    {
        const auto a = knots[i], b = knots[i + 1];
        const auto dx = (x[b] - x[a]) / 3;
        const auto lo = std::min (y[a], y[b]), hi = std::max (y[a], y[b]);
        const auto c1 = std::clamp (y[a] + slope[i] * dx, lo, hi);
        const auto c2 = std::clamp (y[b] - slope[i + 1] * dx, lo, hi);
        // The renderer need not tessellate an already straight cubic. This bound is far below
        // the retained-vertex tolerance; genuinely curved segments keep their Hermite controls.
        if (compactStraight && std::abs (c1 - (y[a] + (y[b] - y[a]) / 3)) < 0.00001f
            && std::abs (c2 - (y[b] - (y[b] - y[a]) / 3)) < 0.00001f)
            path.lineTo (x[b], y[b]);
        else
            path.cubicTo (x[a] + dx, c1, x[b] - dx, c2, x[b], y[b]);
    }
}
template <size_t Size>
juce::Path make (const std::array<float, Size>& x, const std::array<float, Size>& y,
                 const std::array<uint8_t, Size>* valid = nullptr, bool compactStraight = false)
{
    juce::Path path;
    path.preallocateSpace (static_cast<int> (Size * 7u));
    const auto known = [&] (size_t i) { return (! valid || (*valid)[i]) && std::isfinite (y[i]); };
    for (size_t first = 0; first < Size;)
    {
        if (! known (first)) { ++first; continue; }
        auto last = first;
        while (last + 1 < Size && known (last + 1)) ++last;
        append (path, x, y, first, last, true, compactStraight);
        first = last + 1;
    }
    return path;
}
}
