#include "HyphaHybridVuPainter.h"
#include "HyphaVuNeedlePainter.h"
#include "HyphaTruePeakRail.h"

#include <BinaryData.h>

#include "HyphaTheme.h"
#include "HyphaTextStyle.h"
#include "HyphaJungleMaterial.h"

#include <array>
#include <cmath>
#include <limits>

namespace hypha::hybrid_vu
{
namespace
{
constexpr std::array<int, 18> vuTickHalves {
    -40, -35, -30, -25, -20, -18, -16, -14, -12, -10, -8, -6, -4, -2, 0, 2, 4, 6
};
// A physical VU face is not a linear dB ruler.  These control points reproduce the attached
// dial's engraved positions while the 300 ms measurement and needle response remain unchanged.
constexpr std::array<double, 10> vuScaleDb { -40.0, -20.0, -10.0, -7.0, -5.0,
                                             -3.0, -1.0, 0.0, 1.0, 3.0 };
constexpr std::array<float, 10> vuScalePosition { 0.0f, 0.071f, 0.239f, 0.328f, 0.421f,
                                                  0.550f, 0.701f, 0.780f, 0.866f, 1.0f };

const auto meterIvory = COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.34f);

const juce::Image& chassisImage()
{
    static const auto image = juce::ImageFileFormat::loadFrom (
        BinaryData::hybrid_vu_chassis_png,
        static_cast<size_t> (BinaryData::hybrid_vu_chassis_pngSize));
    return image;
}

float scaled (float value, float width, float minimum = 0.7f) noexcept
{
    return juce::jmax (minimum, value * width / 900.0f);
}

void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
               presentation::Context presentation, typography::TextRole role,
               typography::Composition composition, juce::Colour colour,
               juce::Justification justification,
               bool tabular = false, float tracking = 0.0f)
{
    const auto font = (tabular ? monoFont (presentation, role, composition)
                               : labelFont (presentation, role, composition))
                          .withExtraKerningFactor (tracking);
    g.setColour (colour);
    if (tabular)
        drawTabularText (g, font, text, area, justification);
    else
    {
        g.setFont (labelFont (presentation, role, composition)
                       .withExtraKerningFactor (tracking));
        text_style::draw (g, text, area.toNearestInt(), presentation, role,
                          justification, 1, composition);
    }
}

