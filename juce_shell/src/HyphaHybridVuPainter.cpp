#include "HyphaHybridVuPainter.h"

#include <BinaryData.h>

#include "HyphaTheme.h"

#include <array>
#include <cmath>
#include <limits>

namespace hypha::hybrid_vu
{
namespace
{
constexpr float pi = juce::MathConstants<float>::pi;
constexpr std::array<int, 18> vuTickHalves {
    -40, -35, -30, -25, -20, -18, -16, -14, -12, -10, -8, -6, -4, -2, 0, 2, 4, 6
};
// A physical VU face is not a linear dB ruler.  These control points reproduce the attached
// dial's engraved positions while the 300 ms measurement and 0 VU calibration remain unchanged.
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
               float fontHeight, juce::Colour colour, juce::Justification justification,
               bool tabular = false, float tracking = 0.0f)
{
    const auto font = (tabular ? monoFont (fontHeight) : labelFont (fontHeight))
                          .withExtraKerningFactor (tracking);
    g.setColour (colour);
    if (tabular)
        drawTabularText (g, font, text, area, justification);
    else
    {
        g.setFont (font);
        g.drawFittedText (text, area.toNearestInt(), justification, 1, 0.72f);
    }
}

void paintHeader (juce::Graphics& g, juce::Rectangle<float> area, const State& state)
{
    const auto width = area.getWidth();
    auto title = area.reduced (scaled (25.0f, width, 6.5f), 0.0f);
    const auto titleWidth = title.getWidth() * 0.36f;
    auto titleArea = title.removeFromLeft (titleWidth);
    const auto fontHeight = juce::jlimit (10.0f, 38.0f, width * 0.039f);
    const auto roleWidth = titleArea.getWidth() * (width < 450.0f ? 0.38f : 0.30f);
    drawText (g, state.role == observatory::Role::post ? "POST" : "PRE",
              titleArea.removeFromLeft (roleWidth), fontHeight,
              state.role == observatory::Role::post ? COL_FLORA : COL_LED_BLUE,
              juce::Justification::centredLeft, false, 0.12f);
    titleArea.removeFromLeft (scaled (5.0f, width, 1.0f));
    drawText (g, width >= 450.0f ? "H Y P H A" : "HYPHA", titleArea,
              fontHeight * 0.87f, COL_NORMAL, juce::Justification::centredLeft);

    auto record = title.removeFromRight (title.getWidth() * (width < 450.0f ? 0.28f : 0.205f));
    auto connection = title;
    if (width >= 450.0f)
        connection.removeFromLeft (connection.getWidth() * 0.60f);
    const auto dot = juce::jlimit (3.0f, 11.0f, width * 0.010f);
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
    drawText (g, state.connectionText, connection.reduced (0.0f, area.getHeight() * 0.15f),
              fontHeight * 0.66f, brightConnection,
              juce::Justification::centredLeft);

    g.setColour (COL_MUTED.withAlpha (0.46f));
    g.drawVerticalLine (juce::roundToInt (record.getX()),
                        record.getY() + area.getHeight() * 0.24f,
                        record.getBottom() - area.getHeight() * 0.24f);
    const auto recordDot = juce::Point<float> (record.getX() + record.getWidth() * 0.42f,
                                                record.getCentreY());
    const auto recordColour = COL_MUTED.brighter (0.30f);
    g.setColour (recordColour.withAlpha (0.11f));
    g.fillEllipse (recordDot.x - dot, recordDot.y - dot, dot * 2.0f, dot * 2.0f);
    g.setColour (recordColour);
    g.fillEllipse (recordDot.x - dot * 0.48f, recordDot.y - dot * 0.48f,
                   dot * 0.96f, dot * 0.96f);
    record.removeFromLeft (record.getWidth() * 0.60f);
    drawText (g, "REC", record, fontHeight * 0.68f, recordColour,
              juce::Justification::centredLeft, false, 0.10f);
}

juce::Point<float> peakRailPoint (juce::Rectangle<float> face, int channel, float amount)
{
    const auto inner = channel == 0 ? 0.384f : 0.616f;
    const auto outer = channel == 0 ? 0.071f : 0.929f;
    const auto x = face.getX() + face.getWidth() * juce::jmap (amount, outer, inner);
    const auto y = face.getY() + face.getHeight()
        * (0.225f - 0.060f * std::sin (amount * pi * 0.5f));
    return { x, y };
}

void paintPeakRail (juce::Graphics& g, juce::Rectangle<float> face, const State& state,
                    int channel, bool compact)
{
    const auto available = state.currentAvailable && channel < state.meter.channels
        && std::isfinite (state.meter.channel_instant_true_peak_dbtp[channel]);
    const auto progress = available
        ? truePeakNormalized (state.meter.channel_instant_true_peak_dbtp[channel]) : 0.0f;
    const int segments = compact ? 21 : 49;
    const auto width = face.getWidth();
    for (int index = 0; index < segments; ++index)
    {
        const auto amount = static_cast<float> (index) / static_cast<float> (segments - 1);
        const auto point = peakRailPoint (face, channel, amount);
        const auto lit = amount <= progress;
        const auto emphasized = lit && amount >= truePeakNormalized (-24.0);
        const auto height = scaled (index % (compact ? 4 : 7) == 0 ? 12.0f : 9.0f,
                                    width, compact ? 2.4f : 3.5f);
        if (emphasized)
        {
            const auto segmentWidth = scaled (compact ? 4.0f : 5.6f, width, 1.3f);
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.14f));
            g.fillRoundedRectangle (point.x - segmentWidth,
                                    point.y - height * 0.75f,
                                    segmentWidth * 2.0f, height * 1.5f,
                                    segmentWidth * 0.55f);
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.96f));
            g.fillRoundedRectangle (point.x - segmentWidth * 0.5f, point.y - height * 0.5f,
                                    segmentWidth, height, segmentWidth * 0.28f);
        }
        else
        {
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.78f));
            g.drawVerticalLine (juce::roundToInt (point.x), point.y - height * 0.5f,
                                point.y + height * 0.5f);
        }
    }

    if (state.cumulativeAvailable && channel < state.meter.channels
        && std::isfinite (state.meter.channel_max_true_peak_dbtp[channel]))
    {
        const auto held = peakRailPoint (
            face, channel, truePeakNormalized (state.meter.channel_max_true_peak_dbtp[channel]));
        g.setColour (COL_FLORA_BR.withAlpha (0.17f));
        g.fillRoundedRectangle (held.x - scaled (5.0f, width),
                                held.y - scaled (15.0f, width),
                                scaled (10.0f, width), scaled (30.0f, width),
                                scaled (4.0f, width));
        g.setColour (COL_FLORA_BR);
        g.fillRoundedRectangle (held.x - scaled (2.0f, width), held.y - scaled (12.0f, width),
                                scaled (4.0f, width), scaled (24.0f, width),
                                scaled (1.5f, width));
    }

    const std::array<int, 5> labels { -24, -18, -12, -6, 0 };
    for (const auto value : labels)
    {
        if (compact && value != -24)
            continue;
        const auto point = peakRailPoint (face, channel, truePeakNormalized (value));
        const auto labelWidth = scaled (34.0f, width, 14.0f);
        drawText (g, juce::String (value),
                  { point.x - labelWidth * 0.5f, point.y - scaled (28.0f, width, 11.0f),
                    labelWidth, scaled (17.0f, width, 7.0f) },
                  scaled (14.0f, width, 5.4f), COL_NORMAL.withAlpha (0.82f),
                  juce::Justification::centred, true);
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
                     && state.meter.clip_events[channel] > 0;
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
                  scaled (11.0f, width, 5.2f), COL_MUTED,
                  juce::Justification::centred);
    }
}

