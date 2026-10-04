#include "HyphaReferenceRangeStrips.h"

#include "HyphaReferenceAComparison.h"
#include "HyphaReferenceComponent.h"
#include "HyphaReferenceCueSummary.h"
#include "HyphaReferenceHelp.h"
#include "HyphaReferenceHelpText.h"
#include "HyphaReferenceLegend.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"
#include "reference_audition/ReferenceDynamicsRange.h"

#include <algorithm>
#include <cmath>

namespace hypha::reference_ui
{
namespace
{
using reference_audition::DynamicsFact;
using reference_audition::FactRange;
constexpr int minimumATicks = 30;  // A が 3 秒に満たないあいだは比べない（スペクトルと同じ）
constexpr double nan = std::numeric_limits<double>::quiet_NaN();

enum class Unit { db, lu, lufs, percent, ratio, dbfs, onset };

struct Row
{
    const char* name;
    DynamicsFact fact;
    bool spread;   // 音量の動き：それぞれの中央値のまわりの幅（p90 − p10）で比べる
    bool asHeard;  // C を鳴らす gain で合わせる（軸は値から決める）
    double minimum, maximum, step;
    Unit unit;
    AWords words;      // 差を A を主語に言う言葉（HyphaReferenceAComparison.h）
    const char* help;  // 指したときに下の行に出す説明（HyphaReferenceHelpText.h）
};

std::vector<Row> rowsFor (const juce::String& binding)
{
    namespace text = help_text;
    const Row movement { "LOUDNESS MOVEMENT (LUFS-S)", DynamicsFact::lufsS, true, false, -8.0, 8.0, 4.0, Unit::lu, AWords::size, text::movement };
    if (binding == "dynamics")
        return { { "CREST (TP/RMS)", DynamicsFact::crest, false, false, 0.0, 24.0, 6.0, Unit::db, AWords::size, text::crest }, movement };
    if (binding == "loudness")
        return { { "MOMENTARY (LUFS-M)", DynamicsFact::lufsM, false, true, 0.0, 0.0, 6.0, Unit::lufs, AWords::loudness, text::momentary }, movement };
    if (binding == "stereo")
        return { { "WIDTH (S/M)", DynamicsFact::width, false, false, 0.0, 150.0, 50.0, Unit::percent, AWords::width, text::width },
                 { "CORRELATION", DynamicsFact::correlation, false, false, -1.0, 1.0, 0.5, Unit::ratio, AWords::level, text::correlation } };
    if (binding == "waveform") return { { "PEAK", DynamicsFact::peak, false, true, 0.0, 0.0, 6.0, Unit::dbfs, AWords::level, text::peak },
                                        { "RMS", DynamicsFact::rms, false, true, 0.0, 0.0, 6.0, Unit::dbfs, AWords::level, text::rms } };
    if (binding == "transient")  // 2026-10-04 Daisuke：アタック（ピーク − LUFS-M）を足し、立ち上がりは 2 行目に
        return { { "ATTACK (PEAK - LUFS-M)", DynamicsFact::attack, false, false, 0.0, 24.0, 6.0, Unit::db, AWords::size, text::attack },
                 { "ONSET (RISE PER HOP)", DynamicsFact::onset, false, false, 0.0, 1.0, 0.25, Unit::onset, AWords::size, text::onset } };
    return {};
}

juce::String plain (double value, int decimals)
{
    const auto rounded = std::round (value * std::pow (10.0, decimals)) / std::pow (10.0, decimals);
    return (rounded < 0.0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String {}) + juce::String (std::abs (rounded), decimals);
}

juce::String signedText (double value, int decimals)
{
    const auto rounded = std::round (value * std::pow (10.0, decimals)) / std::pow (10.0, decimals);
    return (rounded < 0.0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String ("+")) + juce::String (std::abs (rounded), decimals);
}

juce::String valueText (Unit unit, double value)
{
    switch (unit)
    {
        case Unit::db: return plain (value, 1) + " dB";
        case Unit::lu: return plain (value, 1) + " LU";
        case Unit::lufs: return plain (value, 1) + " LUFS";
        case Unit::percent: return plain (value, 0) + "%";
        case Unit::ratio: return plain (value, 2);
        case Unit::dbfs: return plain (value, 1) + " dBFS";
        case Unit::onset: return plain (value * 100.0, 0) + "%";
    }
    return {};
}

// A − 比べる側を、A を主語に言葉で（「A 0.12 LOWER」、日本語は「Aが0.12低い」）。
juce::String differenceText (const Row& row, double aMinusOther, char other)
{
    switch (row.unit)
    {
        case Unit::db: case Unit::dbfs: return compareA (aMinusOther, 1, " dB", row.words, other).text;
        case Unit::lu: case Unit::lufs: return compareA (aMinusOther, 1, " LU", row.words, other).text;
        case Unit::percent: return compareA (aMinusOther, 0, " pt", row.words, other).text;
        case Unit::ratio: return compareA (aMinusOther, 2, "", row.words, other).text;
        case Unit::onset: return compareA (aMinusOther * 100.0, 0, " pt", row.words, other).text;
    }
    return {};
}

juce::String tickText (Unit unit, double value)
{
    if (unit == Unit::onset || unit == Unit::percent) return plain (unit == Unit::onset ? value * 100.0 : value, 0) + "%";
    if (unit == Unit::ratio) return value > 0.0 ? signedText (value, 1) : plain (value, 1);
    if (unit == Unit::lu) return value > 0.0 ? signedText (value, 0) : plain (value, 0);
    return plain (value, 0);
}

// 帯（p10〜p90）と中央値の縦線。spread は中央値を 0 にして描く。
struct Bar
{
    FactRange range;
    bool shown = false;
    double shift = 0.0;  // 聞こえる大きさに合わせる量（C の gain）
};

void paintBar (juce::Graphics& g, juce::Rectangle<float> line, const Bar& bar, const Row& row, double minimum, double maximum,
               juce::Colour colour)
{
    const auto x = [&line, minimum, maximum] (double value)
    { return line.getX() + static_cast<float> (juce::jlimit (0.0, 1.0, (value - minimum) / (maximum - minimum))) * line.getWidth(); };
    g.setColour (COL_MUTED.withAlpha (0.16f));
    g.fillRect (juce::Rectangle<float> (line.getX(), line.getCentreY() - 0.5f, line.getWidth(), 1.0f));
    if (! bar.shown) return;
    const auto centre = row.spread ? bar.range.median : 0.0;
    const auto low = x (bar.range.p10 + bar.shift - centre), high = x (bar.range.p90 + bar.shift - centre), middle = x (bar.range.median + bar.shift - centre);
    const auto body = juce::Rectangle<float>::leftTopRightBottom (low, line.getY() + 3.0f, std::max (high, low + 2.0f), line.getBottom() - 3.0f);
    g.setColour (colour.withAlpha (0.30f));
    g.fillRoundedRectangle (body, 2.0f);
    g.setColour (colour.withAlpha (0.75f));
    g.drawRoundedRectangle (body, 2.0f, 1.0f);
    g.setColour (colour);
    g.fillRect (juce::Rectangle<float> (middle - 1.0f, line.getY() + 1.0f, 2.0f, line.getHeight() - 2.0f));
}

// A と比べる側（C・V）の区間ごとの値と、比べる側を聞こえる大きさに合わせる量。
struct Sides
{
    reference_audition::DynamicsHops a, other;
    bool aReady = false;
    double shift = 0.0;  // 比べる側の gain（合わせていなければ 0）
    const char* letter = "C";
};

struct Prepared
{
    Row row;
    Bar aBar, otherBar;
    double minimum = 0.0, maximum = 1.0;
    juce::String aText, otherText, difference;
};

std::vector<Prepared> prepare (const std::vector<Row>& rows, const Sides& sides)
{
    const auto dash = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
    std::vector<Prepared> prepared;
    for (const auto& row : rows)
    {
        Prepared item { row, { reference_audition::rangeOf (sides.a[row.fact]), sides.aReady, 0.0 },
                        { reference_audition::rangeOf (sides.other[row.fact]), true, row.asHeard ? sides.shift : 0.0 } };
        item.aBar.shown = item.aBar.shown && item.aBar.range.valid();
        item.otherBar.shown = item.otherBar.range.valid();
        item.minimum = row.minimum;
        item.maximum = row.maximum;
        if (row.asHeard)
        {
            auto low = std::numeric_limits<double>::infinity(), high = -low;
            for (const auto* bar : { &item.aBar, &item.otherBar })
                if (bar->shown) { low = std::min (low, bar->range.p10 + bar->shift); high = std::max (high, bar->range.p90 + bar->shift); }
            if (! std::isfinite (low)) { low = -24.0; high = 0.0; }
            item.minimum = std::floor ((low - 2.0) / row.step) * row.step;
            item.maximum = std::max (item.minimum + 2.0 * row.step, std::ceil ((high + 2.0) / row.step) * row.step);
        }
        const auto value = [&row] (const Bar& bar) { return row.spread ? bar.range.p90 - bar.range.p10 : bar.range.median + bar.shift; };
        item.aText = item.aBar.shown ? valueText (row.unit, value (item.aBar)) : dash;  // 測れていない側は「—」（凡例が A WAITING と言う）
        item.otherText = item.otherBar.shown ? valueText (row.unit, value (item.otherBar)) : dash;
        if (item.aBar.shown && item.otherBar.shown)
            item.difference = differenceText (row, value (item.aBar) - value (item.otherBar), sides.letter[0]);
        prepared.push_back (std::move (item));
    }
    return prepared;
}

// 段ごとに：項目名・A の帯・比べる側の帯・軸の目盛り。右の列（中央値と差）の幅は文字の長さで決め、どの段もそろえる。
// 帯の幅が足りなければ差を出さない（中央値は残す）。
void paintRows (juce::Graphics& g, juce::Rectangle<float> area, const std::vector<Prepared>& prepared, const Sides& sides,
                presentation::Context context)
{
    if (prepared.empty()) return;
    const auto unitFont = labelFont (context, typography::TextRole::unit, typography::Composition::visualization);
    const auto numberFont = monoFont (context, typography::TextRole::readout, typography::Composition::information);
    float numberWidth = 0.0f, differenceWidth = 0.0f;
    for (const auto& item : prepared)
    {
        numberWidth = std::max ({ numberWidth, text_style::shownWidth (numberFont, item.aText), text_style::shownWidth (numberFont, item.otherText) });
        if (item.difference.isNotEmpty()) differenceWidth = std::max (differenceWidth, text_style::shownWidth (unitFont, item.difference));
    }
    numberWidth = std::ceil (numberWidth) + 6.0f;
    differenceWidth = differenceWidth > 0.0f ? std::ceil (differenceWidth) + 12.0f : 0.0f;
    constexpr float letterWidth = 20.0f, gap = 12.0f;
    if (area.getWidth() - letterWidth - numberWidth - differenceWidth - gap < 140.0f) differenceWidth = 0.0f;
    const auto barLeft = area.getX() + letterWidth, barRight = area.getRight() - numberWidth - differenceWidth - gap;
    if (barRight - barLeft < 60.0f) return;
    const auto rowHeight = std::min (area.getHeight() / static_cast<float> (prepared.size()), 118.0f);
    for (const auto& item : prepared)
    {
        auto block = area.removeFromTop (rowHeight);
        help::note (block, item.row.help);
        auto title = block.removeFromTop (std::min (18.0f, block.getHeight() / 4.0f));
        const auto lineHeight = std::min (24.0f, (block.getHeight() - 20.0f) / 2.0f);
        auto aLine = block.removeFromTop (lineHeight);
        auto otherLine = block.removeFromTop (lineHeight);
        auto axis = block.removeFromTop (std::min (15.0f, block.getHeight()));
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::visualization));
        text_style::drawEllipsized (g, item.row.name, title.withRight (barRight).toNearestInt(), juce::Justification::centredLeft);
        for (auto value = item.minimum; value <= item.maximum + 1.0e-9; value += item.row.step)
        {
            const auto x = barLeft + static_cast<float> ((value - item.minimum) / (item.maximum - item.minimum)) * (barRight - barLeft);
            g.setColour (COL_MUTED.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> (x - 0.5f, aLine.getY(), 1.0f, otherLine.getBottom() - aLine.getY()));
            g.setColour (COL_TEXT_TERTIARY.withAlpha (0.85f));
            text_style::drawText (g, tickText (item.row.unit, value), juce::Rectangle<float> (x - 24.0f, axis.getY(), 48.0f, axis.getHeight()),
                                  juce::Justification::centred, false);
        }
        for (const auto& [line, bar, letter, colour, valueColour, text] :
             { std::tuple { aLine, item.aBar, juce::String ("A"), COL_FLORA_BR, COL_FLORA_BR, item.aText },
               std::tuple { otherLine, item.otherBar, juce::String (sides.letter), COL_SPECTRUM_DELTA, COL_SPECTRUM_DELTA_BR, item.otherText } })
        {
            g.setColour (COL_TEXT_SECONDARY);
            g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::visualization));
            text_style::drawEllipsized (g, letter, line.withWidth (letterWidth).toNearestInt(), juce::Justification::centredLeft);
            paintBar (g, juce::Rectangle<float>::leftTopRightBottom (barLeft, line.getY(), barRight, line.getBottom()), bar, item.row,
                      item.minimum, item.maximum, colour);
            const auto number = juce::Rectangle<float>::leftTopRightBottom (barRight + gap, line.getY(), barRight + gap + numberWidth, line.getBottom());
            g.setFont (monoFont (context, typography::TextRole::readout, typography::Composition::information));
            g.setColour (bar.shown ? valueColour : COL_MUTED);
            text_style::drawEllipsized (g, text, number.toNearestInt(), juce::Justification::centredRight);
            if (letter == "A" && differenceWidth > 0.0f && item.difference.isNotEmpty())  // 差は A の行に（A が主語）
            {
                g.setColour (COL_TEXT_SECONDARY);
                g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::visualization));
                text_style::drawEllipsized (g, item.difference, juce::Rectangle<float>::leftTopRightBottom (number.getRight() + 10.0f, line.getY(),
                                                                                                    area.getRight(), line.getBottom()).toNearestInt(),
                                            juce::Justification::centredLeft);
            }
        }
    }
}

