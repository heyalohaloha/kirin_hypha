#include "HyphaAttackV2Painter.h"
#include "HyphaAttackStage.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

namespace hypha::attack_v2
{
namespace
{
juce::String rawInterval (const KirinSnapshotInterval& i)
{
    const auto endpoint = [] (const KirinSnapshotEndpoint& e) {
        return e.kind == KIRIN_ENDPOINT_FINITE ? factNumber (e.value)
            : juce::String::fromUTF8 (e.kind == KIRIN_ENDPOINT_NEGATIVE_INFINITY ? u8"−∞" : u8"+∞"); };
    return juce::String (i.lower.closed ? "[" : "(") + endpoint (i.lower) + ","
        + endpoint (i.upper) + (i.upper.closed ? "]" : ")");
}
}
void paintEvidence (juce::Graphics& g, const Presentation& p, juce::Rectangle<int> area,
                    const presentation::Context& c, int scroll)
{
    attack_stage::paint (g, area.toFloat(), 4, 1, false);
    auto inner = area.reduced (6, 0);
    auto header = inner.removeFromTop (18);
    g.setColour (COL_NORMAL); g.setFont (monoFont (c, typography::TextRole::axis,
        typography::Composition::visualization));
    text_style::drawText (g, words (u8"Facts — frozen / ESC close / End LIVE", u8"根拠 — 固定 / ESC閉じる / End LIVE"),
        header, juce::Justification::centredLeft, false);
    juce::Graphics::ScopedSaveState save (g); g.reduceClipRegion (inner);
    int y = inner.getY() - scroll;
    const int lineHeight = juce::roundToInt (typography::resolve (c, typography::TextRole::axis).lineHeight) + 2;
    const auto line = [&] (const juce::String& s) {
        const auto height = std::max (lineHeight, juce::roundToInt (std::ceil (text_style::shownWrappedHeight (s,
            monoFont (c, typography::TextRole::axis, typography::Composition::visualization), inner.getWidth()))));
        text_style::drawLines (g, s, juce::Rectangle<int> (inner.getX(), y, inner.getWidth(), height),
                              juce::Justification::centredLeft, 8); y += height + 2; };
    line (p.header.target == KIRIN_TARGET_DELTA
        ? "Processing change to the same hit: POST - PRE."
        : "Output hit shape and level.");
    if (p.header.band != 0)
        line ("ATT / REL measure the envelope, not compressor settings.");
    line (words ("Snapshot ", u8"観測版") + juce::String (p.header.snapshot_revision)
        + words (" / presentation ", u8" / 表示版") + juce::String (p.revision));
    line ("C " + juce::String (p.header.cutoff_sample) + " / V " + juce::String (p.viewport)
        + (p.clock == ClockState::hold ? " HOLD" : " LIVE"));
    const std::array<const char*, 4> labels { "DELAY", "ATT", "REL", "LEVEL" };
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto& lane = p.lanes[i];
        const auto scope = scopeText (lane, p.live);
        line (juce::String (p.header.band == 0 ? std::array<const char*, 4> { "TRANSIENT", "STRENGTH", "CREST", "SHARPNESS" }[i] : labels[i])
            + (scope.isEmpty() ? juce::String {} : " / " + scope) + " / " + lane.number.value + " " + lane.number.unit);
        const std::array<juce::String, 4> meanings = p.header.band == 0
            ? std::array<juce::String, 4> { "First 30 ms RMS minus body RMS.",
                "First 30 ms RMS level.",
                "Peak minus RMS in the first 30 ms.",
                "Perceptual sharpness over the first 100 ms." }
            : std::array<juce::String, 4> { "POST arrival minus PRE arrival in this band.",
                "Envelope rise time: 10 to 90 percent.",
                "Decay time: peak to -20 dB.",
                "Peak of the band RMS envelope." };
        line (meanings[i]);
        line (words ("Exact / bound / unknown / pending / N/A: ", u8"確定 / 限界 / 不明 / 取得中 / 不成立: ")
            + juce::String (lane.counts[0]) + "/" + juce::String (lane.counts[1]) + "/"
            + juce::String (lane.counts[2]) + "/" + juce::String (lane.counts[3]) + "/" + juce::String (lane.counts[4]));
        if (p.summary)
        {
            const auto& raw = p.summary->lanes[i];
            line (words ("Whole interval ", u8"全N区間 ") + (raw.whole_median_available ? rawInterval (raw.whole_interval) : "---"));
            if (raw.exact_count)
                line (words ("Exact median ", u8"確定部分中央値 ") + factNumber (raw.exact_median)
                    + (lane.scope == Scope::confirmedSubset
                        ? words (" / age ", u8" / 古さ ") + juce::String (lane.ageSeconds, 2) + "s" : juce::String {}));
            for (std::size_t reason = 1; reason < KIRIN_REASON_COUNT; ++reason)
                if (lane.reasons[reason]) line (reasonText (static_cast<std::uint8_t> (reason), true) + " " + juce::String (lane.reasons[reason]));
        }
        if (p.single && p.header.band != 0)
        {
            const auto& raw = p.single->lanes[i];
            if (raw.has_interval) line (words ("Raw interval ", u8"元区間 ") + rawInterval (raw.interval));
            const auto reason = reasonText (lane.reason, true);
            line ((reason.isEmpty() ? juce::String {} : reason + " / ") + finishText (raw.finish));
            line (words ("Requested ", u8"要求窓 ") + juce::String (raw.requested_start) + juce::String::fromUTF8 (u8"…") + juce::String (raw.requested_end));
            line (words ("Measured ", u8"実測窓 ") + juce::String (raw.actual_start) + juce::String::fromUTF8 (u8"…") + juce::String (raw.actual_end));
        }
        if (p.header.band != 0 && lane.resolution > 0)
            line (words ("Resolution ", u8"判別範囲 ") + factNumber (lane.resolution) + " "
                + (i == 3 ? p.header.target == KIRIN_TARGET_DELTA ? "dB" : "dBFS" : "ms")
                + (lane.withinResolution ? words (" / Whole within", u8" / 全Nが範囲内") : ""));
    }
    line (words ("Source generation ", u8"入力世代 ") + juce::String (p.header.source.generation)
        + " / " + juce::String (p.header.source.sample_rate) + " Hz / " + juce::String (p.header.source.channels) + " ch");
    line (words ("Pair authority ", u8"比較の検証版 ") + juce::String (p.header.authority_revision));
    if (p.summary)
        line (words ("Mean in dB; identical participants for PRE and POST. Set changes break lines and fill.",
                    u8"dB平均。PRE/POSTは同参加集合。集合変更では線と塗りを切る。"));
    line (words ("Scroll / Up Down for further facts", u8"スクロール / 上下キーで続き"));
}
}