struct DialGeometry
{
    juce::Point<float> pivot;
    float radiusX;
    float topRise;
    float edgeFall;
};

DialGeometry dialGeometry (juce::Rectangle<float> face, int channel)
{
    const auto half = face.getWidth() * 0.5f;
    return {
        { face.getX() + half * (channel == 0 ? 0.515f : 1.475f),
          face.getY() + face.getHeight() * 0.865f },
        juce::jmin (half * 0.438f, face.getHeight() * 0.59f),
        face.getHeight() * 0.483f,
        face.getHeight() * 0.136f
    };
}

juce::Point<float> dialPoint (const DialGeometry& dial, float normalized)
{
    const auto angle = juce::jmap (normalized, -2.48f, -0.74f);
    const auto edge = std::abs (normalized * 2.0f - 1.0f);
    return { dial.pivot.x + std::cos (angle) * dial.radiusX,
             dial.pivot.y - dial.topRise + dial.edgeFall * std::pow (edge, 1.72f) };
}

void paintDial (juce::Graphics& g, juce::Rectangle<float> face, const State& state,
                int channel, bool compact)
{
    const auto width = face.getWidth();
    const auto dial = dialGeometry (face, channel);
    const auto pivot = dial.pivot;
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
                      scaled (14.5f, width, 5.6f), meterIvory.withAlpha (0.92f),
                      juce::Justification::centred, true);
        }
    }

    const auto channelAvailable = state.currentAvailable && channel < state.meter.channels
        && std::isfinite (state.meter.channel_vu_dbfs[channel]);
    const auto normalized = vuNormalized (channelAvailable ? state.meter.channel_vu_dbfs[channel]
                                                            : referenceDbfs - 40.0);
    const auto scalePoint = dialPoint (dial, normalized);
    const auto needleEnd = pivot + (scalePoint - pivot) * 1.11f;
    g.setColour (COL_FLORA_BR.withAlpha (0.20f));
    g.drawLine ({ pivot, needleEnd }, scaled (5.2f, width, 1.5f));
    g.setColour (meterIvory.brighter (0.09f));
    g.drawLine ({ pivot, needleEnd }, scaled (2.7f, width, 0.95f));

    if (! compact)
        drawText (g, "VU", { pivot.x - dial.radiusX * 0.31f,
                              pivot.y - face.getHeight() * 0.405f,
                              dial.radiusX * 0.62f, scaled (28.0f, width, 10.0f) },
                  scaled (20.5f, width, 7.0f), meterIvory.withAlpha (0.94f),
                  juce::Justification::centred, false, 0.08f);
    drawText (g, channel == 0 ? "L" : (state.meter.channels > 1 ? "R" : hypha::emDash()),
              { pivot.x - dial.radiusX * 0.28f, pivot.y - face.getHeight() * 0.285f,
                dial.radiusX * 0.56f, scaled (42.0f, width, 14.0f) },
              scaled (30.5f, width, 9.0f), COL_MUTED.brighter (0.22f),
              juce::Justification::centred);
}