void paintHeader (juce::Graphics& g, juce::Rectangle<float> area, const State& state)
{
    const auto width = area.getWidth();
    auto title = area.reduced (scaled (25.0f, width, 6.5f), 0.0f);
    const auto roleText = state.role == observatory::Role::post ? juce::String ("POST")
                                                                : juce::String ("PRE");
    const auto roleFont = labelFont (state.presentation, typography::TextRole::shellTitle)
                              .withExtraKerningFactor (0.12f);
    // Reserve actual glyph widths: a fixed 45% role share clipped POST at 300 px.
    const auto roleWidth = std::ceil (roleFont.getStringWidthFloat (roleText))
                         + scaled (3.0f, width, 1.0f);
    const juce::String brandText = width >= 700.0f ? "H Y P H A" : "HYPHA";
    const auto brandFont = labelFont (state.presentation, typography::TextRole::shellTitle);
    const auto titleGap = scaled (5.0f, width, 1.0f);
    const auto minimumTitle = roleWidth + titleGap
                            + std::ceil (brandFont.getStringWidthFloat (brandText)) + 1.0f;
    auto titleArea = title.removeFromLeft (juce::jmax (title.getWidth() * 0.225f, minimumTitle));
    drawText (g, roleText,
              titleArea.removeFromLeft (roleWidth), state.presentation,
              typography::TextRole::shellTitle, typography::Composition::shell,
              state.role == observatory::Role::post ? COL_FLORA : COL_LED_BLUE,
              juce::Justification::centredLeft, false, 0.12f);
    titleArea.removeFromLeft (titleGap);
    drawText (g, brandText, titleArea,
              state.presentation, typography::TextRole::shellTitle,
              typography::Composition::shell, COL_NORMAL, juce::Justification::centredLeft);

    const auto whole = title;
    auto record = title.removeFromRight (title.getWidth() * (width < 450.0f ? 0.28f : 0.205f));
    auto connection = title;
    if (width >= 450.0f)
        connection.removeFromLeft (connection.getWidth() * 0.60f);
    const auto dot = juce::jlimit (3.0f, 11.0f, width * 0.010f);
    // The place REC would take is open until it records, and the space before the light is open too: a
    // longer source name takes both, in that order, before it is shortened with an ellipsis.
    const auto statusFont = labelFont (state.presentation, typography::TextRole::status);
    const auto named = dot * 3.4f + std::ceil (text_style::shownWidth (statusFont, state.connectionText)) + 1.0f;
    if (! state.recording) connection.setRight (whole.getRight());
    else connection.removeFromRight (dot); // clear of the line before REC
    if (named > connection.getWidth())
        connection.setLeft (juce::jmax (whole.getX(), connection.getRight() - named));
    const auto connectionDot = juce::Point<float> (connection.getX() + dot,
                                                    connection.getCentreY());
    const auto brightConnection = state.connectionColour.interpolatedWith (
        COL_SPECTRUM_DELTA, 0.30f);
    g.setColour (brightConnection.withAlpha (0.13f));
    g.fillEllipse (connectionDot.x - dot, connectionDot.y - dot, dot * 2.0f, dot * 2.0f);
    g.setColour (brightConnection);
    g.fillEllipse (connectionDot.x - dot * 0.45f, connectionDot.y - dot * 0.45f,
                   dot * 0.9f, dot * 0.9f);
    connection.removeFromLeft (dot * 3.4f);
    g.setColour (brightConnection);
    g.setFont (labelFont (state.presentation, typography::TextRole::status));
    text_style::drawEllipsized (g, state.connectionText, connection.reduced (0.0f, area.getHeight() * 0.15f).toNearestInt(),
                                juce::Justification::centredLeft);

    if (state.recording)
    {
        g.setColour (COL_TEXT_TERTIARY);
        g.drawVerticalLine (juce::roundToInt (record.getX()),
                            record.getY() + area.getHeight() * 0.24f,
                            record.getBottom() - area.getHeight() * 0.24f);
        const auto recordDot = juce::Point<float> (
            record.getX() + record.getWidth() * 0.42f, record.getCentreY());
        const auto recordColour = COL_TEXT_SECONDARY;
        g.setColour (recordColour.withAlpha (0.11f));
        g.fillEllipse (recordDot.x - dot, recordDot.y - dot, dot * 2.0f, dot * 2.0f);
        g.setColour (recordColour);
        g.fillEllipse (recordDot.x - dot * 0.48f, recordDot.y - dot * 0.48f,
                       dot * 0.96f, dot * 0.96f);
        record.removeFromLeft (record.getWidth() * 0.60f);
        drawText (g, "REC", record, state.presentation, typography::TextRole::status,
                  typography::Composition::shell, recordColour,
                  juce::Justification::centredLeft, false, 0.10f);
    }
}

void paintClipStatus (juce::Graphics& g, juce::Rectangle<float> face, const State& state,
                      bool compact)
{
    const auto width = face.getWidth();
    const auto centre = juce::Point<float> (face.getCentreX(),
        face.getY() + face.getHeight() * 0.20f);
    const auto radius = scaled (8.0f, width, 2.6f);
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto x = centre.x + (channel == 0 ? -1.0f : 1.0f) * scaled (36.0f, width, 12.0f);
        const auto on = state.cumulativeAvailable && channel < state.meter.channels
                     && state.meter.channel_clip_latched[channel] != 0;
        if (on)
        {
            g.setColour (COL_FLORA_BR.withAlpha (0.16f));
            g.fillEllipse (x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
            const auto core = radius * 0.40f;
            g.setColour (COL_FLORA_BR.withAlpha (0.96f));
            g.fillEllipse (x - core, centre.y - core, core * 2.0f, core * 2.0f);
        }
    }
    if (! compact)
    {
        const auto dashHalfGap = scaled (53.0f, width);
        const auto dashOuter = scaled (74.0f, width);
        g.setColour (COL_MUTED.withAlpha (0.72f));
        g.drawHorizontalLine (juce::roundToInt (centre.y), centre.x - dashOuter,
                              centre.x - dashHalfGap);
        g.drawHorizontalLine (juce::roundToInt (centre.y), centre.x + dashHalfGap,
                              centre.x + dashOuter);
        drawText (g, "CLIP", { centre.x - scaled (29.0f, width),
                                centre.y - scaled (11.0f, width), scaled (58.0f, width),
                                scaled (22.0f, width) },
                  state.presentation, typography::TextRole::metricLabel,
                  typography::Composition::instrument, COL_MUTED,
                  juce::Justification::centred);
    }
}

