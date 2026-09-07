#include "HyphaReferenceVisuals.h"

#include "HyphaReferenceComponent.h"
#include "HyphaTheme.h"

#include <algorithm>
#include <cmath>

namespace hypha::reference_ui
{
namespace
{
using Measurement = reference_audition::RuntimeDetailedMeasurement;
using NullableSeries = reference_audition::RuntimeNullableIntegerSeries;

constexpr double minimumSpectrumDb = -120.0;
constexpr double maximumSpectrumDb = 6.0;

void panel (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (BG.withAlpha (0.72f));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 0.8f);
}

juce::Rectangle<float> chartArea (juce::Graphics& g, juce::Rectangle<float> area,
                                  const juce::String& heading,
                                  const juce::String& detail)
{
    panel (g, area);
    auto header = area.removeFromTop (28.0f);
    g.setColour (COL_NORMAL.withAlpha (0.86f));
    g.setFont (labelFont (9.0f));
    g.drawFittedText (heading, header.reduced (9.0f, 1.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.78f);
    g.setColour (COL_MUTED.withAlpha (0.78f));
    g.setFont (labelFont (7.5f));
    g.drawFittedText (detail, header.reduced (9.0f, 1.0f).toNearestInt(),
                      juce::Justification::centredRight, 1, 0.62f);
    auto chart = area.reduced (10.0f, 8.0f);
    g.setColour (COL_MUTED.withAlpha (0.10f));
    for (int line = 1; line < 4; ++line)
    {
        const auto y = chart.getY() + chart.getHeight() * line / 4.0f;
        g.drawHorizontalLine (juce::roundToInt (y), chart.getX(), chart.getRight());
    }
    return chart;
}

void unavailable (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (COL_MUTED.withAlpha (0.82f));
    g.setFont (labelFont (9.0f));
    g.drawFittedText ("REFERENCE FACTS NOT AVAILABLE / AUDIO REMAINS READY",
                      area.toNearestInt(), juce::Justification::centred, 2, 0.72f);
}

float dbY (double db, juce::Rectangle<float> area)
{
    const auto bounded = juce::jlimit (minimumSpectrumDb, maximumSpectrumDb, db);
    return area.getBottom() - static_cast<float> (
        (bounded - minimumSpectrumDb) / (maximumSpectrumDb - minimumSpectrumDb))
        * area.getHeight();
}

float logX (double frequency, double minimum, double maximum,
            juce::Rectangle<float> area)
{
    if (minimum <= 0.0 || maximum <= minimum || frequency <= 0.0)
        return area.getX();
    const auto normalized = std::log (juce::jlimit (minimum, maximum, frequency) / minimum)
                          / std::log (maximum / minimum);
    return area.getX() + static_cast<float> (normalized) * area.getWidth();
}

template <typename Values>
juce::Path spectrumPath (const std::vector<double>& frequencies, const Values& values,
                         double minimumHz, double maximumHz,
                         juce::Rectangle<float> area, double divisor)
{
    juce::Path path;
    bool started = false;
    const auto count = std::min (frequencies.size(), values.size());
    for (size_t index = 0; index < count; ++index)
    {
        if (frequencies[index] < minimumHz || frequencies[index] > maximumHz)
            continue;
        const auto x = logX (frequencies[index], minimumHz, maximumHz, area);
        const auto y = dbY (static_cast<double> (values[index]) / divisor, area);
        if (! started) { path.startNewSubPath (x, y); started = true; }
        else path.lineTo (x, y);
    }
    return path;
}

juce::Path nullableSpectrumPath (const std::vector<double>& frequencies,
                                 const std::vector<std::optional<std::int64_t>>& values,
                                 double minimumHz, double maximumHz,
                                 juce::Rectangle<float> area)
{
    juce::Path path;
    bool started = false;
    const auto count = std::min (frequencies.size(), values.size());
    for (size_t index = 0; index < count; ++index)
    {
        if (! values[index] || frequencies[index] < minimumHz || frequencies[index] > maximumHz)
        {
            started = false;
            continue;
        }
        const auto point = juce::Point<float> {
            logX (frequencies[index], minimumHz, maximumHz, area),
            dbY (static_cast<double> (*values[index]) / 1000.0, area),
        };
        if (! started) { path.startNewSubPath (point); started = true; }
        else path.lineTo (point);
    }
    return path;
}

bool drawSpectrum (juce::Graphics& g, juce::Rectangle<float> bounds,
                   const State& state, bool lowOnly)
{
    const double minimumHz = 20.0;
    const double maximumHz = lowOnly ? 300.0 : 20'000.0;
    auto area = chartArea (g, bounds, lowOnly ? "LOW FREQUENCY" : "SPECTRUM",
                           "A LIVE  /  B REFERENCE");
    bool drew = false;
    for (const auto& profile : state.profiles)
    {
        if (profile == nullptr) continue;
        const auto view = profile->views.find ("spectrum");
        if (view == profile->views.end()) continue;
        const auto series = view->second.series.find ("level_millidbfs");
        if (series == view->second.series.end()) continue;
        const auto median = nullableSpectrumPath (view->second.axis, series->second.median,
                                                  minimumHz, maximumHz, area);
        g.setColour (COL_GUIDE.withAlpha (0.28f));
        g.strokePath (median, juce::PathStrokeType (1.0f));
        drew = true;
    }
    if (state.detailedMeasurement && state.detailedMeasurement->spectrum)
    {
        const auto& spectrum = *state.detailedMeasurement->spectrum;
        const auto lower = spectrumPath (spectrum.bandCentersHz, spectrum.p10Millidbfs,
                                         minimumHz, maximumHz, area, 1000.0);
        const auto median = spectrumPath (spectrum.bandCentersHz, spectrum.medianMillidbfs,
                                          minimumHz, maximumHz, area, 1000.0);
        const auto upper = spectrumPath (spectrum.bandCentersHz, spectrum.p90Millidbfs,
                                         minimumHz, maximumHz, area, 1000.0);
        g.setColour (COL_FLORA.withAlpha (0.18f));
        g.strokePath (lower, juce::PathStrokeType (0.8f));
        g.strokePath (upper, juce::PathStrokeType (0.8f));
        g.setColour (COL_FLORA.withAlpha (0.82f));
        g.strokePath (median, juce::PathStrokeType (1.5f));
        drew = true;
    }
    if (! state.liveSpectrumDbfs.empty()
        && state.liveSpectrumMinimumHz > 0.0f
        && state.liveSpectrumMaximumHz > state.liveSpectrumMinimumHz)
    {
        juce::Path live;
        bool started = false;
        const auto count = state.liveSpectrumDbfs.size();
        for (size_t index = 0; index < count; ++index)
        {
            const auto fraction = count > 1 ? static_cast<double> (index) / (count - 1) : 0.0;
            const auto frequency = state.liveSpectrumMinimumHz
                * std::pow (state.liveSpectrumMaximumHz / state.liveSpectrumMinimumHz, fraction);
            if (frequency < minimumHz || frequency > maximumHz) continue;
            const auto point = juce::Point<float> {
                logX (frequency, minimumHz, maximumHz, area),
                dbY (state.liveSpectrumDbfs[index], area),
            };
            if (! started) { live.startNewSubPath (point); started = true; }
            else live.lineTo (point);
        }
        g.setColour (COL_SPECTRUM_POST.withAlpha (0.92f));
        g.strokePath (live, juce::PathStrokeType (1.25f));
        drew = true;
    }
    if (! drew) unavailable (g, area);
    return drew;
}

bool drawWaveform (juce::Graphics& g, juce::Rectangle<float> bounds, const State& state)
{
    auto area = chartArea (g, bounds, "WAVEFORM", "B REFERENCE / SAMPLE GRID");
    if (! state.detailedMeasurement || ! state.detailedMeasurement->waveform)
    {
        unavailable (g, area);
        return false;
    }
    const auto& waveform = *state.detailedMeasurement->waveform;
    if (waveform.samplePeakMillidbfs.empty() || waveform.samplePeakMillidbfs.front().empty())
    {
        unavailable (g, area);
        return false;
    }
    const auto bins = waveform.samplePeakMillidbfs.front().size();
    juce::Path peak;
    for (size_t index = 0; index < bins; ++index)
    {
        double value = 0.0;
        for (const auto& channel : waveform.samplePeakMillidbfs)
            value = std::max (value, std::pow (10.0, channel[index] / 20'000.0));
        const auto x = area.getX() + area.getWidth() * static_cast<float> (index)
                                  / static_cast<float> (std::max<size_t> (1, bins - 1));
        const auto y = area.getCentreY() - static_cast<float> (value) * area.getHeight() * 0.46f;
        if (index == 0) peak.startNewSubPath (x, y); else peak.lineTo (x, y);
    }
    for (size_t reverse = bins; reverse-- > 0;)
    {
        double value = 0.0;
        for (const auto& channel : waveform.samplePeakMillidbfs)
            value = std::max (value, std::pow (10.0, channel[reverse] / 20'000.0));
        const auto x = area.getX() + area.getWidth() * static_cast<float> (reverse)
                                  / static_cast<float> (std::max<size_t> (1, bins - 1));
        peak.lineTo (x, area.getCentreY() + static_cast<float> (value) * area.getHeight() * 0.46f);
    }
    peak.closeSubPath();
    g.setColour (COL_FLORA.withAlpha (0.24f));
    g.fillPath (peak);
    g.setColour (COL_FLORA.withAlpha (0.74f));
    g.strokePath (peak, juce::PathStrokeType (0.8f));
    return true;
}

const NullableSeries* timelineSeries (const Measurement& measurement,
                                      const juce::String& binding,
                                      juce::String& title, juce::String& seriesName,
                                      double& minimum, double& maximum)
{
    const reference_audition::RuntimeMeasurementTimeline* timeline = nullptr;
    if (binding == "loudness" && measurement.loudness)
    {
        timeline = &*measurement.loudness;
        title = "LOUDNESS"; seriesName = "lufs_s_millilu"; minimum = -60'000; maximum = 0;
    }
    else if (binding == "dynamics" && measurement.dynamics)
    {
        timeline = &*measurement.dynamics;
        title = "DYNAMICS"; seriesName = "psr_millidb"; minimum = 0; maximum = 30'000;
    }
    else if (binding == "stereo" && measurement.stereo)
    {
        timeline = &*measurement.stereo;
        title = "STEREO"; seriesName = "correlation_milli"; minimum = -1'000; maximum = 1'000;
    }
    if (timeline == nullptr) return nullptr;
    const auto found = timeline->series.find (seriesName.toStdString());
    return found == timeline->series.end() ? nullptr : &found->second;
}

bool drawTimeline (juce::Graphics& g, juce::Rectangle<float> bounds,
                   const State& state, const juce::String& binding)
{
    juce::String title = binding.toUpperCase();
    juce::String seriesName;
    double minimum = 0.0, maximum = 1.0;
    const NullableSeries* series = nullptr;
    if (state.detailedMeasurement)
        series = timelineSeries (*state.detailedMeasurement, binding, title, seriesName,
                                 minimum, maximum);
    auto area = chartArea (g, bounds, title, "B REFERENCE / SOURCE TIMELINE");
    if (series == nullptr || series->empty())
    {
        unavailable (g, area);
        return false;
    }
    juce::Path path;
    bool started = false;
    for (size_t index = 0; index < series->size(); ++index)
    {
        if (! (*series)[index]) { started = false; continue; }
        const auto normalized = juce::jlimit (0.0, 1.0,
            (static_cast<double> (*(*series)[index]) - minimum) / (maximum - minimum));
        const auto point = juce::Point<float> {
            area.getX() + area.getWidth() * static_cast<float> (index)
                              / static_cast<float> (std::max<size_t> (1, series->size() - 1)),
            area.getBottom() - static_cast<float> (normalized) * area.getHeight(),
        };
        if (! started) { path.startNewSubPath (point); started = true; }
        else path.lineTo (point);
    }
    g.setColour (COL_FLORA.withAlpha (0.88f));
    g.strokePath (path, juce::PathStrokeType (1.35f));
    return true;
}

bool drawTransient (juce::Graphics& g, juce::Rectangle<float> bounds, const State& state)
{
    auto area = chartArea (g, bounds, "TRANSIENT", "B REFERENCE / ONSET STRENGTH");
    if (! state.detailedMeasurement || ! state.detailedMeasurement->transient
        || state.detailedMeasurement->transient->onsetStrengthQ15.empty())
    {
        unavailable (g, area);
        return false;
    }
    const auto& values = state.detailedMeasurement->transient->onsetStrengthQ15;
    juce::Path path;
    path.startNewSubPath (area.getX(), area.getBottom());
    for (size_t index = 0; index < values.size(); ++index)
    {
        const auto x = area.getX() + area.getWidth() * static_cast<float> (index)
                                  / static_cast<float> (std::max<size_t> (1, values.size() - 1));
        const auto y = area.getBottom() - area.getHeight()
            * static_cast<float> (values[index]) / 32'767.0f;
        path.lineTo (x, y);
    }
    g.setColour (COL_FLORA.withAlpha (0.86f));
    g.strokePath (path, juce::PathStrokeType (1.2f));
    return true;
}

void paintOne (juce::Graphics& g, juce::Rectangle<float> area,
               const State& state, const juce::String& binding)
{
    if (binding == "spectrum_full") drawSpectrum (g, area, state, false);
    else if (binding == "spectrum_low") drawSpectrum (g, area, state, true);
    else if (binding == "waveform") drawWaveform (g, area, state);
    else if (binding == "transient") drawTransient (g, area, state);
    else drawTimeline (g, area, state, binding);
}
}

bool paintConfiguredReferenceViews (juce::Graphics& g, juce::Rectangle<float> area,
                                    const State& state)
{
    if (state.viewBindings.empty() || area.getWidth() < 120.0f || area.getHeight() < 70.0f)
        return false;
    const auto count = std::min<size_t> (3, state.viewBindings.size());
    constexpr float gap = 6.0f;
    if (count == 1)
        paintOne (g, area, state, state.viewBindings[0]);
    else if (area.getWidth() < 620.0f)
    {
        const auto height = (area.getHeight() - gap * static_cast<float> (count - 1))
                          / static_cast<float> (count);
        for (size_t index = 0; index < count; ++index)
        {
            paintOne (g, area.removeFromTop (height), state, state.viewBindings[index]);
            area.removeFromTop (gap);
        }
    }
    else if (count == 2 && state.presentationLayout == "main")
    {
        auto primary = area.removeFromLeft (area.getWidth() * 0.62f);
        area.removeFromLeft (gap);
        paintOne (g, primary, state, state.viewBindings[0]);
        paintOne (g, area, state, state.viewBindings[1]);
    }
    else if (count == 2)
    {
        const auto width = (area.getWidth() - gap) * 0.5f;
        paintOne (g, area.removeFromLeft (width), state, state.viewBindings[0]);
        area.removeFromLeft (gap);
        paintOne (g, area, state, state.viewBindings[1]);
    }
    else if (state.presentationLayout == "equal")
    {
        const auto width = (area.getWidth() - gap * 2.0f) / 3.0f;
        for (size_t index = 0; index < count; ++index)
        {
            paintOne (g, area.removeFromLeft (width), state, state.viewBindings[index]);
            area.removeFromLeft (gap);
        }
    }
    else
    {
        auto primary = area.removeFromLeft (area.getWidth() * 0.62f);
        area.removeFromLeft (gap);
        paintOne (g, primary, state, state.viewBindings[0]);
        const auto height = (area.getHeight() - gap) * 0.5f;
        paintOne (g, area.removeFromTop (height), state, state.viewBindings[1]);
        area.removeFromTop (gap);
        paintOne (g, area, state, state.viewBindings[2]);
    }
    return true;
}
}
