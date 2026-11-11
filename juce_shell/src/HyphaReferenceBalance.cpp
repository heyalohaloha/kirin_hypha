#include "HyphaReferenceBalance.h"

#include "HyphaReferenceAWindowControl.h"
#include "HyphaReferenceChartReading.h"
#include "HyphaReferenceComponent.h"
#include "HyphaReferenceOverlay.h"
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
#include <limits>

// B（REF）の画面の右の Balance。ONE は A（金）に、選んでいる B の曲（水色）と、REFERENCE SET の曲全体の分布
// （p10〜p90 の薄い帯）を重ねる。ALL は A に、一覧の番号で出した曲と目標の線を、出した時に決めた色で重ねる
// （HyphaReferenceOverlay.h）。曲の値は Kirin OS が残した既定の Cue の 64 帯域のスペクトル（ranges の中央値）、
// 目標は Hypha が同じ定義で測った値。既製のジャンル曲線は出さない。A は同じ定義で測った、選んだ窓の中央値
// （ReferenceKirinSpectrum。FREQ の画面のスペクトルとは定義が違うので使わない）。曲と目標は B が鳴る音量に
// そろえる（A の窓の音量 − その曲の Cue の Integrated）。
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
    std::vector<OverlayItem> shown;  // ALL：出している線（一覧の順）
    bool linesOn = false;            // ALL：番号で出した線がある（値がまだ無い線を含む）
    bool rangeShown (const State& state) const noexcept { return ! state.overlay.all && comparable.size() >= 2; }  // 帯は 2 曲から
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
    if (state.overlay.all)
        for (auto& item : overlayItems (state))
        {
            sources.linesOn = sources.linesOn || item.line != nullptr;
            if (item.line != nullptr && item.drawable()) sources.shown.push_back (std::move (item));
        }
    return sources;
}

// 凡例の 1 項目：文字・見本の色と太さ・右の余白（A の窓の矢印）。
struct LegendEntry
{
    juce::String text;
    juce::Colour colour;
    float thickness = 1.6f;
    int extra = 0;
};

std::vector<LegendEntry> legendEntries (const BalanceSources& sources, const State& state)
{
    std::vector<LegendEntry> entries { { sources.aShown ? aWindowLegend (state.aKirin->frames) : juce::String ("A WAITING"),
                                         COL_FLORA_BR, 2.8f, AWindowControl::arrowWidth } };
    if (state.overlay.all)
    {
        for (const auto& item : sources.shown)
            entries.push_back ({ juce::String (item.number) + " " + item.name, overlayLineColour (item.line->colour) });
        return entries;
    }
    const auto* chosen = sources.chosen;
    entries.push_back ({ chosen != nullptr ? cuePartLegend ("B", chosen->part, chosen->partStartSeconds, chosen->partEndSeconds, true)
                                           : juce::String ("B"), COL_SPECTRUM_DELTA, 1.6f });
    if (sources.rangeShown (state)) entries.push_back ({ targetRangeLegend, COL_NORMAL.withAlpha (0.28f), 8.0f });  // 帯が無いときは言わない
    return entries;
}

constexpr int legendRowHeight = 18, legendItemExtra = 22;  // 見本 14 と文字のあいだ 4、余白 4
constexpr int overlayLegendRows = 3;  // ALL の凡例は 3 段まで（残りは「+N」。色と名前は左の一覧の番号にもある）

struct LegendCell
{
    juce::String text;
    juce::Colour colour;
    float thickness = 1.6f;
    juce::Rectangle<int> area;
};