using needle_painter::dialGeometry;
using needle_painter::dialPoint;

void paintDial (juce::Graphics& g, juce::Rectangle<float> face, const State& state,
                int channel, bool compact, bool geometry = true, bool needle = true, bool captions = true)
{
    const auto width = face.getWidth();
    const auto dial = dialGeometry (face, channel);
    const auto pivot = dial.pivot;
    if (geometry)
    {
    juce::Path arc;
    for (int step = 0; step <= 80; ++step)
    {
        const auto point = dialPoint (dial, step / 80.0f);
        if (step == 0) arc.startNewSubPath (point); else arc.lineTo (point);
    }
    g.setColour (meterIvory.withAlpha (0.10f));
    g.strokePath (arc, juce::PathStrokeType (scaled (4.5f, width)));
    g.setColour (meterIvory.withAlpha (0.90f));
    g.strokePath (arc, juce::PathStrokeType (scaled (2.35f, width)));

    for (const auto halfVu : vuTickHalves)
    {
        const auto vu = static_cast<float> (halfVu) * 0.5f;
        const auto normalized = vuNormalized (vu + referenceDbfs);
        const bool major = halfVu == -40 || halfVu == -20 || halfVu == -14
                        || halfVu == -10 || halfVu == -6 || halfVu == -2
                        || halfVu == 0 || halfVu == 2 || halfVu == 6;
        if (compact && ! major)
            continue;
        const auto inner = dialPoint (dial, normalized);
        const auto tickScale = major ? 1.13f : 1.065f;
        const auto outer = pivot + (inner - pivot) * tickScale;
        g.setColour (meterIvory.withAlpha (major ? 0.98f : 0.76f));
        g.drawLine ({ inner, outer }, scaled (major ? 2.6f : 1.4f, width));
        const bool show = major && (! compact || halfVu == -40 || halfVu == -20
                                    || halfVu == -6 || halfVu == 0 || halfVu == 6);
        if (show)
        {
            const auto edge = std::abs (normalized * 2.0f - 1.0f);
            const auto edgeLift = juce::jlimit (0.0f, 1.0f, (edge - 0.4f) / 0.6f);
            const auto labelXScale = 1.14f + edge * 0.035f;
            const auto labelYScale = 1.19f + edgeLift * edgeLift * 0.12f;
            const auto point = juce::Point<float> (
                pivot.x + (inner.x - pivot.x) * labelXScale,
                pivot.y + (inner.y - pivot.y) * labelYScale);
            drawText (g, vu > 0.0f ? "+" + juce::String ((int) vu) : juce::String ((int) vu),
                      { point.x - scaled (26.0f, width, 9.0f),
                        point.y - scaled (10.0f, width, 4.0f),
                        scaled (52.0f, width, 18.0f), scaled (20.0f, width, 8.0f) },
                      state.presentation, typography::TextRole::axis,
                      typography::Composition::instrument, meterIvory.withAlpha (0.92f),
                      juce::Justification::centred, true);
        }
    }

    }
    const auto channelAvailable = state.currentAvailable && channel < state.meter.channels
        && std::isfinite (state.meter.channel_vu_dbfs[channel]);
    const auto normalized = vuNormalized (channelAvailable ? state.meter.channel_vu_dbfs[channel]
        : std::numeric_limits<double>::quiet_NaN(), state.calibrationDbfs);
    if (needle && state.drawNeedles) needle_painter::paint (g, face, channel, normalized);

    if (captions && ! compact)
        drawText (g, "VU", { pivot.x - dial.radiusX * 0.31f,
                              pivot.y - face.getHeight() * 0.405f,
                              dial.radiusX * 0.62f, scaled (28.0f, width, 10.0f) },
                  state.presentation, typography::TextRole::sectionTitle,
                  typography::Composition::instrument, meterIvory.withAlpha (0.94f),
                  juce::Justification::centred, false, 0.08f);
    if (captions) drawText (g, channel == 0 ? "L" : (state.meter.channels > 1 ? "R" : hypha::emDash()),
              { pivot.x - dial.radiusX * 0.28f, pivot.y - face.getHeight() * 0.285f,
                dial.radiusX * 0.56f, scaled (42.0f, width, 14.0f) },
              state.presentation, typography::TextRole::secondaryValue,
              typography::Composition::instrument, COL_MUTED.brighter (0.22f),
              juce::Justification::centred);
}