void paintMeterFace (juce::Graphics& g, juce::Rectangle<float> face, const State& state)
{
    const auto width = face.getWidth();
    const auto inner = face.reduced (scaled (12.0f, width, 3.0f));
    const bool compact = face.getWidth() < 410.0f;
    paintPeakRail (g, inner, state, 0, compact);
    paintPeakRail (g, inner, state, 1, compact);
    drawText (g, compact ? "TP" : "TRUE PEAK (dBTP)",
              { inner.getCentreX() - inner.getWidth() * 0.16f,
                inner.getY() + inner.getHeight() * 0.005f,
                inner.getWidth() * 0.32f, inner.getHeight() * 0.11f },
              scaled (15.0f, width, 5.2f), COL_MUTED.brighter (0.18f),
              juce::Justification::centred, false, compact ? 0.0f : 0.15f);
    paintClipStatus (g, inner, state, compact);
    paintDial (g, inner, state, 0, compact);
    paintDial (g, inner, state, 1, compact);
}

void paintMetric (juce::Graphics& g, juce::Rectangle<float> area, const char* label,
                  double value, const char* unit, bool emphasized)
{
    auto content = area.reduced (0.0f, area.getHeight() * 0.10f);
    const auto wideLabel = juce::String (label) == "CREST";
    const auto truePeakLabel = juce::String (label) == "TP";
    const auto labelFraction = wideLabel ? 0.40f : (truePeakLabel ? 0.28f : 0.245f);
    auto labelArea = content.removeFromLeft (area.getWidth() * labelFraction);
    auto unitArea = content.removeFromRight (
        area.getWidth() * (wideLabel ? 0.18f : (truePeakLabel ? 0.31f : 0.28f)));
    drawText (g, label, labelArea, juce::jlimit (7.0f, 22.0f, area.getHeight() * 0.29f),
              COL_MUTED.brighter (0.12f), juce::Justification::centred);
    g.setColour (COL_MUTED.withAlpha (0.40f));
    g.drawVerticalLine (juce::roundToInt (labelArea.getRight()), content.getY(), content.getBottom());
    g.setColour (std::isfinite (value)
                     ? (emphasized ? COL_FLORA_BR
                                   : COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.42f))
                     : COL_MUTED);
    drawTabularText (g,
                     monoFont (juce::jlimit (13.0f, 58.0f, area.getHeight() * 0.65f))
                         .withHorizontalScale (wideLabel || truePeakLabel ? 0.66f : 0.68f),
                     std::isfinite (value) ? juce::String (value, 1) : juce::String ("---"),
                     content.reduced (area.getWidth()
                                          * (truePeakLabel ? 0.030f : (wideLabel ? 0.033f : 0.040f)),
                                      0.0f),
                     juce::Justification::centredRight);
    drawText (g, unit, unitArea, juce::jlimit (6.5f, 18.0f, area.getHeight() * 0.25f),
              COL_MUTED.brighter (0.12f), juce::Justification::centredLeft);
}
}