// 文字の長さに合わせて左から並べ、入らなければ次の段へ（均等に割ると B が切れる。2026-10-04）。ALL は 1 項目を
// 段の 3 分の 1 までにし、3 段に入らない分を最後の「+N」にまとめる。
std::vector<LegendCell> legendCells (const std::vector<LegendEntry>& entries, const juce::Font& font, juce::Rectangle<int> area, bool overlay)
{
    std::vector<LegendCell> cells;
    const auto cap = overlay ? juce::jmax (80, (area.getWidth() - 20) / 3) : std::numeric_limits<int>::max();
    int x = area.getX(), y = area.getY(), row = 0;
    const auto widthOf = [&] (const LegendEntry& entry, size_t index)
    { return juce::jmin (index == 0 ? std::numeric_limits<int>::max() : cap, legendItemExtra + referenceTextWidth (font, entry.text) + entry.extra); };
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const auto width = widthOf (entries[index], index);
        if (x > area.getX() && x + width > area.getRight()) { x = area.getX(); y += legendRowHeight; ++row; }
        if (overlay && row == overlayLegendRows)
        {
            // 最後の段の後ろに「+N」が入るまで項目を戻す。
            const auto more = [&] { return "+" + juce::String (static_cast<int> (entries.size() - index)); };
            const auto moreWidth = [&] { return referenceTextWidth (font, more()) + 8; };
            while (cells.size() > 1 && cells.back().area.getRight() + 10 + moreWidth() > area.getRight()) { cells.pop_back(); --index; }
            const auto& last = cells.back().area;
            cells.push_back ({ more(), juce::Colours::transparentBlack, 0.0f, { last.getRight() + 10, last.getY(), moreWidth(), legendRowHeight } });
            return cells;
        }
        cells.push_back ({ entries[index].text, entries[index].colour, entries[index].thickness,
                           { x, y, juce::jmin (width, area.getRight() - x), legendRowHeight } });
        x += width + 10;
    }
    return cells;
}

int legendRows (const std::vector<LegendCell>& cells)
{
    int bottom = 0, top = cells.empty() ? 0 : cells.front().area.getY();
    for (const auto& cell : cells) bottom = juce::jmax (bottom, cell.area.getBottom());
    return juce::jmax (1, (bottom - top) / legendRowHeight);
}

// 本体の領域から、下の凡例の領域を取る。
juce::Rectangle<int> takeLegend (juce::Rectangle<float>& body, const std::vector<LegendEntry>& entries, const juce::Font& font, bool overlay)
{
    const auto width = juce::roundToInt (body.getWidth()) - 18;
    const auto rows = legendRows (legendCells (entries, font, { 0, 0, width, legendRowHeight }, overlay));
    return body.removeFromBottom (static_cast<float> (legendRowHeight * rows)).reduced (9.0f, 0.0f).toNearestInt();
}

