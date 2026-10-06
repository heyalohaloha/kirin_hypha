#include "HyphaReferenceCueSummary.h"

#include "HyphaReferenceAComparison.h"
#include "HyphaReferenceBlauertZones.h"
#include "HyphaReferenceComponent.h"
#include "HyphaReferenceHelp.h"
#include "HyphaReferenceHelpText.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <array>
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

bool paintCueSpectrum (juce::Graphics& g, juce::Rectangle<float> area, const State& state, double minimumHz, double maximumHz,
                       presentation::Context context)
{
    // C の Cue の値があるときだけ。A の窓がまだ足りなければ C だけを描き、A は「集めています」と出す。
    if (! state.separateComparisons || state.comparisonSlot != 2 || ! state.cueKirin) return false;
    help::note (area, help_text::cueSpectrum);
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
    paintBlauertZones (g, area, minimumHz, maximumHz);  // Blauert の帯（2026-10-04）
    for (auto it = lower.rbegin(); it != lower.rend(); ++it) band.lineTo (*it);
    band.closeSubPath();
    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.10f));
    g.fillPath (band);
    // A を太く下に、C を細く上に（同じ値でも両方見える。2026-10-04）。
    const bool aReady = kirinComparable (state) && state.aKirin->frames >= minimumAFrames;
    if (aReady)
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
        g.setColour (COL_FLORA_BR.withAlpha (0.9f));
        g.strokePath (live, juce::PathStrokeType (2.8f));
    }
    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.95f));
    g.strokePath (median, juce::PathStrokeType (1.4f));
    if (aReady)  // 曲の中の帯どうしの差なので、音量を合わせていなくても出せる
        paintBlauertReadout (g, area, 'C', blauertVersus (state.aKirin->centersHz, state.aKirin->medianDb,
                                                         cue.centersHz, cue.medianDb),
                             minimumHz, maximumHz, context);
    return true;
}

juce::String cueSpectrumLegend (const State& state)
{
    const bool aReady = kirinComparable (state) && state.aKirin->frames >= minimumAFrames;
    const auto a = aReady ? "A LAST " + juce::String (juce::roundToInt (state.aKirin->frames / 10.0)) + " S" : juce::String ("A WAITING");
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    return a + " / " + cuePartLegend ("C", state.cuePart, nan, nan, false)
        + (std::isfinite (comparisonGainDb (state)) ? "" : " / LEVEL NOT MATCHED");
}

juce::String cuePartLegend (const char* side, reference_audition::CuePart part, double startSeconds, double endSeconds, bool range)
{
    using reference_audition::CuePart;
    const auto clock = [] (double seconds)
    {
        const auto whole = juce::roundToInt (seconds);
        return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
    };
    const auto times = range && std::isfinite (startSeconds) && std::isfinite (endSeconds) && endSeconds > startSeconds
        ? " " + clock (startSeconds) + "-" + clock (endSeconds) : juce::String();
    const juce::String name { side };
    switch (part)
    {
        case CuePart::whole: return name + " WHOLE";
        case CuePart::chorus: return name + " CHORUS" + times;
        case CuePart::loudest: return name + " LOUDEST 30 S" + times;
        case CuePart::cue: case CuePart::unknown: break;
    }
    return name + " CUE" + times;
}

juce::String matchReadout (const State& state)
{
    if (state.comparisonMode == "original") return "ORIGINAL LEVEL";
    const auto gain = comparisonGainDb (state);
    // 仕様 C：A が Cue の長さ（最長 30 秒）たまるまでは合わせない。進み具合を出す（たまれば押してすぐ合う）。
    if (! std::isfinite (gain) && state.comparisonMode == "loudness_match" && state.aWindowNeededBlocks > 0
        && state.aWindowBlocks < state.aWindowNeededBlocks)
        return "A " + juce::String (state.aWindowBlocks / 10) + " / " + juce::String (state.aWindowNeededBlocks / 10) + " S";
    if (! std::isfinite (gain)) return {};
    const auto value = "C " + gainText (displayGainDb (state, gain)) + " dB";
    // 合わせ方（MATCHED AND FIXED）は状態の行が言い、鳴っていることは色で分かる。2 度言うと読みが切れた（「MATCHED / C
    // −4.9 dB /…」2026-10-04、「MATCH済み / C −1.2…」2026-10-05）。鳴っていなければ鳴らすときの gain。
    return state.bSelected && state.audibleComparisonSlot == 2 ? value : "ON PLAY / " + value;
}