// V：上の段の項目の時間の線（古い → 新しい）。縦軸は線の値の範囲（動きが見えるように）で、上と下の値を添える。
// A は金の太い線を下に、V は水色の細い線を上に（同じ値でも両方見える）。
void paintTimeLines (juce::Graphics& g, juce::Rectangle<float> area, const Prepared& item, const Sides& sides,
                     presentation::Context context)
{
    const auto& a = sides.a[item.row.fact];
    const auto& v = sides.other[item.row.fact];
    const auto count = std::min (a.size(), v.size());
    if (count < 2 || area.getHeight() < 30.0f) return;
    help::note (area, help_text::timeLines);
    const auto shift = item.otherBar.shift;
    auto low = std::numeric_limits<double>::infinity(), high = -low;
    for (size_t index = 0; index < count; ++index)
        for (const auto value : { a[a.size() - count + index], v[v.size() - count + index] + shift })
            if (std::isfinite (value)) { low = std::min (low, value); high = std::max (high, value); }
    if (! std::isfinite (low)) return;
    const auto pad = std::max (high - low, item.row.step * 0.5) * 0.12;
    low -= pad;
    high += pad;
    const auto unitFont = labelFont (context, typography::TextRole::unit, typography::Composition::visualization);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::visualization));
    text_style::drawEllipsized (g, juce::String (item.row.name) + " / OVER TIME", area.removeFromTop (16.0f).toNearestInt(),
                                juce::Justification::centredLeft);
    // 上と下の値は線の右の余白に（線と重ねない）。
    const auto highText = valueText (item.row.unit, high), lowText = valueText (item.row.unit, low);
    const auto labelWidth = std::ceil (std::max (text_style::shownWidth (unitFont, highText), text_style::shownWidth (unitFont, lowText))) + 8.0f;
    auto chart = area.withTrimmedLeft (20.0f).withTrimmedBottom (2.0f);
    const auto labels = chart.removeFromRight (labelWidth);
    const auto y = [&chart, low, high] (double value)
    { return chart.getBottom() - static_cast<float> (juce::jlimit (0.0, 1.0, (value - low) / (high - low))) * chart.getHeight(); };
    g.setColour (COL_MUTED.withAlpha (0.12f));
    for (const auto edge : { chart.getY(), chart.getBottom() - 1.0f })
        g.fillRect (juce::Rectangle<float> (chart.getX(), edge, chart.getWidth(), 1.0f));
    g.setColour (COL_TEXT_TERTIARY.withAlpha (0.85f));
    text_style::drawText (g, highText, labels.withHeight (13.0f), juce::Justification::centredRight, false);
    text_style::drawText (g, lowText, labels.withTop (labels.getBottom() - 13.0f), juce::Justification::centredRight, false);
    const auto path = [&] (const std::vector<double>& values, double offset)
    {
        juce::Path line;
        bool started = false;
        for (size_t index = 0; index < count; ++index)
        {
            const auto value = values[values.size() - count + index];
            if (! std::isfinite (value)) { started = false; continue; }
            const juce::Point<float> point { chart.getX() + chart.getWidth() * static_cast<float> (index) / static_cast<float> (count - 1),
                                             y (value + offset) };
            if (! started) { line.startNewSubPath (point); started = true; } else line.lineTo (point);
        }
        return line;
    };
    g.setColour (COL_FLORA_BR.withAlpha (0.9f));
    g.strokePath (path (a, 0.0), juce::PathStrokeType (2.4f));
    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.95f));
    g.strokePath (path (v, shift), juce::PathStrokeType (1.3f));
}