// ALL：A と、出している線。指している線（一覧の行、無ければ図の中の一番近い線）を太く、ほかを薄くする。
void paintOverlay (juce::Graphics& g, juce::Rectangle<float> chart, const State& state, const BalanceSources& sources,
                   presentation::Context context, bool& unmatched)
{
    std::vector<ReadCurve> curves;
    for (const auto& item : sources.shown)
    {
        unmatched = unmatched || ! std::isfinite (item.gainDb);
        curves.push_back (drawnCurve ("", overlayLineColour (item.line->colour), *item.centers, *item.medians,
                                      std::isfinite (item.gainDb) ? item.gainDb : 0.0, minimumHz, maximumHz));
    }
    int pointed = -1;
    for (size_t index = 0; index < sources.shown.size(); ++index)
        if (sources.shown[index].id == state.overlayPointed) pointed = static_cast<int> (index);
    if (pointed < 0 && chart.contains (state.chartPointer))
    {
        const float hz = static_cast<float> (minimumHz * std::pow (maximumHz / minimumHz, (state.chartPointer.x - chart.getX()) / chart.getWidth()));
        float nearest = 14.0f;  // 線から 14 px より遠ければ、どれも指していない
        for (size_t index = 0; index < curves.size(); ++index)
            if (const auto reading = readAt (curves[index], hz, [chart] (float level) { return yFor (level, chart); }))
                if (const auto distance = std::abs (reading->y - state.chartPointer.y); distance < nearest)
                { nearest = distance; pointed = static_cast<int> (index); }
    }
    if (sources.aShown)
    {
        g.setColour (COL_FLORA_BR.withAlpha (pointed >= 0 ? 0.62f : 0.9f));
        g.strokePath (curve (state.aKirin->centersHz, state.aKirin->medianDb, chart), juce::PathStrokeType (2.8f));
    }
    const auto stroke = [&] (size_t index, float alpha, float thickness)
    {
        juce::Path path;
        for (size_t band = 0; band < curves[index].hz.size(); ++band)
        {
            const juce::Point<float> point { xFor (curves[index].hz[band], chart), yFor (curves[index].level[band], chart) };
            if (band == 0) path.startNewSubPath (point); else path.lineTo (point);
        }
        g.setColour (curves[index].colour.withAlpha (alpha));
        g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    for (size_t index = 0; index < curves.size(); ++index)
        if (static_cast<int> (index) != pointed) stroke (index, pointed >= 0 ? 0.30f : 0.92f, 1.4f);
    if (pointed >= 0) stroke (static_cast<size_t> (pointed), 1.0f, 2.4f);
    // 指した所の値：A と、指している線だけ（全部の値を並べると図を覆う）。
    std::vector<ReadCurve> readings;
    if (sources.aShown)
        readings.push_back (drawnCurve ("A", COL_FLORA_BR, state.aKirin->centersHz, state.aKirin->medianDb, 0.0, minimumHz, maximumHz));
    const auto pointedName = pointed >= 0 ? juce::String (sources.shown[static_cast<size_t> (pointed)].number) : juce::String();
    if (pointed >= 0)
    {
        readings.push_back (curves[static_cast<size_t> (pointed)]);
        readings.back().name = pointedName.toRawUTF8();
        readings.back().colour = overlayTextColour (sources.shown[static_cast<size_t> (pointed)].line->colour);
    }
    if (! readings.empty())
        paintChartReading (g, chart, minimumHz, maximumHz, state.chartPointer, readings,
                           [chart] (float level) { return yFor (level, chart); }, context);
    if (sources.shown.empty())
    {
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (labelFont (context, typography::TextRole::status, typography::Composition::visualization));
        text_style::drawEllipsized (g, sources.linesOn ? "REFERENCE VALUES ARRIVE FROM KIRIN OS" : overlayEmptyText,
                                    chart.toNearestInt(), juce::Justification::centred);
    }
}
}

juce::Rectangle<int> referenceBalanceAWindowArea (juce::Rectangle<float> bounds, const State& state, presentation::Context context)
{
    const auto entries = legendEntries (balanceSources (state), state);
    const auto font = labelFont (context, typography::TextRole::legend, typography::Composition::visualization);
    bounds.removeFromTop (26.0f);
    const auto legend = takeLegend (bounds, entries, font, state.overlay.all);
    return legend.withHeight (legendRowHeight).withWidth (juce::jmin (legend.getWidth(),
        legendItemExtra + referenceTextWidth (font, entries[0].text) + AWindowControl::arrowWidth));
}

