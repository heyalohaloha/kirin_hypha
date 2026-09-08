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
               bool tabular = false)
{
    const auto font = tabular ? monoFont (fontHeight) : labelFont (fontHeight);
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
    const auto hyphaWidth = titleArea.getWidth() * 0.44f;
    drawText (g, width >= 450.0f ? "H Y P H A" : "HYPHA",
              titleArea.removeFromLeft (hyphaWidth), fontHeight,
              COL_NORMAL, juce::Justification::centredLeft);
    titleArea.removeFromLeft (scaled (10.0f, width, 2.0f));
    drawText (g, state.role == observatory::Role::post ? "POST" : "PRE", titleArea,
              fontHeight * 0.87f, COL_MUTED.brighter (0.18f),
              juce::Justification::centredLeft);

    auto record = title.removeFromRight (title.getWidth() * 0.20f);
    auto connection = title;
    if (width >= 450.0f)
        connection.removeFromLeft (connection.getWidth() * 0.60f);
    const auto dot = juce::jlimit (3.0f, 11.0f, width * 0.010f);
    const auto connectionDot = juce::Point<float> (connection.getX() + dot,
                                                    connection.getCentreY());
    g.setColour (state.connectionColour.withAlpha (0.13f));
    g.fillEllipse (connectionDot.x - dot, connectionDot.y - dot, dot * 2.0f, dot * 2.0f);
    g.setColour (state.connectionColour);
    g.fillEllipse (connectionDot.x - dot * 0.45f, connectionDot.y - dot * 0.45f,
                   dot * 0.9f, dot * 0.9f);
    connection.removeFromLeft (dot * 3.4f);
    drawText (g, state.connectionText, connection.reduced (0.0f, area.getHeight() * 0.15f),
              fontHeight * 0.66f, state.connectionColour,
              juce::Justification::centredLeft);

    g.setColour (COL_MUTED.withAlpha (0.46f));
    g.drawVerticalLine (juce::roundToInt (record.getX()),
                        record.getY() + area.getHeight() * 0.24f,
                        record.getBottom() - area.getHeight() * 0.24f);
    const auto recordDot = juce::Point<float> (record.getX() + record.getWidth() * 0.45f,
                                                record.getCentreY());
    g.setColour (COL_FLORA_BR.withAlpha (0.11f));
    g.fillEllipse (recordDot.x - dot, recordDot.y - dot, dot * 2.0f, dot * 2.0f);
    g.setColour (COL_FLORA_BR);
    g.fillEllipse (recordDot.x - dot * 0.48f, recordDot.y - dot * 0.48f,
                   dot * 0.96f, dot * 0.96f);
    record.removeFromLeft (record.getWidth() * 0.62f);
    drawText (g, "REC", record, fontHeight * 0.68f, COL_NORMAL.withAlpha (0.88f),
              juce::Justification::centredLeft);
}