const reference_audition::RuntimeDetailedMeasurement* cueMeasurement (const State& state)
{
    return state.cueMeasurement && state.cueMeasurement->audio.sampleRateHz > 0 ? state.cueMeasurement.get() : nullptr;
}
}

bool rangeStripBinding (const juce::String& binding) noexcept
{
    return binding == "dynamics" || binding == "loudness" || binding == "stereo" || binding == "waveform" || binding == "transient";
}

bool paintCueRangeStrips (juce::Graphics& g, juce::Rectangle<float> bounds, const State& state, const juce::String& binding,
                          presentation::Context context)
{
    const auto rows = rowsFor (binding);
    if (rows.empty()) return false;
    // C：Cue（無ければ曲全体）に丸ごと入る区間。A：直近の C の窓の長さを、C の区間の長さにまとめ直す。C の値がまだ無い
    // （Kirin OS が測っていない・Kirin OS のプレビュー）ときも帯の形は描き、値は「—」（Hypha 本体と同じ見た目）。
    const auto* measurement = cueMeasurement (state);
    const auto rate = measurement != nullptr ? static_cast<double> (measurement->audio.sampleRateHz) : 48'000.0;
    Sides sides;
    if (measurement != nullptr)
    {
        const auto start = std::isfinite (state.cueStartSeconds) ? std::llround (state.cueStartSeconds * rate) : 0;
        const auto end = std::isfinite (state.cueEndSeconds) ? std::llround (state.cueEndSeconds * rate) : measurement->audio.totalSampleFrames;
        sides.other = reference_audition::kirinHops (*measurement, start, end);
    }
    const auto timeline = state.visualTimeline;
    const auto& ticks = timeline ? timeline->aTicks : nullptr;
    const auto window = timeline ? std::clamp (timeline->binding.matchWindowBlocks, minimumATicks, reference_audition::DynamicsTicks::capacity) : 0;
    const auto used = ticks ? std::min (static_cast<int> (ticks->size()), window) : 0;
    const auto binsPerHop = sides.other.hopSamples > 0
        ? std::max (1, static_cast<int> (std::llround (static_cast<double> (sides.other.hopSamples) * 10.0 / rate))) : 1;
    if (used >= minimumATicks)
        sides.a = reference_audition::aggregateHops (ticks->data() + (ticks->size() - static_cast<size_t> (used)), static_cast<size_t> (used),
                                                     binsPerHop, timeline->aTickChannels);
    sides.aReady = sides.a.size() > 0;
    const auto gain = comparisonGainDb (state);
    const bool matched = std::isfinite (gain);
    sides.shift = matched ? gain : 0.0;
    const auto prepared = prepare (rows, sides);
    bool anyHeard = false;
    for (const auto& item : prepared) anyHeard = anyHeard || item.row.asHeard;

    surface_material::paintPanel (g, bounds, 0.72f);
    auto header = bounds.removeFromTop (28.0f).reduced (9.0f, 1.0f).toNearestInt();
    help::note (header, help_text::strips);
    g.setColour (COL_NORMAL.withAlpha (0.92f));
    g.setFont (labelFont (context, typography::TextRole::metricLabel, typography::Composition::visualization));
    text_style::drawEllipsized (g, binding.toUpperCase(), header.removeFromLeft (juce::roundToInt (header.getWidth() * 0.36f)),
                                juce::Justification::centredLeft);
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    const auto aLegend = sides.aReady ? "A LAST " + juce::String (juce::roundToInt (used / 10.0)) + " S" : juce::String ("A WAITING");
    paintReferenceLegend (g, aLegend + " / " + cuePartLegend ("C", state.cuePart, nan, nan, false)
                                 + (anyHeard && ! matched ? " / LEVEL NOT MATCHED" : ""), header);
    paintRows (g, bounds.reduced (14.0f, 8.0f), prepared, sides, context);
    return true;
}

