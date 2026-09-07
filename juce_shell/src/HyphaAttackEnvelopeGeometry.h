#pragma once
#include "HyphaAttackUiContract.h"
#include "kirin_hypha_ffi.h"
#include "HyphaPolylineGeometry.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>

namespace hypha::attack_envelope
{
inline constexpr std::size_t capacity = KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY;
struct Geometry { juce::Path body, edge; };
inline Geometry geometry (const KirinAttackWaveformBatch& batch, juce::Rectangle<float> area,
                          std::int64_t first, std::int64_t latest, std::uint32_t rate,
                          float maximumError = .05f)
{
    Geometry result;
    if (latest <= first || rate == 0 || ! std::isfinite (area.getX()) || ! std::isfinite (area.getY())
        || ! std::isfinite (area.getWidth()) || ! std::isfinite (area.getHeight())
        || area.getWidth() < 1 || area.getHeight() < 1) return result;
    std::array<juce::Point<float>, capacity + 2> top;
    int used = 0;
    float startX = 0, endX = 0;
    const auto centre = area.getCentreY();
    const auto span = static_cast<std::uint64_t> (latest) - static_cast<std::uint64_t> (first);
    const auto x = [&] (std::int64_t sample) {
        const auto offset = static_cast<std::uint64_t> (sample) - static_cast<std::uint64_t> (first);
        return area.getX() + area.getWidth() * static_cast<float> (
            static_cast<long double> (offset) / static_cast<long double> (span)); };
    const auto finish = [&] {
        if (used == 0) return;
        std::array<float, capacity + 2> xs {}, ys {};
        xs[0] = startX; ys[0] = top[0].y;
        for (int i = 0; i < used; ++i) { xs[static_cast<std::size_t> (i+1)] = top[static_cast<std::size_t> (i)].x;
                                        ys[static_cast<std::size_t> (i+1)] = top[static_cast<std::size_t> (i)].y; }
        const auto last = static_cast<std::size_t> (used+1);
        xs[last] = endX; ys[last] = top[static_cast<std::size_t> (used-1)].y;
        std::array<bool, capacity + 2> keep {};
        std::size_t anchor = 0;
        const auto retain = [&] (std::size_t end) {
            const auto segment = polyline_geometry::retainedVertices (xs, ys, anchor, end,
                std::isfinite (maximumError) ? juce::jlimit (0.0f,.05f,maximumError) : 0.0f);
            for (auto i=anchor;i<=end;++i) keep[i]=segment[i];
            anchor=end;
        };
        // Split at extrema before simplifying: the same error bound applies to every subsegment.
        for (std::size_t i=1;i<last;++i)
            if ((ys[i]<ys[i-1] && ys[i]<=ys[i+1]) || (ys[i]>ys[i-1] && ys[i]>=ys[i+1])) retain (i);
        retain (last);
        result.body.startNewSubPath (startX, top[0].y);
        result.edge.startNewSubPath (startX, top[0].y);
        for (int i = 0; i < used; ++i) { if (! keep[static_cast<std::size_t> (i+1)]) continue;
                                       result.body.lineTo (top[static_cast<std::size_t> (i)]);
                                       result.edge.lineTo (top[static_cast<std::size_t> (i)]); }
        result.body.lineTo (endX, top[static_cast<std::size_t> (used - 1)].y);
        result.edge.lineTo (endX, top[static_cast<std::size_t> (used - 1)].y);
        result.body.lineTo (endX, 2*centre - top[static_cast<std::size_t> (used - 1)].y);
        result.edge.startNewSubPath (endX, 2*centre - top[static_cast<std::size_t> (used - 1)].y);
        for (int i = used - 1; i >= 0; --i) {
            if (! keep[static_cast<std::size_t> (i+1)]) continue;
            const auto p = top[static_cast<std::size_t> (i)];
            result.body.lineTo (p.x, 2*centre - p.y); result.edge.lineTo (p.x, 2*centre - p.y); }
        result.body.lineTo (startX, 2*centre - top[0].y); result.body.closeSubPath();
        result.edge.lineTo (startX, 2*centre - top[0].y); used = 0;
    };
    const auto count = std::min (batch.count, static_cast<std::uint32_t> (capacity));
    std::int64_t previousEnd = 0;
    std::uint64_t generation = 0;
    std::uint32_t channels = 0;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto& p = batch.points[i];
        const auto duration = static_cast<std::uint64_t> (p.end_sample) - static_cast<std::uint64_t> (p.start_sample);
        if (p.sample_rate != rate || p.channels < 1 || p.channels > 2 || ! std::isfinite (p.rms_dbfs)
            || p.end_sample <= p.start_sample || duration > rate
            || p.end_sample <= first || p.start_sample >= latest)
        { finish(); continue; }
        if (used > 0 && (p.start_sample != previousEnd || p.generation != generation || p.channels != channels))
            finish();
        const auto level = juce::jlimit (0.0f, 1.0f, (p.rms_dbfs - attack_ui::absoluteFloorDb)
                                                            / -attack_ui::absoluteFloorDb);
        if (level <= 0) { finish(); continue; } // No persistent silence baseline.
        const auto left = std::max (p.start_sample, first), right = std::min (p.end_sample, latest);
        if (used == 0) startX = x (left);
        endX = x (right);
        top[static_cast<std::size_t> (used++)] = {
            (x (left) + endX) * .5f,
            centre - level * std::max (0.0f, area.getHeight() * .5f - 1.0f) };
        previousEnd = p.end_sample; generation = p.generation; channels = p.channels;
    }
    finish(); return result;
}
}
