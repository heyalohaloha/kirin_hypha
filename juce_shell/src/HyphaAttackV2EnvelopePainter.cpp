#include "HyphaAttackV2Painter.h"
#include "HyphaAttackUiContract.h"
#include "HyphaAttackStage.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"
#include <algorithm>
#include <cmath>

namespace hypha::attack_v2
{
namespace
{
constexpr auto visualization = typography::Composition::visualization;
const auto postColour = juce::Colour (attack_ui::waveformColour), preColour = juce::Colour (attack_ui::preTraceColour);
juce::Point<float> position (std::size_t at, std::size_t count, double level, juce::Rectangle<float> plot)
{
    return { plot.getX() + plot.getWidth() * static_cast<float> (at) / static_cast<float> (count - 1),
             plot.getBottom() - plot.getHeight() * static_cast<float> (std::clamp ((level + 120.0) / 120.0, 0.0, 1.0)) };
}
void edge (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour colour)
{ g.setColour (colour.withAlpha (0.90f)); g.drawLine ({ a, b }, 1.0f); }
template <typename Point>
void average (juce::Graphics& g, const Point* points, std::size_t count,
              juce::Rectangle<float> plot, bool pre, juce::Colour colour)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& p = points[i];
        if (p.valid_count == 0 || (pre && ! p.has_pre)) continue;
        const auto mean = pre ? p.pre_mean : p.post_mean;
        const auto current = position (i, count, mean, plot);
        if (i == 0 || ! p.connect_previous || points[i - 1].participating_bits != p.participating_bits)
        { g.setColour (colour); g.fillEllipse (current.x - .7f, current.y - .7f, 1.4f, 1.4f); continue; }
        const auto& old = points[i - 1];
        if (old.valid_count == 0 || (pre && ! old.has_pre)) continue;
        juce::Path range;
        range.startNewSubPath (position (i - 1, count, pre ? old.pre_min : old.post_min, plot));
        range.lineTo (position (i - 1, count, pre ? old.pre_max : old.post_max, plot));
        range.lineTo (position (i, count, pre ? p.pre_max : p.post_max, plot));
        range.lineTo (position (i, count, pre ? p.pre_min : p.post_min, plot)); range.closeSubPath();
        g.setColour (colour.withAlpha (.10f)); g.fillPath (range);
        edge (g, position (i - 1, count, pre ? old.pre_mean : old.post_mean, plot), current, colour);
    }
}
void measured (juce::Graphics& g, const double* points, const std::uint8_t* mask,
               std::size_t count, juce::Rectangle<float> plot, juce::Colour colour)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (mask[i] == 0) continue;
        const auto current = position (i, count, points[i], plot);
        if (i != 0 && mask[i - 1]) edge (g, position (i - 1, count, points[i - 1], plot), current, colour);
        else { g.setColour (colour); g.fillEllipse (current.x - .7f, current.y - .7f, 1.4f, 1.4f); }
    }
}
void allShape (juce::Graphics& g, const KirinAttackDetail& detail, juce::Rectangle<float> plot, juce::Colour colour)
{
    const auto count = std::min (detail.shape_count, static_cast<std::uint32_t> (KIRIN_ATTACK_SHAPE_CAPACITY));
    for (std::size_t i = 1; i < count; ++i)
        if (std::isfinite (detail.shape[i - 1]) && std::isfinite (detail.shape[i]))
            edge (g, position (i - 1, count, detail.shape[i - 1], plot), position (i, count, detail.shape[i], plot), colour);
}
}
void paintEnvelopes (juce::Graphics& g, const Presentation& p, const Geometry& geometry,
                     const presentation::Context& c, bool overlay)
{
    const auto pane = [&] (juce::Rectangle<int> area, bool head) {
        if (area.isEmpty()) return;
        attack_stage::paint (g, area.toFloat(), 3, .18f, false);
        auto inner = area.reduced (4, 2);
        auto caption = inner.removeFromTop (presentation::densityIndex (c.density) == 4 ? 20 : 14);
        g.setFont (monoFont (c, typography::TextRole::axis, visualization)); g.setColour (COL_NORMAL);
        text_style::drawText (g, juce::String::fromUTF8 (head ? u8"HEAD −20…+40 ms" : u8"TAIL 0…+300 ms"), caption, juce::Justification::centredLeft, false);
        juce::String title;
        if (p.summary)
        {
            std::uint8_t min = 8, max = 0;
            const auto scan = [&] (const auto& points) { for (const auto& point : points) { min = std::min (min, point.valid_count); max = std::max (max, point.valid_count); } };
            if (head) scan (p.summary->head); else scan (p.summary->tail);
            title = words ("Mean(dB)", u8"平均(dB)") + " " + juce::String (min) + juce::String::fromUTF8 (u8"–") + juce::String (max)
                + "/" + juce::String (p.summary->cohort_count);
        }
        else title = words ("Single hit", u8"一打");
        text_style::drawText (g, title, caption, juce::Justification::centredRight, false);
        auto postPlot = inner.toFloat(), prePlot = postPlot;
        const bool hasPre = p.header.target == KIRIN_TARGET_DELTA;
        if (hasPre && ! overlay)
        {
            const auto rowHeight = (inner.getHeight() - 4) / 2;
            prePlot = inner.withHeight (rowHeight).toFloat();
            postPlot = inner.withTrimmedTop (rowHeight + 4).toFloat();
        }
        juce::Graphics::ScopedSaveState saved (g); g.reduceClipRegion (inner);
        if (p.summary)
        {
            if (head) { average (g, p.summary->head, 96, postPlot, false, postColour); if (hasPre) average (g, p.summary->head, 96, prePlot, true, preColour); }
            else { average (g, p.summary->tail, 64, postPlot, false, postColour); if (hasPre) average (g, p.summary->tail, 64, prePlot, true, preColour); }
        }
        else if (p.single && p.header.band != 0)
        {
            const auto offset = head ? 0 : 96, count = head ? 96 : 64;
            measured (g, p.single->post + offset, p.single->post_valid + offset, static_cast<std::size_t> (count), postPlot, postColour);
            if (hasPre) measured (g, p.single->pre + offset, p.single->pre_valid + offset, static_cast<std::size_t> (count), prePlot, preColour);
        }
        else if (p.single)
        {
            if (p.single->has_all_post) allShape (g, p.single->all_post, postPlot, postColour);
            if (p.single->has_all_pre) allShape (g, p.single->all_pre, prePlot, preColour);
        }
    };
    if (! geometry.head.isEmpty()) { pane (geometry.head, true); pane (geometry.tail, false); }
}
}