bool paintVersionRangeStrips (juce::Graphics& g, juce::Rectangle<float> area, const reference_audition::VisualTimeline* timeline,
                              const juce::String& binding, double gainDb, presentation::Context context)
{
    const auto rows = rowsFor (binding);
    if (rows.empty() || timeline == nullptr || ! timeline->aPairTicks || ! timeline->vPairTicks) return false;
    const auto count = std::min (timeline->aPairTicks->size(), timeline->vPairTicks->size());
    if (static_cast<int> (count) < minimumATicks) return false;
    Sides sides;
    sides.a = reference_audition::aggregateHops (timeline->aPairTicks->data() + (timeline->aPairTicks->size() - count), count, 1,
                                                 timeline->pairTickChannels);
    sides.other = reference_audition::aggregateHops (timeline->vPairTicks->data() + (timeline->vPairTicks->size() - count), count, 1,
                                                     timeline->pairTickChannels);
    sides.aReady = sides.a.size() > 0;
    sides.letter = "V";
    sides.shift = std::isfinite (gainDb) ? gainDb : 0.0;
    const auto prepared = prepare (rows, sides);
    auto overlay = area.removeFromTop (std::round (area.getHeight() * 0.32f));
    area.removeFromTop (6.0f);
    paintTimeLines (g, overlay.reduced (6.0f, 0.0f), prepared.front(), sides, context);
    paintRows (g, area.reduced (6.0f, 0.0f), prepared, sides, context);
    return true;
}
}