float vuNormalized (double dbfs) noexcept
{
    if (! std::isfinite (dbfs)) return 0.0f;
    const auto vu = juce::jlimit (vuScaleDb.front(), vuScaleDb.back(), dbfs - referenceDbfs);
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
    // five labelled 6 dB divisions evenly spaced while preserving that physical lead-in.
    constexpr double floorDbtp = -28.0;
    return static_cast<float> (juce::jlimit (0.0, 1.0, (dbtp - floorDbtp) / -floorDbtp));
}

void paint (juce::Graphics& g, juce::Rectangle<int> requested, const State& state)
{
    auto bounds = requested.toFloat();
    const auto& chassis = chassisImage();
    if (chassis.isValid())
        g.drawImage (chassis, bounds, juce::RectanglePlacement::stretchToFit);
    else
    {
        g.setColour (BG.darker (0.38f));
        g.fillRect (bounds);
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
    auto calibration = juce::Rectangle<float> (
        bounds.getX() + bounds.getWidth() * 0.021f,
        bounds.getY() + bounds.getHeight() * 0.880f,
        bounds.getWidth() * 0.958f, bounds.getHeight() * 0.095f);
    paintHeader (g, header, state);
    paintMeterFace (g, face, state);

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
                 state.shortTermLoudness ? "S" : "M", loudness, "LUFS", false);
    metrics.removeFromLeft (metricGap);
    paintMetric (g, metrics.removeFromLeft (metricWidth), "TP", truePeak, "dBTP",
                 std::isfinite (truePeak) && truePeak > truePeakEmphasisThresholdDbtp);
    metrics.removeFromLeft (metricGap);
    paintMetric (g, metrics, "CREST", crest, "dB", false);
    drawText (g, "0 VU = -18 dBFS", calibration,
              juce::jlimit (6.0f, 15.0f, calibration.getHeight() * 0.42f),
              COL_MUTED.withAlpha (0.78f), juce::Justification::centred, true, 0.08f);
}
}