namespace
{
// 2026-10-04：1 段（図を大きくする）。左に見出し（CよりA（dB））を 1 度だけ、4 帯域は名前と差を 1 行に。
// 差は見出しの A を主語に言葉で（「3.7少ない」。HyphaReferenceAComparison.h）。
constexpr const char* bandNames[] { "LOW 20-250", "LOW-MID 250-2k", "MID 2k-8k", "HIGH 8k-20k" };
constexpr const char* bandHeading = "A VS C (dB)";
constexpr int bandGap = 6, bandPadding = 8, bandNameGap = 6;

struct BandLayout
{
    int heading = 0;
    std::array<int, 4> cells {};
    bool fits = false;
};

// 欄の幅は名前と大きな差の文（24.5）の幅に余白を足し、残りを等分する（値が変わっても欄は動かない）。
// 足りなければ等分にして名前を省略する（差の文は省略しない）。
BandLayout bandLayout (int width, const presentation::Context& context)
{
    BandLayout layout;
    const auto words = labelFont (context, typography::TextRole::unit, typography::Composition::information);
    layout.heading = juce::roundToInt (std::ceil (text_style::shownWidth (words, bandHeading))) + 10;
    const auto reserve = std::ceil (std::max (aComparisonWidth (compareBand ({ 0.0, 24.5 }), context), aComparisonWidth (compareBand ({ 24.5, 0.0 }), context)));
    const auto available = width - layout.heading - bandGap * 3;
    std::array<int, 4> need {};
    int total = 0;
    for (size_t band = 0; band < need.size(); ++band)
    {
        need[band] = juce::roundToInt (std::ceil (text_style::shownWidth (words, bandNames[band]) + bandNameGap + reserve)) + bandPadding * 2;
        total += need[band];
    }
    layout.fits = total <= available;
    for (size_t band = 0; band < need.size(); ++band)
        layout.cells[band] = layout.fits ? need[band] + (available - total) / 4 : available / 4;
    return layout;
}
}

bool bandSummaryFits (int width, const presentation::Context& context)
{
    return bandLayout (width, context).fits;
}

void paintBandSummary (juce::Graphics& g, juce::Rectangle<int> area, const State& state, presentation::Context context)
{
    const auto gain = comparisonGainDb (state);
    const bool ready = kirinComparable (state) && state.aKirin->frames >= minimumAFrames && std::isfinite (gain);
    const auto layout = bandLayout (area.getWidth(), context);
    help::note (area, help_text::cueBands);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
    text_style::drawEllipsized (g, bandHeading, area.removeFromLeft (layout.heading), juce::Justification::centredLeft);
    for (size_t band = 0; band < layout.cells.size(); ++band)
    {
        auto cell = area.removeFromLeft (layout.cells[band]);
        area.removeFromLeft (bandGap);
        surface_material::paintPanel (g, cell.toFloat(), 0.6f);
        auto inner = cell.reduced (bandPadding, 0);
        const auto c = ready ? state.cueKirin->balanceDb[band] + gain : std::numeric_limits<double>::quiet_NaN();
        const auto a = ready ? state.aKirin->balanceDb[band] : std::numeric_limits<double>::quiet_NaN();
        const auto comparison = std::isfinite (c) && a > -200.0 ? compareBand ({ a, c }) : AComparison {};
        const auto phraseWidth = comparison.shown() ? std::ceil (aComparisonWidth (comparison, context)) : 16.0f;
        auto phrase = inner.removeFromRight (std::min (inner.getWidth(), juce::roundToInt (phraseWidth)));
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawEllipsized (g, bandNames[band], inner.withTrimmedRight (bandNameGap), juce::Justification::centredLeft);
        if (comparison.shown())
            paintAComparison (g, comparison, phrase.toFloat(), juce::Justification::centredRight, context, COL_TEXT_SECONDARY,
                              COL_OBSERVATORY_VALUE);
        else
        {
            g.setColour (COL_MUTED);
            g.setFont (monoFont (context, typography::TextRole::readout, typography::Composition::information));
            text_style::drawEllipsized (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")), phrase, juce::Justification::centredRight);
        }
    }
}

void paintCueBar (juce::Graphics& g, juce::Rectangle<int> area, const State& state, presentation::Context context)
{
    help::note (area, help_text::cueBar);
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
