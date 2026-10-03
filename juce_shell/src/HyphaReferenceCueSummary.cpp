#include "HyphaReferenceCueSummary.h"

#include "HyphaReferenceComponent.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <cmath>

namespace hypha::reference_ui
{
namespace
{
// HyphaReferenceVisuals と同じ縦軸（スペクトルの dBFS）。
constexpr double minimumDb = -120.0, maximumDb = 6.0;
constexpr int minimumAFrames = 30;  // A の窓が 3 秒に満たないあいだは比べない（短い窓は揺れる）

float dbY (double db, juce::Rectangle<float> area)
{
    return area.getBottom() - static_cast<float> ((juce::jlimit (minimumDb, maximumDb, db) - minimumDb) / (maximumDb - minimumDb))
        * area.getHeight();
}

float logX (double hz, double minimumHz, double maximumHz, juce::Rectangle<float> area)
{
    const auto normalized = std::log (juce::jlimit (minimumHz, maximumHz, hz) / minimumHz) / std::log (maximumHz / minimumHz);
    return area.getX() + static_cast<float> (normalized) * area.getWidth();
}

juce::String signedDb (double value)
{
    return (value >= 0.0 ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + juce::String (std::abs (value), 1);
}

juce::String clock (double seconds)
{
    const auto whole = static_cast<int> (std::floor (std::max (0.0, seconds)));
    return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
}
}

double comparisonGainDb (const State& state) noexcept
{
    if (state.comparisonMode == "original") return 0.0;
    if (state.bSelected && state.audibleComparisonSlot == state.comparisonSlot) return state.appliedGainDb;
    return state.comparisonMode == "loudness_match" ? state.aWindowLoudness - state.cueLoudness
                                                    : std::numeric_limits<double>::quiet_NaN();
}

bool kirinComparable (const State& state) noexcept
{
    if (! state.aKirin || ! state.cueKirin || state.aKirin->centersHz.size() != state.cueKirin->centersHz.size()
        || state.cueKirin->medianDb.size() != state.cueKirin->centersHz.size())
        return false;
    for (size_t band = 0; band < state.aKirin->centersHz.size(); ++band)
        if (std::abs (state.aKirin->centersHz[band] / state.cueKirin->centersHz[band] - 1.0) > 1.0e-3) return false;
    return true;
}

bool paintCueSpectrum (juce::Graphics& g, juce::Rectangle<float> area, const State& state, double minimumHz, double maximumHz)
{
    // C の Cue の値があるときだけ。A の窓がまだ足りなければ C だけを描き、A は「集めています」と出す。
    if (! state.separateComparisons || state.comparisonSlot != 2 || ! state.cueKirin) return false;
    const auto& cue = *state.cueKirin;
    const auto gain = comparisonGainDb (state);
    const bool matched = std::isfinite (gain);
    const auto shift = matched ? gain : 0.0;
    juce::Path band, median;
    std::vector<juce::Point<float>> lower;
    bool started = false;
    for (size_t index = 0; index < cue.centersHz.size(); ++index)
    {
        const auto hz = cue.centersHz[index];
        if (hz < minimumHz || hz > maximumHz) continue;
        const auto x = logX (hz, minimumHz, maximumHz, area);
        const juce::Point<float> top { x, dbY (cue.p90Db[index] + shift, area) };
        const juce::Point<float> mid { x, dbY (cue.medianDb[index] + shift, area) };
        lower.push_back ({ x, dbY (cue.p10Db[index] + shift, area) });
        if (! started) { band.startNewSubPath (top); median.startNewSubPath (mid); started = true; }
        else { band.lineTo (top); median.lineTo (mid); }
    }
    if (! started) return false;
    for (auto it = lower.rbegin(); it != lower.rend(); ++it) band.lineTo (*it);
    band.closeSubPath();
    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.10f));
    g.fillPath (band);
    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.95f));
    g.strokePath (median, juce::PathStrokeType (1.6f));
    if (kirinComparable (state) && state.aKirin->frames >= minimumAFrames)
    {
        juce::Path live;
        started = false;
        for (size_t index = 0; index < state.aKirin->centersHz.size(); ++index)
        {
            const auto hz = state.aKirin->centersHz[index];
            if (hz < minimumHz || hz > maximumHz) continue;
            const juce::Point<float> point { logX (hz, minimumHz, maximumHz, area), dbY (state.aKirin->medianDb[index], area) };
            if (! started) { live.startNewSubPath (point); started = true; } else live.lineTo (point);
        }
        g.setColour (COL_FLORA_BR.withAlpha (0.95f));
        g.strokePath (live, juce::PathStrokeType (1.8f));
    }
    return true;
}