void paintMeterFace (juce::Graphics& g, juce::Rectangle<float> face, const State& state,
                     bool staticPart, bool dynamicPart)
{
    const auto width = face.getWidth();
    const auto inner = face.reduced (scaled (12.0f, width, 3.0f));
    const bool compact = face.getWidth() < 410.0f;
    if (dynamicPart)
    {
        peak_rail::paint (g, inner, state, 0, compact);
        peak_rail::paint (g, inner, state, 1, compact);
    }
    if (dynamicPart) drawText (g, compact ? "TP" : "TRUE PEAK (dBTP)",
              { inner.getCentreX() - inner.getWidth() * 0.16f,
                inner.getY() + inner.getHeight() * 0.005f,
                inner.getWidth() * 0.32f, inner.getHeight() * 0.11f },
              state.presentation, typography::TextRole::metricLabel,
              typography::Composition::instrument, COL_MUTED.brighter (0.18f),
              juce::Justification::centred, false, compact ? 0.0f : 0.15f);
    if (dynamicPart) paintClipStatus (g, inner, state, compact);
    if (staticPart && dynamicPart)
    {
        paintDial (g, inner, state, 0, compact);
        paintDial (g, inner, state, 1, compact);
    }
}

void paintMetric (juce::Graphics& g, juce::Rectangle<float> area, const char* label,
                  double value, const char* unit, bool emphasized,
                  presentation::Context presentation)
{
    auto content = area.reduced (0.0f, area.getHeight() * 0.10f);
    const auto wideLabel = juce::String (label) == "CREST";
    const auto truePeakLabel = juce::String (label) == "TP";
    const auto labelFraction = wideLabel ? 0.40f : (truePeakLabel ? 0.28f : 0.245f);
    auto labelArea = content.removeFromLeft (area.getWidth() * labelFraction);
    auto unitArea = content.removeFromRight (
        area.getWidth() * (wideLabel ? 0.18f : (truePeakLabel ? 0.31f : 0.28f)));
    drawText (g, label, labelArea, presentation, typography::TextRole::metricLabel,
              typography::Composition::instrument, COL_MUTED.brighter (0.12f),
              juce::Justification::centred);
    g.setColour (COL_MUTED.withAlpha (0.40f));
    g.drawVerticalLine (juce::roundToInt (labelArea.getRight()), content.getY(), content.getBottom());
    g.setColour (std::isfinite (value)
                     ? (emphasized ? COL_FLORA_BR
                                   : COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.42f))
                     : COL_MUTED);
    drawTabularText (g, monoFont (presentation, typography::TextRole::primaryValue,
                                  typography::Composition::instrument),
                     std::isfinite (value) ? juce::String (value, 1) : juce::String ("---"),
                     content.reduced (area.getWidth()
                                          * (truePeakLabel ? 0.030f : (wideLabel ? 0.033f : 0.040f)),
                                      0.0f),
                     juce::Justification::centredRight);
    drawText (g, unit, unitArea, presentation, typography::TextRole::unit,
              typography::Composition::instrument, COL_MUTED.brighter (0.12f),
              juce::Justification::centredLeft);
}
}

float vuNormalized (double dbfs, int calibrationDbfs) noexcept
{
    if (! std::isfinite (dbfs)) return 0.0f;
    const auto reference = vu_calibration::valid (calibrationDbfs)
        ? calibrationDbfs : vu_calibration::defaultDbfs;
    const auto vu = juce::jlimit (vuScaleDb.front(), vuScaleDb.back(), dbfs - reference);
    for (size_t upper = 1; upper < vuScaleDb.size(); ++upper)
        if (vu <= vuScaleDb[upper])
            return juce::jmap (static_cast<float> (vu),
                               static_cast<float> (vuScaleDb[upper - 1]),
                               static_cast<float> (vuScaleDb[upper]),
                               vuScalePosition[upper - 1], vuScalePosition[upper]);
    return vuScalePosition.back();
}