void paintReferenceBalance (juce::Graphics& g, juce::Rectangle<float> bounds, const State& state,
                            presentation::Context context)
{
    surface_material::paintPanel (g, bounds, 0.72f);
    help::note (bounds, state.overlay.all ? help_text::balanceAll : help_text::balance);
    auto header = bounds.removeFromTop (26.0f).reduced (9.0f, 2.0f).toNearestInt();
    g.setColour (COL_NORMAL.withAlpha (0.92f));
    g.setFont (labelFont (context, typography::TextRole::metricLabel, typography::Composition::visualization));
    text_style::drawEllipsized (g, "BALANCE", header.removeFromLeft (header.getWidth() / 3), juce::Justification::centredLeft);
    header.removeFromRight (OverlaySwitch::width + 10);  // ONE／ALL（REF が置く部品）
    const auto sources = balanceSources (state);
    const auto* chosen = sources.chosen;
    const auto& centers = sources.centers;
    const auto& comparable = sources.comparable;
    const bool aShown = sources.aShown;
    const auto entries = legendEntries (sources, state);
    const auto legendFont = labelFont (context, typography::TextRole::legend, typography::Composition::visualization);
    auto legend = takeLegend (bounds, entries, legendFont, state.overlay.all);
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
    if (state.overlay.all) paintOverlay (g, chart, state, sources, context, unmatched);
    std::vector<float> rangeLow, rangeHigh;
    if (sources.rangeShown (state))
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
    if (aShown && ! state.overlay.all)
    {
        g.setColour (COL_FLORA_BR.withAlpha (0.9f));
        g.strokePath (curve (state.aKirin->centersHz, state.aKirin->medianDb, chart), juce::PathStrokeType (2.8f));
        readings.push_back (drawnCurve ("A", COL_FLORA_BR, state.aKirin->centersHz, state.aKirin->medianDb, 0.0, minimumHz, maximumHz));
    }
    if (chosen != nullptr && ! state.overlay.all)
    {
        std::vector<float> shifted;
        for (const auto value : chosen->medianDb) shifted.push_back (value + static_cast<float> (shift (*chosen, true)));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.95f));
        g.strokePath (curve (chosen->centersHz, shifted, chart), juce::PathStrokeType (1.4f));
        readings.push_back (drawnCurve ("B", COL_SPECTRUM_DELTA, chosen->centersHz, shifted, 0.0, minimumHz, maximumHz));
    }
    // 指した所の値（C の図と同じ読み）。図を描いた値をそのまま読む。
    if (! readings.empty() || sources.rangeShown (state))
    {
        const ReadRange range { drawnCurve ("", COL_NORMAL, centers, rangeLow, 0.0, minimumHz, maximumHz),
                                drawnCurve ("", COL_NORMAL, centers, rangeHigh, 0.0, minimumHz, maximumHz), blauertReadoutHeight };
        paintChartReading (g, chart, minimumHz, maximumHz, state.chartPointer, readings,
                           [chart] (float level) { return yFor (level, chart); }, context, &range);
    }
    if (chosen != nullptr && aShown && ! state.overlay.all)  // 曲の中の帯どうしの差なので、音量をそろえられない曲でも出せる
        paintBlauertReadout (g, chart, 'B', blauertVersus (state.aKirin->centersHz, state.aKirin->medianDb,
                                                          chosen->centersHz, chosen->medianDb),
                             minimumHz, maximumHz, context);
    g.setColour (unmatched ? COL_FLORA : COL_TEXT_TERTIARY);
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    text_style::drawEllipsized (g, unmatched ? "LEVEL NOT MATCHED" : "20 Hz - 20 kHz", header, juce::Justification::centredRight);
    if (chosen == nullptr && comparable.empty() && ! state.overlay.all)
    {
        g.setColour (COL_TEXT_SECONDARY.withAlpha (0.92f));
        g.setFont (labelFont (context, typography::TextRole::status, typography::Composition::visualization));
        text_style::drawEllipsized (g, "REFERENCE VALUES ARRIVE FROM KIRIN OS", chart.toNearestInt(), juce::Justification::centred);
    }
    // 凡例：A の窓（金、右に窓を選ぶ矢印）・B の鳴らす部分（水色、「Bサビ 1:02-1:24」）・目標の範囲（薄い帯）。
    // ALL は A と、出している線の番号と名前（出した時の色）。
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    for (const auto& cell : legendCells (entries, legendFont, legend, state.overlay.all))
    {
        auto area = cell.area;
        if (cell.thickness > 0.0f)
        {
            g.setColour (cell.colour);
            g.fillRect (area.removeFromLeft (14).toFloat().withSizeKeepingCentre (14.0f, cell.thickness));
            area.removeFromLeft (4);
        }
        g.setColour (COL_TEXT_SECONDARY);
        text_style::drawEllipsized (g, cell.text, area, juce::Justification::centredLeft);
    }
}
}