juce::String cueSpectrumLegend (const State& state)
{
    const bool aReady = kirinComparable (state) && state.aKirin->frames >= minimumAFrames;
    const auto a = aReady ? "A LAST " + juce::String (juce::roundToInt (state.aKirin->frames / 10.0)) + " S" : juce::String ("A WAITING");
    return a + " / C CUE" + (std::isfinite (comparisonGainDb (state)) ? "" : " / LEVEL NOT MATCHED");
}

juce::String matchReadout (const State& state)
{
    if (state.comparisonMode == "original") return "ORIGINAL LEVEL";
    const auto gain = comparisonGainDb (state);
    if (! std::isfinite (gain)) return {};
    const auto value = "C " + signedDb (gain) + " dB";
    return state.bSelected && state.audibleComparisonSlot == 2 ? "MATCHED / " + value + " / FIXED" : "ON PLAY / " + value;
}

void paintBandSummary (juce::Graphics& g, juce::Rectangle<int> area, const State& state, presentation::Context context)
{
    static constexpr const char* names[] { "LOW 20-250", "LOW-MID 250-2k", "MID 2k-8k", "HIGH 8k-20k" };
    const auto gain = comparisonGainDb (state);
    const bool ready = kirinComparable (state) && state.aKirin->frames >= minimumAFrames && std::isfinite (gain);
    constexpr int gap = 6;
    const auto width = (area.getWidth() - gap * 3) / 4;
    for (size_t band = 0; band < 4; ++band)
    {
        auto cell = area.removeFromLeft (width);
        area.removeFromLeft (gap);
        surface_material::paintPanel (g, cell.toFloat(), 0.6f);
        auto inner = cell.reduced (8, 3);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawEllipsized (g, names[band], inner.removeFromTop (inner.getHeight() / 2), juce::Justification::centredLeft);
        const auto c = ready ? state.cueKirin->balanceDb[band] + gain : std::numeric_limits<double>::quiet_NaN();
        const auto a = ready ? state.aKirin->balanceDb[band] : std::numeric_limits<double>::quiet_NaN();
        const bool shown = std::isfinite (c) && std::isfinite (a) && a > -200.0;
        g.setColour (shown ? COL_OBSERVATORY_VALUE : COL_MUTED);
        g.setFont (monoFont (context, typography::TextRole::readout, typography::Composition::information));
        text_style::drawEllipsized (g, shown ? signedDb (c - a) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                                    inner.removeFromLeft (inner.getWidth() * 3 / 5), juce::Justification::centredLeft);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawEllipsized (g, "dB C-A", inner, juce::Justification::centredRight);
    }
}

void paintCueBar (juce::Graphics& g, juce::Rectangle<int> area, const State& state, presentation::Context context)
{
    surface_material::paintPanel (g, area.toFloat(), 0.6f);
    const auto duration = state.sourceDurationSeconds;
    if (! std::isfinite (duration) || duration <= 0.0 || ! std::isfinite (state.cueStartSeconds) || ! std::isfinite (state.cueEndSeconds))
        return;
    auto track = area.reduced (8, 5).toFloat();
    const auto x = [&track, duration] (double seconds)
    { return track.getX() + static_cast<float> (juce::jlimit (0.0, 1.0, seconds / duration)) * track.getWidth(); };
    g.setColour (COL_MUTED.withAlpha (0.16f));
    for (double tick = 10.0; tick < duration; tick += 10.0)  // 10 秒ごとの目盛り
        g.drawVerticalLine (juce::roundToInt (x (tick)), track.getY() + track.getHeight() * 0.55f, track.getBottom());
    const auto cue = juce::Rectangle<float>::leftTopRightBottom (x (state.cueStartSeconds), track.getY(),
                                                                 std::max (x (state.cueEndSeconds), x (state.cueStartSeconds) + 2.0f), track.getBottom());
    g.setColour (COL_FLORA.withAlpha (0.16f));
    g.fillRoundedRectangle (cue, 2.0f);
    g.setColour (COL_FLORA_BR.withAlpha (0.8f));
    g.drawRoundedRectangle (cue, 2.0f, 1.0f);
    if (std::isfinite (state.cuePlayheadSeconds))
    {
        g.setColour (COL_SPECTRUM_DELTA_BR);
        g.fillRect (juce::Rectangle<float> (x (state.cuePlayheadSeconds) - 0.75f, track.getY() - 2.0f, 1.5f, track.getHeight() + 4.0f));
    }
    g.setFont (monoFont (context, typography::TextRole::unit, typography::Composition::information));
    g.setColour (COL_TEXT_TERTIARY);
    auto labels = area.reduced (12, 0);
    text_style::drawEllipsized (g, "0:00", labels.removeFromLeft (40), juce::Justification::centredLeft);
    g.setColour (COL_FLORA_BR.withAlpha (0.9f));
    text_style::drawEllipsized (g, "CUE " + clock (state.cueStartSeconds) + "-" + clock (state.cueEndSeconds)
                                    + (state.cueLoops ? " / LOOP" : "") + " / " + clock (duration),
                                labels, juce::Justification::centredRight);
}
}
