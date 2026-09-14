#include "HyphaReferenceTonalView.h"

#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace hypha::reference_ui
{
namespace
{
constexpr std::array<const char*, 4> groupNames { "LOW", "LOW MID", "HIGH MID", "HIGH" };
constexpr std::array<double, 5> groupEdges { 20.0, 250.0, 2'000.0, 8'000.0, 20'000.0 };

double centerFrequency (int band) noexcept
{
    return 20.0 * std::pow (1'000.0, (static_cast<double> (band) + 0.5) / 60.0);
}

int groupForBand (int band) noexcept
{
    const auto frequency = centerFrequency (band);
    for (int group = 0; group < 4; ++group)
        if (frequency < groupEdges[static_cast<size_t> (group + 1)]) return group;
    return 3;
}

int representativeBand (int group) noexcept
{
    const auto frequency = std::sqrt (groupEdges[static_cast<size_t> (group)]
                                    * groupEdges[static_cast<size_t> (group + 1)]);
    return juce::jlimit (0, 59, static_cast<int> (
        std::floor (60.0 * std::log (frequency / 20.0) / std::log (1'000.0))));
}

std::optional<double> groupValue (const TonalView::Curve& curve, int group) noexcept
{
    if (curve.values == nullptr || group < 0 || group > 3) return std::nullopt;
    double power = 0.0;
    bool valid = false;
    for (int band = 0; band < 60; ++band)
    {
        if (groupForBand (band) != group
            || (curve.validBits & (std::uint64_t (1) << band)) == 0) continue;
        const auto value = curve.values[band];
        if (! std::isfinite (value)) continue;
        power += std::pow (10.0, static_cast<double> (value) / 10.0);
        valid = true;
    }
    return valid && power > 0.0 ? std::optional<double> (10.0 * std::log10 (power))
                                : std::nullopt;
}

std::optional<double> bandValue (const TonalView::Curve& curve, int band) noexcept
{
    if (curve.values == nullptr || band < 0 || band >= 60
        || (curve.validBits & (std::uint64_t (1) << band)) == 0
        || ! std::isfinite (curve.values[band])) return std::nullopt;
    return static_cast<double> (curve.values[band]);
}

juce::String signedDb (std::optional<double> value)
{
    if (! value) return "--";
    return (*value >= 0.0 ? "+" : "") + juce::String (*value, 1);
}

juce::String frequencyText (double frequency)
{
    return frequency < 1'000.0 ? juce::String (juce::roundToInt (frequency)) + " Hz"
                               : juce::String (frequency / 1'000.0, 1) + " kHz";
}

float tonalY (double value, juce::Rectangle<float> area) noexcept
{
    return area.getBottom() - static_cast<float> (
        (juce::jlimit (-90.0, -3.0, value) + 90.0) / 87.0) * area.getHeight();
}

juce::Path curvePath (const TonalView::Curve& curve, juce::Rectangle<float> area)
{
    juce::Path path;
    bool open = false;
    for (int band = 0; band < 60; ++band)
    {
        const auto value = bandValue (curve, band);
        if (! value) { open = false; continue; }
        const auto x = area.getX() + static_cast<float> (band) * area.getWidth() / 59.0f;
        const auto point = juce::Point<float> { x, tonalY (*value, area) };
        if (open) path.lineTo (point); else path.startNewSubPath (point);
        open = true;
    }
    return path;
}

juce::String relationToGenre (const reference_audition::ReferenceTonalCurve& genre,
                              int band, std::optional<double> value)
{
    if (! value || band < 0 || band >= 60
        || (genre.validBits & (std::uint64_t (1) << band)) == 0) return {};
    if (*value < genre.p10[static_cast<size_t> (band)]) return " / BELOW RANGE";
    if (*value > genre.p90[static_cast<size_t> (band)]) return " / ABOVE RANGE";
    return " / WITHIN RANGE";
}
}

TonalView::TonalView()
{
    setComponentID ("reference-tonal-view");
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void TonalView::update (std::shared_ptr<const reference_audition::VisualTimeline> next,
                        presentation::Context nextContext, bool concealed,
                        juce::String candidateName, juce::String cueLabel)
{
    timeline = concealed ? nullptr : std::move (next);
    context = nextContext;
    candidate = std::move (candidateName);
    cue = std::move (cueLabel);
    hidden = concealed;
    compact = context.logicalWidth < 450;
    if (hidden) { pointedBand = selectedBand = -1; }
    updateAccessibleDescription();
    repaint();
}

TonalView::Curve TonalView::aCurve() const noexcept
{
    if (! timeline || ! timeline->tonalAvailable) return {};
    return { timeline->tonal.values_db, timeline->tonal.valid_bits };
}

TonalView::Curve TonalView::cCurve() const noexcept
{
    if (! timeline || ! timeline->tonalReference) return {};
    return { timeline->tonalReference->median.data(), timeline->tonalReference->validBits };
}

int TonalView::activeBand() const noexcept
{
    return selectedBand >= 0 ? selectedBand : pointedBand;
}

void TonalView::paint (juce::Graphics& g)
{
    if (hidden) return;
    auto area = getLocalBounds().toFloat();
    surface_material::paintPanel (g, area, 0.72f);
    auto header = area.removeFromTop (compact ? 20.0f : 27.0f).reduced (8.0f, 0.0f);
    g.setFont (labelFont (context, typography::TextRole::metricLabel,
                          typography::Composition::visualization));
    g.setColour (COL_NORMAL.withAlpha (0.94f));
    text_style::drawEllipsized (g, "BALANCE", header.removeFromLeft (header.getWidth() * 0.42f).toNearestInt(),
                                juce::Justification::centredLeft);
    const auto captured = timeline && timeline->capture;
    auto condition = juce::String (captured ? "A CAPTURE" : "A LIVE")
                   + (timeline && timeline->tonalReference ? " / C CUE" : "");
    if (timeline && timeline->tonalGenre)
        condition += " / " + timeline->tonalGenre->displayLabel.toUpperCase();
    g.setFont (labelFont (context, typography::TextRole::legend,
                          typography::Composition::visualization));
    g.setColour (COL_TEXT_TERTIARY.withAlpha (0.92f));
    text_style::drawEllipsized (g, activeBand() >= 0 ? "CLICK AGAIN FOR OVERVIEW" : condition,
                                header.toNearestInt(), juce::Justification::centredRight);

    const auto a = aCurve(), c = cCurve();
    if (compact)
    {
        graphArea = {};
        summaryArea = area.reduced (5.0f, 4.0f);
    }
    else
    {
        summaryArea = area.removeFromBottom (42.0f).reduced (5.0f, 3.0f);
        graphArea = area.reduced (10.0f, 5.0f);
        g.setColour (COL_MUTED.withAlpha (0.11f));
        for (int line = 1; line < 4; ++line)
            g.drawHorizontalLine (juce::roundToInt (graphArea.getY() + graphArea.getHeight() * line / 4.0f),
                                  graphArea.getX(), graphArea.getRight());
        if (timeline && timeline->tonalGenre)
        {
            const Curve low { timeline->tonalGenre->p10.data(), timeline->tonalGenre->validBits };
            const Curve high { timeline->tonalGenre->p90.data(), timeline->tonalGenre->validBits };
            g.setColour (COL_TEXT_TERTIARY.withAlpha (0.32f));
            g.strokePath (curvePath (low, graphArea), juce::PathStrokeType (0.9f));
            g.strokePath (curvePath (high, graphArea), juce::PathStrokeType (0.9f));
        }
        if (c.values != nullptr)
        {
            g.setColour (COL_FLORA.withAlpha (0.86f));
            g.strokePath (curvePath (c, graphArea), juce::PathStrokeType (1.45f));
        }
        if (a.values != nullptr)
        {
            g.setColour (COL_SPECTRUM_POST.withAlpha (0.94f));
            g.strokePath (curvePath (a, graphArea), juce::PathStrokeType (1.55f));
        }
        const auto band = activeBand();
        if (band >= 0)
        {
            const auto x = graphArea.getX() + static_cast<float> (band) * graphArea.getWidth() / 59.0f;
            g.setColour (COL_NORMAL.withAlpha (0.55f));
            g.drawVerticalLine (juce::roundToInt (x), graphArea.getY(), graphArea.getBottom());
        }
    }

    const auto band = activeBand();
    if (band >= 0)
    {
        const auto av = bandValue (a, band), cv = bandValue (c, band);
        const auto delta = av && cv ? std::optional<double> (*cv - *av) : std::nullopt;
        auto detail = frequencyText (centerFrequency (band)) + "   A " + signedDb (av)
                    + "   C " + signedDb (cv) + "   C-A " + signedDb (delta) + " dB";
        if (timeline && timeline->tonalGenre)
            detail += relationToGenre (*timeline->tonalGenre, band, cv);
        g.setColour (COL_NORMAL.withAlpha (0.94f));
        g.setFont (labelFont (context, typography::TextRole::readout,
                              typography::Composition::visualization));
        text_style::drawEllipsized (g, detail, summaryArea.toNearestInt(), juce::Justification::centred);
        return;
    }

    auto summary = summaryArea;
    const auto gap = compact ? 3.0f : 5.0f;
    const auto width = (summary.getWidth() - gap * 3.0f) / 4.0f;
    for (int group = 0; group < 4; ++group)
    {
        auto cell = summary.removeFromLeft (width);
        summary.removeFromLeft (gap);
        g.setColour (COL_MUTED.withAlpha (0.10f));
        g.fillRoundedRectangle (cell, 3.0f);
        g.setColour (COL_TEXT_SECONDARY.withAlpha (0.92f));
        g.setFont (labelFont (context, typography::TextRole::legend,
                              typography::Composition::visualization));
        text_style::drawEllipsized (g, groupNames[static_cast<size_t> (group)],
                                    cell.removeFromTop (compact ? 14.0f : 15.0f).toNearestInt(),
                                    juce::Justification::centred);
        const auto av = groupValue (a, group), cv = groupValue (c, group);
        const auto delta = av && cv ? std::optional<double> (*cv - *av) : std::nullopt;
        g.setColour (COL_NORMAL.withAlpha (0.9f));
        const auto text = delta ? "C-A " + signedDb (delta) + " dB"
                          : av ? "A " + signedDb (av) + " dB"
                          : cv ? "C " + signedDb (cv) + " dB" : "--";
        text_style::drawEllipsized (g, text, cell.toNearestInt(), juce::Justification::centred);
    }
}

int TonalView::bandAt (juce::Point<float> point) const noexcept
{
    if (graphArea.isEmpty() || ! graphArea.contains (point)) return -1;
    return juce::jlimit (0, 59, juce::roundToInt (
        (point.x - graphArea.getX()) * 59.0f / graphArea.getWidth()));
}

int TonalView::groupAt (juce::Point<float> point) const noexcept
{
    if (! summaryArea.contains (point)) return -1;
    return juce::jlimit (0, 3, static_cast<int> (
        4.0f * (point.x - summaryArea.getX()) / summaryArea.getWidth()));
}

void TonalView::mouseMove (const juce::MouseEvent& event)
{
    if (hidden || selectedBand >= 0) return;
    const auto next = bandAt (event.position);
    if (next != pointedBand) { pointedBand = next; updateAccessibleDescription(); repaint(); }
}

void TonalView::mouseExit (const juce::MouseEvent&)
{
    if (selectedBand < 0 && pointedBand >= 0)
    { pointedBand = -1; updateAccessibleDescription(); repaint(); }
}

void TonalView::mouseDown (const juce::MouseEvent& event)
{
    if (hidden) return;
    auto next = bandAt (event.position);
    if (next < 0)
    {
        const auto group = groupAt (event.position);
        if (group >= 0) next = representativeBand (group);
    }
    if (next < 0) return;
    selectedBand = selectedBand == next ? -1 : next;
    pointedBand = -1;
    updateAccessibleDescription();
    repaint();
}

bool TonalView::keyPressed (const juce::KeyPress& key)
{
    if (hidden) return false;
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::homeKey)
    { selectedBand = pointedBand = -1; updateAccessibleDescription(); repaint(); return true; }
    const auto direction = key == juce::KeyPress::leftKey ? -1
                         : key == juce::KeyPress::rightKey ? 1 : 0;
    if (direction == 0) return false;
    selectedBand = juce::jlimit (0, 59, (activeBand() < 0 ? 0 : activeBand()) + direction);
    pointedBand = -1;
    updateAccessibleDescription();
    repaint();
    return true;
}

void TonalView::updateAccessibleDescription()
{
    if (hidden || ! timeline) { setTitle ({}); setDescription ({}); return; }
    auto description = juce::String (timeline->capture ? "Captured A" : "Live A");
    if (timeline->tonalReference)
        description += " and C" + (candidate.isNotEmpty() ? " " + candidate : juce::String {})
                     + (cue.isNotEmpty() ? ", cue " + cue : juce::String {});
    const auto band = activeBand();
    if (band >= 0) description += ", selected " + frequencyText (centerFrequency (band));
    setTitle ("Balance");
    setDescription (description);
}
}
