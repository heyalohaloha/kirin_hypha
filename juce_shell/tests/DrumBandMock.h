#pragma once

#include "../src/HyphaAttackStage.h"
#include "../src/HyphaAttackUiContract.h"
#include "../src/HyphaSurfaceMaterial.h"
#include "../src/HyphaTextStyle.h"

#include <array>
#include <cmath>

// Design mock, not a product painter: DRUM's VIEW set to BAND with 50 Hz chosen. The history row
// shows the selected hit in that band, its head magnified (where a few ms of delay are visible)
// and its tail (where the ring-out is); the lanes show, hit by hit over 6 s, what moved between
// PRE and POST in that band: DELAY, ATT, REL and LEVEL. Synthesised values.
namespace hypha::tests::drum_band_mock
{
constexpr auto visualization = typography::Composition::visualization;

struct Envelope
{
    float delayMs, riseMs, tauMs, peakDb;
};

constexpr Envelope preEnvelope { 0.0f, 6.0f, 45.0f, -6.0f };
constexpr Envelope postEnvelope { 2.4f, 7.0f, 55.0f, -6.8f };
constexpr float floorDb = -48.0f;

inline float levelDb (const Envelope& envelope, float ms)
{
    const auto t = ms - envelope.delayMs;
    if (t <= 0.0f) return floorDb;
    const auto rise = juce::jmin (1.0f, t / envelope.riseMs);
    const auto decay = t > envelope.riseMs ? std::exp (-(t - envelope.riseMs) / envelope.tauMs) : 1.0f;
    return juce::jmax (floorDb, envelope.peakDb + 20.0f * std::log10 (juce::jmax (1.0e-6f, rise * decay)));
}

inline float releaseEndMs (const Envelope& envelope)
{
    return envelope.delayMs + envelope.riseMs + envelope.tauMs * std::log (10.0f);
}

inline juce::Rectangle<int> rect (attack_ui::Box box) { return { box.x, box.y, box.width, box.height }; }

inline void paintPane (juce::Graphics& g, juce::Rectangle<int> pane, float from, float to, bool head,
                       const char* name, const char* left, const char* right, presentation::Context context)
{
    attack_stage::paint (g, pane.toFloat(), 4.0f, 0.30f, true);
    auto inner = pane.reduced (6, 4);
    const auto axisLine = text_style::requiredLineHeight (
        typography::resolve (context, typography::TextRole::axis, visualization));
    auto labels = inner.removeFromBottom (axisLine);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (monoFont (context, typography::TextRole::axis, visualization));
    text_style::drawText (g, left, labels, juce::Justification::centredLeft, false);
    text_style::drawText (g, right, labels, juce::Justification::centredRight, false);
    g.setColour (COL_TEXT_SECONDARY);
    g.setFont (monoFont (context, typography::TextRole::legend, visualization));
    text_style::drawText (g, name, inner.removeFromTop (axisLine), juce::Justification::centredLeft, false);
    const auto plot = inner.toFloat();
    const auto x = [&] (float ms) { return plot.getX() + plot.getWidth() * (ms - from) / (to - from); };
    const auto y = [&] (float db) { return plot.getBottom() - plot.getHeight() * (db - floorDb) / -floorDb; };
    for (const auto db : { -12.0f, -24.0f, -36.0f })
    {
        g.setColour (COL_TEXT_TERTIARY.withAlpha (0.14f));
        g.drawHorizontalLine (juce::roundToInt (y (db)), plot.getX(), plot.getRight());
    }
    const auto path = [&] (const Envelope& envelope, bool closed)
    {
        juce::Path result;
        const auto steps = 480;
        for (int step = 0; step <= steps; ++step)
        {
            const auto ms = from + (to - from) * static_cast<float> (step) / steps;
            const juce::Point<float> at { x (ms), y (levelDb (envelope, ms)) };
            if (step == 0)
            {
                if (closed) { result.startNewSubPath (x (from), plot.getBottom()); result.lineTo (at); }
                else result.startNewSubPath (at);
            }
            else result.lineTo (at);
        }
        if (closed) { result.lineTo (x (to), plot.getBottom()); result.closeSubPath(); }
        return result;
    };
    const auto post = juce::Colour (attack_ui::waveformColour);
    const auto pre = juce::Colour (attack_ui::preTraceColour);
    {
        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (plot.getSmallestIntegerContainer());
        g.setGradientFill (juce::ColourGradient (post.withAlpha (0.34f), plot.getX(), plot.getY(),
                                                 post.withAlpha (0.04f), plot.getX(), plot.getBottom(), false));
        g.fillPath (path (postEnvelope, true));
        g.setColour (post.withAlpha (0.20f));
        g.strokePath (path (postEnvelope, false), juce::PathStrokeType (2.6f));
        g.setColour (post.withAlpha (0.92f));
        g.strokePath (path (postEnvelope, false), juce::PathStrokeType (1.0f));
        g.setColour (pre.withAlpha (0.85f));
        g.strokePath (path (preEnvelope, false), juce::PathStrokeType (0.9f));
    }
    const auto bracket = [&] (float fromMs, float toMs, float atY, const char* text)
    {
        g.setColour (COL_FLORA_BR);
        g.drawLine (x (fromMs), atY, x (toMs), atY, 1.5f);
        g.drawLine (x (fromMs), atY - 4.0f, x (fromMs), atY + 4.0f, 1.0f);
        g.drawLine (x (toMs), atY - 4.0f, x (toMs), atY + 4.0f, 1.0f);
        g.setFont (monoFont (context, typography::TextRole::readout, visualization));
        text_style::drawText (g, text, juce::Rectangle<float> (x (toMs) + 6.0f, atY - 10.0f, 120.0f, 20.0f),
                              juce::Justification::centredLeft, false);
    };
    if (head)
    {
        for (const auto& [envelope, colour] : { std::pair { preEnvelope, pre }, std::pair { postEnvelope, post } })
        {
            g.setColour (colour.withAlpha (0.7f));
            g.drawVerticalLine (juce::roundToInt (x (envelope.delayMs)), plot.getY(), plot.getBottom());
        }
        bracket (preEnvelope.delayMs, postEnvelope.delayMs, plot.getY() + 10.0f, "+2.4 ms");
        return;
    }
    for (const auto& [envelope, colour] : { std::pair { preEnvelope, pre }, std::pair { postEnvelope, post } })
    {
        const auto endX = x (releaseEndMs (envelope));
        const auto endY = y (envelope.peakDb - 20.0f);
        g.setColour (colour.withAlpha (0.85f));
        g.drawLine (endX, endY - 6.0f, endX, endY + 6.0f, 1.2f);
    }
    bracket (releaseEndMs (preEnvelope), releaseEndMs (postEnvelope), y (preEnvelope.peakDb - 20.0f) - 14.0f,
             "+23 ms");
}

// Over the page as AttackComponent paints it: header, history row and lanes redrawn for BAND.
inline void paintBandView (juce::Graphics& g, const attack_ui::Layout& layout, presentation::Context context)
{
    // Header: title, VIEW, and the band choice where the caption was.
    auto header = rect (layout.header);
    auto titleRow = header.removeFromTop (attack_ui::titleRowHeight (context));
    g.setColour (BG);
    g.fillRect (titleRow);
    g.fillRect (header.withTrimmedRight (170));
    auto viewButton = titleRow.removeFromRight (juce::roundToInt (static_cast<float> (titleRow.getWidth()) * 0.16f));
    g.setColour (COL_NORMAL);
    g.setFont (monoFont (context, typography::TextRole::sectionTitle, visualization).withExtraKerningFactor (0.08f));
    text_style::drawText (g, "DRUM / BAND", titleRow, juce::Justification::centredLeft);
    surface_material::paintControl (g, viewButton.reduced (1).toFloat(), false, false, true,
                                    juce::Colour (attack_ui::waveformColour), 3.0f);
    g.setFont (monoFont (context, typography::TextRole::action, visualization));
    text_style::drawText (g, "VIEW  BAND", viewButton, juce::Justification::centred);
    auto chips = header.withTrimmedRight (180);
    g.setFont (monoFont (context, typography::TextRole::legend, visualization));
    g.setColour (COL_TEXT_SECONDARY);
    text_style::drawText (g, "Hz", chips.removeFromLeft (44), juce::Justification::centredLeft);
    const char* bands[] { "ALL", "50", "100", "200", "500", "1k", "2k", "5k" };
    const auto chipWidth = chips.getWidth() / 8;
    for (int index = 0; index < 8; ++index)
    {
        auto chip = chips.removeFromLeft (chipWidth).reduced (3, 2);
        const bool selected = index == 1;
        surface_material::paintControl (g, chip.toFloat(), false, false, selected,
                                        juce::Colour (attack_ui::waveformColour), 3.0f);
        g.setColour (selected ? COL_FLORA_BR : COL_TEXT_SECONDARY);
        text_style::drawText (g, bands[index], chip, juce::Justification::centred, false);
    }

    // History row: the selected hit in 50 Hz, head and tail.
    const auto label = rect (attack_ui::labelCell (layout, layout.history));
    g.setColour (BG);
    g.fillRect (label);
    g.setColour (COL_TEXT_SECONDARY);
    g.setFont (monoFont (context, typography::TextRole::legend, visualization));
    text_style::drawText (g, "50 Hz", label.withTrimmedBottom (label.getHeight() / 2), juce::Justification::bottomLeft);
    text_style::drawText (g, "HIT", label.withTrimmedTop (label.getHeight() / 2), juce::Justification::topLeft);
    auto plot = rect (attack_ui::historyPlot (layout));
    g.setColour (BG);
    g.fillRect (plot);
    auto headPane = plot.removeFromLeft (plot.getWidth() * 2 / 5);
    plot.removeFromLeft (8);
    paintPane (g, headPane, -5.0f, 25.0f, true, "HEAD  PRE / POST", "-5", "+25 ms", context);
    paintPane (g, plot, 0.0f, 300.0f, false, "TAIL  -20 dB", "0", "+300 ms", context);

    // Lanes: what moved in 50 Hz, hit by hit, and the latest value.
    struct Fact { const char* name; const char* scale; const char* latest; std::uint32_t colour; float base, spread, limit; };
    const std::array<Fact, attack_ui::laneCount> facts {{
        { "DELAY", "+/-10 ms", "+2.4 ms", attack_ui::transientColour, 2.4f, 0.3f, 10.0f },
        { "ATT", "+/-10 ms", "+1.0 ms", attack_ui::strengthColour, 1.0f, 0.4f, 10.0f },
        { "REL", "+/-100 ms", "+23 ms", attack_ui::crestColour, 23.0f, 5.0f, 100.0f },
        { "LEVEL", "+/-12 dB", "-0.8 dB", attack_ui::sharpnessColour, -0.8f, 0.3f, 12.0f },
    }};
    for (std::size_t index = 0; index < facts.size(); ++index)
    {
        const auto& fact = facts[index];
        const auto colour = juce::Colour (fact.colour);
        auto cell = rect (attack_ui::labelCell (layout, layout.lanes[index]));
        g.setColour (BG);
        g.fillRect (cell);
        g.setColour (colour);
        g.setFont (monoFont (context, typography::TextRole::legend, visualization).withExtraKerningFactor (0.08f));
        text_style::drawText (g, fact.name, cell.removeFromTop (cell.getHeight() / 2), juce::Justification::bottomLeft);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (monoFont (context, typography::TextRole::axis, visualization));
        text_style::drawText (g, fact.scale, cell, juce::Justification::topLeft);
        auto readout = rect (attack_ui::readoutCell (layout, layout.lanes[index]));
        g.setColour (BG);
        g.fillRect (readout.withTrimmedLeft (8));
        g.setColour (COL_NORMAL);
        g.setFont (monoFont (context, typography::TextRole::primaryValue, visualization));
        text_style::drawText (g, fact.latest, readout.withTrimmedLeft (12), juce::Justification::centredLeft);
        const auto lane = rect (attack_ui::lanePlot (layout, index));
        attack_stage::paint (g, lane.toFloat(), 3.0f, 0.18f, false);
        const auto zero = static_cast<float> (lane.getCentreY());
        g.setColour (COL_TEXT_TERTIARY.withAlpha (0.35f));
        g.drawHorizontalLine (juce::roundToInt (zero), static_cast<float> (lane.getX()), static_cast<float> (lane.getRight()));
        for (int hit = 0; hit < 12; ++hit)
        {
            const auto xAt = static_cast<float> (lane.getRight()) - 12.0f
                           - static_cast<float> (11 - hit) * (static_cast<float> (lane.getWidth()) - 40.0f) / 11.0f;
            const auto value = fact.base + fact.spread * std::sin (static_cast<float> (hit) * 1.7f);
            const auto height = juce::jlimit (-1.0f, 1.0f, value / fact.limit) * (static_cast<float> (lane.getHeight()) * 0.45f);
            g.setColour (colour.withAlpha (hit == 11 ? 1.0f : 0.72f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (
                xAt - 2.0f, juce::jmin (zero, zero - height), xAt + 2.0f, juce::jmax (zero, zero - height)));
        }
    }
}
}
