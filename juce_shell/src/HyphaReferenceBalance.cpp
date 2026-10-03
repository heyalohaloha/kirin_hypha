#include "HyphaReferenceBalance.h"

#include "HyphaReferenceComponent.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <algorithm>
#include <cmath>

// H11: B（REF）の画面の右の Balance（「Tonal Balance」とは呼ばない、方向設計 §4）。A（金）に、選んでいる
// B の曲（水色）と、B SET の曲全体の分布（p10〜p90 の薄い帯）を重ねる。曲の値は Kirin OS が残した
// 既定の Cue の 64 帯域のスペクトル（ranges の中央値）。既製のジャンル曲線は出さない。
// H12: A は同じ定義で測った直近 10 秒の中央値（ReferenceKirinSpectrum。FREQ の画面のスペクトルとは
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
}

void paintReferenceBalance (juce::Graphics& g, juce::Rectangle<float> bounds, const State& state,
                            presentation::Context context)
{
    surface_material::paintPanel (g, bounds, 0.72f);
    auto header = bounds.removeFromTop (26.0f).reduced (9.0f, 2.0f).toNearestInt();
    g.setColour (COL_NORMAL.withAlpha (0.92f));
    g.setFont (labelFont (context, typography::TextRole::metricLabel, typography::Composition::visualization));
    text_style::drawEllipsized (g, "BALANCE", header.removeFromLeft (header.getWidth() / 3), juce::Justification::centredLeft);
    auto legend = bounds.removeFromBottom (18.0f).reduced (9.0f, 0.0f).toNearestInt();
    auto chart = bounds.reduced (10.0f, 6.0f);
    g.setColour (COL_MUTED.withAlpha (0.10f));
    for (const auto hz : { 100.0, 1'000.0, 10'000.0 })
        g.drawVerticalLine (juce::roundToInt (xFor (hz, chart)), chart.getY(), chart.getBottom());

    // B SET の分布：同じ帯域の並びを持つ曲の中央値（B が鳴る音量にそろえる）から、帯域ごとの p10〜p90。
    // 鳴っている B は実際に掛けている gain で、ほかは A の直近の窓との差でそろえる。そろえられない（A の窓・
    // 曲の LUFS-I が無い）曲はそのままの高さで描き、見出しの右に「音量はそろっていない」と出す。
    const bool bPlaying = state.bSelected && state.audibleComparisonSlot == 3;
    bool unmatched = false;
    const auto shift = [&state, &unmatched, bPlaying] (const SongFact& fact, bool chosenSong)
    {
        if (chosenSong && bPlaying && std::isfinite (state.appliedGainDb)) return state.appliedGainDb;
        if (std::isfinite (state.aWindowLoudness) && std::isfinite (fact.lufsI)) return state.aWindowLoudness - fact.lufsI;
        unmatched = true;
        return 0.0;
    };
    const SongFact* chosen = nullptr;
    std::vector<const SongFact*> set;
    for (size_t index = 0; index < state.songFacts.size() && index < state.songs.size(); ++index)
    {
        const auto& fact = state.songFacts[index];
        if (fact.medianDb.empty()) continue;
        if (state.songs[index].id == state.songId) chosen = &fact;
        set.push_back (&fact);
    }
    const auto& centers = chosen != nullptr ? chosen->centersHz : set.empty() ? std::vector<double> {} : set.front()->centersHz;
    std::vector<const SongFact*> comparable;
    for (const auto* fact : set) if (fact->centersHz == centers) comparable.push_back (fact);
    if (comparable.size() >= 2)
    {
        juce::Path band;
        std::vector<juce::Point<float>> lower;
        for (size_t bin = 0; bin < centers.size(); ++bin)
        {
            std::vector<double> values;
            for (const auto* fact : comparable) values.push_back (fact->medianDb[bin] + shift (*fact, fact == chosen));
            const juce::Point<float> top { xFor (centers[bin], chart), yFor (percentile (values, 0.9), chart) };
            lower.push_back ({ top.x, yFor (percentile (values, 0.1), chart) });
            if (bin == 0) band.startNewSubPath (top); else band.lineTo (top);
        }
        for (auto it = lower.rbegin(); it != lower.rend(); ++it) band.lineTo (*it);
        band.closeSubPath();
        g.setColour (COL_NORMAL.withAlpha (0.09f));
        g.fillPath (band);
    }
    if (chosen != nullptr)
    {
        std::vector<float> shifted;
        for (const auto value : chosen->medianDb) shifted.push_back (value + static_cast<float> (shift (*chosen, true)));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.95f));
        g.strokePath (curve (chosen->centersHz, shifted, chart), juce::PathStrokeType (1.6f));
    }
    // A（直近 10 秒、Kirin OS の Cue と同じ定義）。曲と同じ帯域の並びのときだけ重ねる。
    const bool aShown = state.aKirin && state.aKirin->frames >= 30 && ! centers.empty()
        && state.aKirin->centersHz.size() == centers.size()
        && std::abs (state.aKirin->centersHz.back() / centers.back() - 1.0) < 1.0e-3;
    if (aShown)
    {
        g.setColour (COL_FLORA_BR.withAlpha (0.95f));
        g.strokePath (curve (state.aKirin->centersHz, state.aKirin->medianDb, chart), juce::PathStrokeType (1.8f));
    }
    g.setColour (unmatched ? COL_FLORA : COL_TEXT_TERTIARY);
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    text_style::drawEllipsized (g, unmatched ? "LEVEL NOT MATCHED" : "20 Hz - 20 kHz", header, juce::Justification::centredRight);
    if (chosen == nullptr && comparable.empty())
    {
        g.setColour (COL_TEXT_SECONDARY.withAlpha (0.92f));
        g.setFont (labelFont (context, typography::TextRole::status, typography::Composition::visualization));
        text_style::drawEllipsized (g, "B SET VALUES ARRIVE FROM KIRIN OS", chart.toNearestInt(), juce::Justification::centred);
    }
    // 凡例：A LIVE（金）・B（水色）・B SET（p10〜p90 の薄い帯）。
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    const auto item = [&] (const juce::String& text, juce::Colour colour) {
        auto cell = legend.removeFromLeft (legend.getWidth() / 3);
        g.setColour (colour);
        g.fillRect (cell.removeFromLeft (14).withSizeKeepingCentre (14, 2).toFloat());
        g.setColour (COL_TEXT_SECONDARY);
        text_style::drawEllipsized (g, text, cell.withTrimmedLeft (4), juce::Justification::centredLeft);
    };
    item (aShown ? "A 10 S" : "A WAITING", COL_FLORA_BR);
    item ("B", COL_SPECTRUM_DELTA);
    item ("B SET", COL_NORMAL.withAlpha (0.35f));
}
}
