#pragma once
#include "../src/HyphaPolylineGeometry.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyPolylineGeometryContract()
{
    const auto require = [] (bool value) {
        if (! value) { std::cerr << "Polyline geometry error exceeds 0.05 px\n"; std::exit (EXIT_FAILURE); }
    };
    std::array<float, 512> x {}, y {};
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float> (i) * 0.5f;
    for (int shape = 0; shape < 32; ++shape)
    {
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = static_cast<float> (70 * std::sin (i * 0.013 + shape)
                + shape * std::sin (i * 0.19) + (i == 257 ? shape * 9 : 0));
        const auto keep = polyline_geometry::retainedVertices (x, y);
        require (keep.front() && keep.back());
        size_t a = 0;
        for (size_t b = 1; b < keep.size(); ++b)
        {
            if (! keep[b]) continue;
            const double dx = static_cast<double> (x[b]) - x[a], dy = static_cast<double> (y[b]) - y[a];
            const double length = dx * dx + dy * dy;
            for (size_t i = a + 1; i < b; ++i)
            {
                const double px = static_cast<double> (x[i]) - x[a], py = static_cast<double> (y[i]) - y[a];
                const auto t = std::fmax (0.0, std::fmin (1.0, (px * dx + py * dy) / length));
                require (std::hypot (px - t * dx, py - t * dy) <= 0.050001);
            }
            a = b;
        }
        if (shape != 0) require (keep[257]); // narrow spectral spike remains visible
    }
    y.fill (0);
    auto keep = polyline_geometry::retainedVertices (x, y);
    size_t kept = 0;
    for (bool value : keep) kept += value ? 1u : 0u;
    require (kept == 2);
    keep = polyline_geometry::retainedVertices (x, y, 100, 150);
    require (! keep[0] && keep[100] && keep[150] && ! keep[511]);
    x.fill (0); y.fill (0);
    keep = polyline_geometry::retainedVertices (x, y);
    require (keep.front() && keep.back());
    std::cout << "Polyline geometry: PASS (32 changing curves, spikes, bounded error, endpoints)\n";
}
}