float truePeakNormalized (double dbtp) noexcept
{
    if (! std::isfinite (dbtp)) return 0.0f;
    // The reference face retains a short unlabelled floor below -24 dBTP.  This keeps the
    // labelled divisions and +3 dBTP headroom preserve that physical lead-in.
    constexpr double floorDbtp = -28.0;
    return static_cast<float> (juce::jlimit (0.0, 1.0, (dbtp - floorDbtp) / (3.0 - floorDbtp)));
}

static void paintParts (juce::Graphics& g, juce::Rectangle<int> requested, const State& state,
                        bool staticPart, bool dynamicPart)
{
    auto bounds = requested.toFloat();
    if (staticPart)
    {
    const auto& chassis = chassisImage();
    if (chassis.isValid())
        g.drawImage (chassis, bounds, juce::RectanglePlacement::stretchToFit);
    else
    {
        g.setColour (BG.darker (0.38f));
        g.fillRect (bounds);
    }
    if (state.jungle)
        jungle_material::paintVuAcceleration (g, bounds, state.role);
    }
    auto header = juce::Rectangle<float> (
        bounds.getX() + bounds.getWidth() * 0.020f,
        bounds.getY() + bounds.getHeight() * 0.018f,
        bounds.getWidth() * 0.960f, bounds.getHeight() * 0.105f);
    auto face = juce::Rectangle<float> (
        bounds.getX() + bounds.getWidth() * 0.018f,
        bounds.getY() + bounds.getHeight() * 0.135f,
        bounds.getWidth() * 0.964f, bounds.getHeight() * 0.565f);
    auto metrics = juce::Rectangle<float> (
        bounds.getX() + bounds.getWidth() * 0.021f,
        bounds.getY() + bounds.getHeight() * 0.721f,
        bounds.getWidth() * 0.958f, bounds.getHeight() * 0.151f);
    if (dynamicPart) paintHeader (g, header, state);
    paintMeterFace (g, face, state, staticPart, dynamicPart);
    if (! dynamicPart) return;

    const auto metricGap = scaled (8.0f, bounds.getWidth(), 2.0f);
    const auto metricWidth = (metrics.getWidth() - metricGap * 2.0f) / 3.0f;
    const auto loudness = state.currentAvailable && state.watchAvailable
        ? (state.shortTermLoudness ? state.watch.current.lufs_s : state.watch.current.lufs_m)
        : std::numeric_limits<double>::quiet_NaN();
    const auto truePeak = state.currentAvailable ? state.meter.true_peak
                                                  : std::numeric_limits<double>::quiet_NaN();
    const auto crest = state.currentAvailable && state.watchAvailable
        ? state.watch.current.crest : std::numeric_limits<double>::quiet_NaN();
    paintMetric (g, metrics.removeFromLeft (metricWidth),
                 state.shortTermLoudness ? "S" : "M", loudness, "LUFS", false,
                 state.presentation);
    metrics.removeFromLeft (metricGap);
    paintMetric (g, metrics.removeFromLeft (metricWidth), "TP", truePeak, "dBTP",
                 std::isfinite (truePeak) && truePeak > truePeakEmphasisThresholdDbtp,
                 state.presentation);
    metrics.removeFromLeft (metricGap);
    paintMetric (g, metrics, "CREST", crest, "dB", false, state.presentation);
}

void paintDialLayer (juce::Graphics& g, juce::Rectangle<int> bounds, const State& state, bool captions)
{
    const auto face = needle_painter::faceBounds (bounds);
    const bool compact = bounds.getWidth() * 0.964f < 410.0f;
    for (int channel = 0; channel < 2; ++channel)
        paintDial (g, face, state, channel, compact, ! captions, false, captions);
}

void paint (juce::Graphics& g, juce::Rectangle<int> bounds, const State& state)
{ paintParts (g, bounds, state, true, true); }
void paintStatic (juce::Graphics& g, juce::Rectangle<int> bounds, const State& state)
{ paintParts (g, bounds, state, true, false); }
void paintDynamic (juce::Graphics& g, juce::Rectangle<int> bounds, const State& state)
{ paintParts (g, bounds, state, false, true); }
}
