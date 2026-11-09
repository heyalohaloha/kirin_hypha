#include "HyphaReferenceBalance.h"

#include "HyphaReferenceAWindowControl.h"
#include "HyphaReferenceChartReading.h"
#include "HyphaReferenceComponent.h"
#include "HyphaReferencePreviewGain.h"
#include "HyphaReferenceLegend.h"
#include "HyphaReferenceCueSummary.h"
#include "HyphaReferenceBlauertZones.h"
#include "HyphaReferenceFrequencyTicks.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"
#include "HyphaReferenceHelp.h"
#include "HyphaReferenceHelpText.h"

#include <algorithm>
#include <cmath>

// B（REF）の画面の右の Balance。A（金）に、選んでいる
// B の曲（水色）と、REFERENCE SET の曲全体の分布（p10〜p90 の薄い帯）を重ねる。曲の値は Kirin OS が残した
// 既定の Cue の 64 帯域のスペクトル（ranges の中央値）。既製のジャンル曲線は出さない。
// A は同じ定義で測った、選んだ窓の中央値（ReferenceKirinSpectrum。FREQ の画面のスペクトルとは
// 定義が違うので使わない）。曲は B が鳴る音量にそろえる（A の 10 秒の音量 − その曲の Cue の Integrated）。
namespace hypha::reference_ui
{
namespace
{
constexpr double minimumHz = 20.0, maximumHz = 20'000.0, minimumDb = -90.0, maximumDb = -6.0;

float xFor (double hz, juce::Rectangle<float> area)
{
    const auto normalized = std::log (juce::jlimit (minimumHz, maximumHz, hz) / minimumHz) / std::log (maximumHz / minimumHz);
    return area.getX() + static_cast<float> (normalized) * area.getWidth();
}

float yFor (double db, juce::Rectangle<float> area)
{
    return area.getBottom() - static_cast<float> ((juce::jlimit (minimumDb, maximumDb, db) - minimumDb)
                                                  / (maximumDb - minimumDb)) * area.getHeight();
}

double percentile (std::vector<double> values, double fraction)
{
    std::sort (values.begin(), values.end());
    const auto position = fraction * static_cast<double> (values.size() - 1);
    const auto lower = static_cast<size_t> (std::floor (position));
    const auto upper = std::min (values.size() - 1, lower + 1);
    return values[lower] + (values[upper] - values[lower]) * (position - static_cast<double> (lower));
}

juce::Path curve (const std::vector<double>& centers, const std::vector<float>& values, juce::Rectangle<float> area)
{
    juce::Path path;
    for (size_t index = 0; index < centers.size() && index < values.size(); ++index)
    {
        const juce::Point<float> point { xFor (centers[index], area), yFor (values[index], area) };
        if (index == 0) path.startNewSubPath (point); else path.lineTo (point);  // isEmpty() は線が無いと true
    }
    return path;
}

// 図に載る物。凡例も、A の窓のボタンの場所も、ここから決める。
struct BalanceSources
{
    const SongFact* chosen = nullptr;
    std::vector<const SongFact*> comparable;  // 同じ帯域の並びの曲
    std::vector<double> centers;
    bool aShown = false;
    bool rangeShown() const noexcept { return comparable.size() >= 2; }  // 帯は 2 曲から
};

BalanceSources balanceSources (const State& state)
{
    BalanceSources sources;
    std::vector<const SongFact*> set;
    for (size_t index = 0; index < state.songFacts.size() && index < state.songs.size(); ++index)
    {
        const auto& fact = state.songFacts[index];
        if (fact.medianDb.empty()) continue;
        if (state.songs[index].id == state.songId) sources.chosen = &fact;
        set.push_back (&fact);
    }
    if (sources.chosen != nullptr) sources.centers = sources.chosen->centersHz;
    else if (! set.empty()) sources.centers = set.front()->centersHz;
    for (const auto* fact : set) if (fact->centersHz == sources.centers) sources.comparable.push_back (fact);
    sources.aShown = state.aKirin && state.aKirin->frames >= 30 && ! sources.centers.empty()
        && state.aKirin->centersHz.size() == sources.centers.size()
        && std::abs (state.aKirin->centersHz.back() / sources.centers.back() - 1.0) < 1.0e-3;
    return sources;
}

juce::StringArray balanceLabels (const BalanceSources& sources, const State& state)
{
    const auto* chosen = sources.chosen;
    juce::StringArray labels {
        sources.aShown ? aWindowLegend (state.aKirin->frames) : juce::String ("A WAITING"),
        chosen != nullptr ? cuePartLegend ("B", chosen->part, chosen->partStartSeconds, chosen->partEndSeconds, true) : juce::String ("B") };
    if (sources.rangeShown()) labels.add (targetRangeLegend);  // 帯が無いときは言わない
    return labels;
}

constexpr int legendRowHeight = 18, legendItemExtra = 22;  // 見本 14 と文字のあいだ 4、余白 4

// 本体の領域から、下の凡例の領域を取る。
juce::Rectangle<int> takeLegend (juce::Rectangle<float>& body, const juce::StringArray& labels, const juce::Font& font)
{
    const auto rows = referenceLegendRows (font, labels, juce::roundToInt (body.getWidth()) - 18, legendItemExtra, AWindowControl::arrowWidth);
    return body.removeFromBottom (static_cast<float> (legendRowHeight * rows)).reduced (9.0f, 0.0f).toNearestInt();
}
}

juce::Rectangle<int> referenceBalanceAWindowArea (juce::Rectangle<float> bounds, const State& state, presentation::Context context)
{
    const auto labels = balanceLabels (balanceSources (state), state);
    const auto font = labelFont (context, typography::TextRole::legend, typography::Composition::visualization);
    bounds.removeFromTop (26.0f);
    const auto legend = takeLegend (bounds, labels, font);
    return legend.withHeight (legendRowHeight).withWidth (juce::jmin (legend.getWidth(),
        legendItemExtra + referenceTextWidth (font, labels[0]) + AWindowControl::arrowWidth));
}

void paintReferenceBalance (juce::Graphics& g, juce::Rectangle<float> bounds, const State& state,
                            presentation::Context context)
{
    surface_material::paintPanel (g, bounds, 0.72f);
    help::note (bounds, help_text::balance);
    auto header = bounds.removeFromTop (26.0f).reduced (9.0f, 2.0f).toNearestInt();
    g.setColour (COL_NORMAL.withAlpha (0.92f));
    g.setFont (labelFont (context, typography::TextRole::metricLabel, typography::Composition::visualization));
    text_style::drawEllipsized (g, "BALANCE", header.removeFromLeft (header.getWidth() / 3), juce::Justification::centredLeft);
    const auto sources = balanceSources (state);
    const auto* chosen = sources.chosen;
    const auto& centers = sources.centers;
    const auto& comparable = sources.comparable;
    const bool aShown = sources.aShown;
    const auto labels = balanceLabels (sources, state);
    const auto legendFont = labelFont (context, typography::TextRole::legend, typography::Composition::visualization);
    auto legend = takeLegend (bounds, labels, legendFont);
    auto chart = bounds.reduced (10.0f, 6.0f);
    paintFrequencyTicks (g, chart, minimumHz, maximumHz, context);
    paintBlauertZones (g, chart, minimumHz, maximumHz);

    // REFERENCE SET の分布：同じ帯域の並びを持つ曲の中央値（B が鳴る音量にそろえる）から、帯域ごとの p10〜p90。
    // 鳴っている B は実際に掛けている gain で、ほかは A の直近の窓との差でそろえる。そろえられない（A の窓・
    // 曲の LUFS-I が無い）曲はそのままの高さで描き、見出しの右に「音量はそろっていない」と出す。
    const bool bPlaying = state.bSelected && state.audibleComparisonSlot == 3;
    bool unmatched = false;
    const auto shift = [&state, &unmatched, bPlaying] (const SongFact& fact, bool chosenSong)
    {
        if (chosenSong && bPlaying && std::isfinite (state.appliedGainDb)) return state.appliedGainDb;
        if (const auto gain = songPreviewGainDb (state, fact, chosenSong); std::isfinite (gain)) return gain;
        unmatched = true;
        return 0.0;
    };
    std::vector<float> rangeLow, rangeHigh;
    if (sources.rangeShown())
    {
        juce::Path band;
        std::vector<juce::Point<float>> lower;
        for (size_t bin = 0; bin < centers.size(); ++bin)
        {
            std::vector<double> values;
            for (const auto* fact : comparable) values.push_back (fact->medianDb[bin] + shift (*fact, fact == chosen));
            rangeLow.push_back (static_cast<float> (percentile (values, 0.1)));
            rangeHigh.push_back (static_cast<float> (percentile (values, 0.9)));
            const juce::Point<float> top { xFor (centers[bin], chart), yFor (rangeHigh.back(), chart) };
            lower.push_back ({ top.x, yFor (rangeLow.back(), chart) });
            if (bin == 0) band.startNewSubPath (top); else band.lineTo (top);
        }
        for (auto it = lower.rbegin(); it != lower.rend(); ++it) band.lineTo (*it);
        band.closeSubPath();
        g.setColour (COL_NORMAL.withAlpha (0.12f));
        g.fillPath (band);
    }
    // A（選んだ窓、Kirin OS の Cue と同じ定義）。曲と同じ帯域の並びのときだけ重ねる。A を太く下に、B を細く上に
    // 描く（同じ値でも両方見える。2026-10-04）。
    std::vector<ReadCurve> readings;
    if (aShown)
    {
        g.setColour (COL_FLORA_BR.withAlpha (0.9f));
        g.strokePath (curve (state.aKirin->centersHz, state.aKirin->medianDb, chart), juce::PathStrokeType (2.8f));
        readings.push_back (drawnCurve ("A", COL_FLORA_BR, state.aKirin->centersHz, state.aKirin->medianDb, 0.0, minimumHz, maximumHz));
    }
    if (chosen != nullptr)
    {
        std::vector<float> shifted;
        for (const auto value : chosen->medianDb) shifted.push_back (value + static_cast<float> (shift (*chosen, true)));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.95f));
        g.strokePath (curve (chosen->centersHz, shifted, chart), juce::PathStrokeType (1.4f));
        readings.push_back (drawnCurve ("B", COL_SPECTRUM_DELTA, chosen->centersHz, shifted, 0.0, minimumHz, maximumHz));
    }
    // 指した所の値（C の図と同じ読み）。図を描いた値をそのまま読む。
    if (! readings.empty() || sources.rangeShown())
    {
        const ReadRange range { drawnCurve ("", COL_NORMAL, centers, rangeLow, 0.0, minimumHz, maximumHz),
                                drawnCurve ("", COL_NORMAL, centers, rangeHigh, 0.0, minimumHz, maximumHz), blauertReadoutHeight };
        paintChartReading (g, chart, minimumHz, maximumHz, state.chartPointer, readings,
                           [chart] (float level) { return yFor (level, chart); }, context, &range);
    }
    if (chosen != nullptr && aShown)  // 曲の中の帯どうしの差なので、音量をそろえられない曲でも出せる
        paintBlauertReadout (g, chart, 'B', blauertVersus (state.aKirin->centersHz, state.aKirin->medianDb,
                                                          chosen->centersHz, chosen->medianDb),
                             minimumHz, maximumHz, context);
    g.setColour (unmatched ? COL_FLORA : COL_TEXT_TERTIARY);
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    text_style::drawEllipsized (g, unmatched ? "LEVEL NOT MATCHED" : "20 Hz - 20 kHz", header, juce::Justification::centredRight);
    if (chosen == nullptr && comparable.empty())
    {
        g.setColour (COL_TEXT_SECONDARY.withAlpha (0.92f));
        g.setFont (labelFont (context, typography::TextRole::status, typography::Composition::visualization));
        text_style::drawEllipsized (g, "REFERENCE VALUES ARRIVE FROM KIRIN OS", chart.toNearestInt(), juce::Justification::centred);
    }
    // 凡例：A の窓（金、右に窓を選ぶ矢印）・B の鳴らす部分（水色、「Bサビ 1:02-1:24」）・目標の範囲（薄い帯）。
    // 2026-10-04。文字の長さに合わせて左から並べる（均等に割ると B が切れる）。
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    const auto left = legend.getX(), right = legend.getRight();
    int x = left, y = legend.getY();
    const auto item = [&] (const juce::String& text, juce::Colour colour, float thickness, int extra) {
        const auto width = legendItemExtra + referenceTextWidth (legendFont, text) + extra;
        if (x > left && x + width > right) { x = left; y += legendRowHeight; }
        auto cell = juce::Rectangle<int> (x, y, juce::jmin (width, right - x), legendRowHeight);
        x += width + 10;
        g.setColour (colour);
        g.fillRect (cell.removeFromLeft (14).toFloat().withSizeKeepingCentre (14.0f, thickness));
        g.setColour (COL_TEXT_SECONDARY);
        text_style::drawEllipsized (g, text, cell.withTrimmedLeft (4), juce::Justification::centredLeft);
    };
    item (labels[0], COL_FLORA_BR, 2.8f, AWindowControl::arrowWidth);
    item (labels[1], COL_SPECTRUM_DELTA, 1.6f, 0);
    if (sources.rangeShown()) item (labels[2], COL_NORMAL.withAlpha (0.28f), 8.0f, 0);
}
}