juce::Point<float> peakRailPoint (juce::Rectangle<float> face, int channel, float amount)
{
    const auto inner = channel == 0 ? 0.390f : 0.610f;
    const auto outer = channel == 0 ? 0.075f : 0.925f;
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
    const int segments = compact ? 17 : 43;
    const auto width = face.getWidth();
    for (int index = 0; index < segments; ++index)
    {
        const auto amount = static_cast<float> (index) / static_cast<float> (segments - 1);
        const auto point = peakRailPoint (face, channel, amount);
        const auto lit = amount <= progress;
        const auto emphasized = lit && amount >= 0.07f;
        const auto height = scaled (index % (compact ? 3 : 6) == 0 ? 10.0f : 7.0f,
                                    width, compact ? 2.0f : 3.0f);
        if (emphasized)
        {
            const auto segmentWidth = scaled (compact ? 4.0f : 5.2f, width, 1.3f);
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.96f));
            g.fillRoundedRectangle (point.x - segmentWidth * 0.5f, point.y - height * 0.5f,
                                    segmentWidth, height, segmentWidth * 0.28f);
        }
        else
        {
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.66f));
            g.drawVerticalLine (juce::roundToInt (point.x), point.y - height * 0.5f,
                                point.y + height * 0.5f);
        }
    }

    if (state.cumulativeAvailable && channel < state.meter.channels
        && std::isfinite (state.meter.channel_max_true_peak_dbtp[channel]))
    {
        const auto held = peakRailPoint (
            face, channel, truePeakNormalized (state.meter.channel_max_true_peak_dbtp[channel]));
        g.setColour (COL_FLORA_BR);
        g.fillRoundedRectangle (held.x - scaled (2.0f, width), held.y - scaled (11.0f, width),
                                scaled (4.0f, width), scaled (22.0f, width),
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
        g.setColour ((on ? COL_FLORA_BR : COL_MUTED).withAlpha (on ? 0.90f : 0.24f));
        g.fillEllipse (x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (COL_MUTED.withAlpha (0.72f));
        g.drawEllipse (x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f,
                       scaled (1.0f, width));
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
        { face.getX() + half * (channel == 0 ? 0.52f : 1.48f),
          face.getY() + face.getHeight() * 0.86f },
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
    g.setColour (meterIvory.withAlpha (0.86f));
    g.strokePath (arc, juce::PathStrokeType (scaled (1.9f, width)));

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
        g.setColour (meterIvory.withAlpha (major ? 0.96f : 0.72f));
        g.drawLine ({ inner, outer }, scaled (major ? 2.3f : 1.1f, width));
        const bool show = major && (! compact || halfVu == -40 || halfVu == -20
                                    || halfVu == -6 || halfVu == 0 || halfVu == 6);
        if (show)
        {
            const auto point = juce::Point<float> (
                pivot.x + (inner.x - pivot.x) * 1.14f,
                pivot.y + (inner.y - pivot.y) * 1.19f);
            drawText (g, vu > 0.0f ? "+" + juce::String ((int) vu) : juce::String ((int) vu),
                      { point.x - scaled (26.0f, width, 9.0f),
                        point.y - scaled (10.0f, width, 4.0f),
                        scaled (52.0f, width, 18.0f), scaled (20.0f, width, 8.0f) },
                      scaled (14.5f, width, 5.6f), COL_NORMAL.withAlpha (0.88f),
                      juce::Justification::centred, true);
        }
    }

    const auto channelAvailable = state.currentAvailable && channel < state.meter.channels
        && std::isfinite (state.meter.channel_vu_dbfs[channel]);
    const auto normalized = vuNormalized (channelAvailable ? state.meter.channel_vu_dbfs[channel]
                                                            : referenceDbfs - 40.0);
    const auto scalePoint = dialPoint (dial, normalized);
    const auto needleEnd = pivot + (scalePoint - pivot) * 1.11f;
    g.setColour (COL_FLORA_BR.withAlpha (0.22f));
    g.drawLine ({ pivot, needleEnd }, scaled (5.5f, width, 1.5f));
    g.setColour (meterIvory.brighter (0.06f));
    g.drawLine ({ pivot, needleEnd }, scaled (2.0f, width, 0.9f));

    if (! compact)
        drawText (g, "VU", { pivot.x - dial.radiusX * 0.31f,
                              pivot.y - face.getHeight() * 0.405f,
                              dial.radiusX * 0.62f, scaled (28.0f, width, 10.0f) },
                  scaled (19.0f, width, 7.0f), meterIvory.withAlpha (0.92f),
                  juce::Justification::centred);
    drawText (g, channel == 0 ? "L" : (state.meter.channels > 1 ? "R" : hypha::emDash()),
              { pivot.x - dial.radiusX * 0.28f, pivot.y - face.getHeight() * 0.285f,
                dial.radiusX * 0.56f, scaled (42.0f, width, 14.0f) },
              scaled (29.0f, width, 9.0f), COL_MUTED.brighter (0.22f),
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
              juce::Justification::centred);
    paintClipStatus (g, inner, state, compact);
    paintDial (g, inner, state, 0, compact);
    paintDial (g, inner, state, 1, compact);
}

void paintMetric (juce::Graphics& g, juce::Rectangle<float> area, const char* label,
                  double value, const char* unit, bool emphasized)
{
    auto content = area.reduced (0.0f, area.getHeight() * 0.10f);
    const auto wideLabel = juce::String (label) == "CREST";
    const auto labelFraction = wideLabel ? 0.40f : (juce::String (label) == "TP" ? 0.28f : 0.245f);
    auto labelArea = content.removeFromLeft (area.getWidth() * labelFraction);
    auto unitArea = content.removeFromRight (
        area.getWidth() * (wideLabel ? 0.18f : (juce::String (label) == "TP" ? 0.31f : 0.28f)));
    drawText (g, label, labelArea, juce::jlimit (7.0f, 20.0f, area.getHeight() * 0.27f),
              COL_MUTED.brighter (0.12f), juce::Justification::centred);
    g.setColour (COL_MUTED.withAlpha (0.40f));
    g.drawVerticalLine (juce::roundToInt (labelArea.getRight()), content.getY(), content.getBottom());
    g.setColour (std::isfinite (value)
                     ? (emphasized ? COL_FLORA_BR
                                   : COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.42f))
                     : COL_MUTED);
    drawTabularText (g,
                     monoFont (juce::jlimit (13.0f, 50.0f, area.getHeight() * 0.60f))
                         .withHorizontalScale (0.82f),
                     std::isfinite (value) ? juce::String (value, 1) : juce::String ("---"),
                     content.reduced (area.getWidth() * 0.01f, 0.0f),
                     juce::Justification::centred);
    drawText (g, unit, unitArea, juce::jlimit (6.5f, 18.0f, area.getHeight() * 0.25f),
              COL_MUTED.brighter (0.12f), juce::Justification::centredLeft);
}
}

float vuNormalized (double dbfs) noexcept
{
    if (! std::isfinite (dbfs)) return 0.0f;
    if (dbfs < referenceDbfs - 20.0) return 0.0f;
    const auto vu = juce::jlimit (-20.0, 3.0, dbfs - referenceDbfs);
    const auto maximum = std::pow (10.0, 3.0 / 20.0);
    return static_cast<float> (std::pow (10.0, vu / 20.0) / maximum);
}

float truePeakNormalized (double dbtp) noexcept
{
    if (! std::isfinite (dbtp)) return 0.0f;
    return static_cast<float> (juce::jlimit (0.0, 1.0, (dbtp + 24.0) / 24.0));
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
              COL_MUTED.withAlpha (0.78f), juce::Justification::centred, true);
}
}
