#include "HyphaReferenceComponent.h"

#include "HyphaReferenceStatusModel.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <cmath>

// REF の状態の行（聴ける／準備中／できない）。2026-10-04 から、足元の段があるときはどの画面でも足元の左に出す
// （StatusStrip、エディターが置く）。足元の段が無い 100%・125% は REF の中の一番下。描き方はどちらも同じ。
namespace hypha::reference_ui
{
namespace
{
// 状態の文は日英の空白付き区切りごとに、入るところまで出す（途中で「…」にしない）。区切りは大事な順に並んでいる
// （HyphaReferenceStatusModel.cpp）。最初の区切りも入らなければ省略記号（drawEllipsized）。2026-10-05、
// 300% で「BはAに追従中（直近10…」「…/ Aを下…」と、言いかけで切れていた。括弧の中の「/」は区切りではない
// （2026-11-12、「音量をAに合わせ中（5 / 25秒）」が「（5」で切れていた）。
juce::String fittedSegments (const juce::String& shown, const juce::Font& font, float width)
{
    if (text_style::shownWidth (font, shown) <= width) return shown;
    juce::String fitted;
    int depth = 0;
    for (int slash = 1; slash + 1 < shown.length(); ++slash)
    {
        const auto previous = shown[slash - 1];
        if (previous == '(' || previous == 0xff08) ++depth;
        if ((previous == ')' || previous == 0xff09) && depth > 0) --depth;
        if (depth > 0 || (shown[slash] != '/' && shown[slash] != 0xff0f)
            || shown[slash - 1] != ' ' || shown[slash + 1] != ' ') continue;
        const auto prefix = shown.substring (0, slash).trimEnd();
        if (prefix.isEmpty()) continue;
        if (text_style::shownWidth (font, prefix) > width) break;
        fitted = prefix;
    }
    return fitted.isNotEmpty() ? fitted : shown;
}
}

int Component::statusRowHeight() const noexcept
{
    if (! presentationContext.referenceCheck || ! actionButton.isVisible())
        return detailedLayout() ? 24 : 18;
    if (! shortPanel() && ! detailedLayout()) return 36;
    const auto font = labelFont (presentationContext, typography::TextRole::readout, typography::Composition::information);
    const auto actionFont = labelFont (presentationContext, typography::TextRole::action, typography::Composition::information);
    const auto width = panelArea().getWidth();
    const int minimum = detailedLayout() ? 188 : 116;
    const int limit = shortPanel() ? width : juce::jmax (minimum, width / 2);
    const auto actionWidth = juce::jlimit (minimum, juce::jmax (minimum, limit),
        juce::roundToInt (std::ceil (text_style::shownWidth (actionFont, actionButton.getButtonText()))) + 24);
    const auto line = text_style::shownText (referenceStatusLine (current, false).text);
    if (detailedLayout() && text_style::shownWidth (font, line) <= width - actionWidth - 22) return 24;
    const auto japaneseSplit = line.indexOfChar (juce::juce_wchar (0x3002));
    const auto englishSplit = line.indexOf (". ");
    const auto split = japaneseSplit >= 0 ? japaneseSplit : englishSplit;
    if (split < 0) return text_style::shownWidth (font, line) <= width - 16 ? 36 : 54;
    return text_style::shownWidth (font, line.substring (0, split + 1)) <= width - 16
        && text_style::shownWidth (font, line.substring (split + 1).trimStart())
            <= width - actionWidth - (shortPanel() ? 5 : 19) ? 36 : 54;
}

// 300% の B・C・V のページの並びかどうかは、ここだけで決める（inspection の密度）。2026-10-06：200% にも当たり、200% の
// B に 300% の欄が詰まって B SET が切れ、C・V の見出しが消えていた。
bool Component::rolePage() const noexcept
{
    return current.blindLargeScreen && presentationContext.density == observatory::Density::inspection && current.separateComparisons
        && ! isBlindSession (current.blindPhase);
}


bool Component::statusRowConcealed() const noexcept
{
    // 始めた VERSION BLIND は、開示の後も終了まで PRE/POST Blind と同じ画面が窓全体を覆い、結果もそこで言う（2026-10-04）。
    return isBlindSession (current.blindPhase);
}

bool Component::statusRowHasControls() const noexcept
{
    return actionButton.isVisible() || (blindButton.isVisible() && blindButton.getParentComponent() == &statusStrip);
}

// 案内が次の一手を言っているあいだは行を出さない。断り・アクション・待ちの超過のときは出す。
bool Component::statusLineShown() const noexcept
{
    return (checkChangeNotice.isNotEmpty() && current.checkId == checkChangeTarget)
        || (current.comparisonSlot == 2 && current.song.chosen && ! songSupported (current))
        || (current.comparisonSlot == 3 && current.targetsEnabled && (current.targets.measuring || current.targets.issue.isNotEmpty()))
        || ! guideShown || current.readiness == Readiness::rejected
        || actionButton.isVisible() || current.preparationOverdue.isNotEmpty();
}

void Component::layoutStatusRow (juce::Rectangle<int> row)
{
    if (blindButton.isVisible() && blindButton.getParentComponent() == &statusStrip)
    {
        blindButton.setBounds (row.removeFromRight (detailedLayout() ? 112 : 84));
        row.removeFromRight (detailedLayout() ? 8 : 6);
    }
    if (actionButton.isVisible())
    {
        if (presentationContext.referenceCheck && statusRowHeight() > 24) row = row.removeFromBottom (18);
        // 承認のボタンは文字の幅に合わせる（量と鳴らす役まで言う。2026-10-05、300% で「Aを0.8 dB下げて
        // Bを…」と切れた。英語の「LOWER A 0.8 DB & PLAY B」も 188 に入らなかった）。最小は今までの幅、最大は行の半分。
        const int minimum = detailedLayout() ? 188 : 116;
        const auto font = labelFont (presentationContext, typography::TextRole::action, typography::Composition::information);
        const int fitted = juce::roundToInt (std::ceil (text_style::shownWidth (font, actionButton.getButtonText()))) + 24;
        const int limit = presentationContext.referenceCheck && shortPanel() ? row.getWidth() : juce::jmax (minimum, row.getWidth() / 2);
        actionButton.setBounds (row.removeFromRight (juce::jlimit (minimum, juce::jmax (minimum, limit), fitted)));
    }
}

Component::StatusTextLayout Component::statusTextLayout (juce::Rectangle<int> statusArea) const
{
    StatusTextLayout layout;
    auto line = referenceStatusLine (current, statusInFooter() && current.heldAttenuationDb < -0.05);
    if (current.comparisonSlot == 2 && current.checkId == checkChangeTarget && checkChangeNotice.isNotEmpty())
    {
        if (line.kind == StatusKind::ready && current.heldAttenuationDb >= -0.05
            && current.action.kind == ActionKind::none && (! current.bSelected || current.peakShortfallDb <= 0.05)
            && current.status != "LOW DISK SPACE / REFERENCE AVAILABLE"
            && current.status != "TEMPORARY STORAGE UNAVAILABLE / REFERENCE AVAILABLE")
            line.text = current.bSelected ? checkChangeNotice + "  /  " + line.text : checkChangeNotice;
        else line.text += "  /  " + checkChangeNotice;
    }
    if (current.comparisonSlot == 3 && current.targetsEnabled && ! isBlindSession (current.blindPhase)
        && (current.targets.measuring || current.targets.issue.isNotEmpty()))
    {
        const auto notice = current.targets.measuring ? "MEASURING " + juce::String (juce::roundToInt (current.targets.progress * 100)) + "%"
                                                     : current.targets.issue;
        const bool idle = ! current.bSelected && current.heldAttenuationDb >= -0.05
            && current.pendingAudition.stage == reference_audition::PendingAuditionView::Stage::none
            && current.action.kind == ActionKind::none && ! current.kirinOsRequest;
        if (idle) line = { current.targets.measuring ? StatusKind::waiting : StatusKind::unable, notice };
        // A temporary failure must survive fitting while the audition and its controls stay active.
        else if (! current.targets.measuring) line.text = notice + " / " + line.text;
        else line.text += " / " + notice;
    }
    // In the smallest B view the playback mode belongs in DETAILS. Keep warnings and
    // attenuation intact; only shorten the ordinary playing description.
    if (presentationContext.referenceCompact && current.comparisonSlot == 3 && ! targetBar.isExpanded()
        && current.bSelected && current.audibleComparisonSlot == 3 && current.audibleHeard
        && line.kind == StatusKind::ready && current.action.kind == ActionKind::none
        && current.heldAttenuationDb >= -0.05 && current.peakShortfallDb <= 0.05
        && std::abs (current.rangeShortfallDb) <= 0.5 && current.publicationIssue.isEmpty()
        && current.targets.issue.isEmpty() && ! current.targets.measuring && current.preparationOverdue.isEmpty()
        && current.status != "LOW DISK SPACE / REFERENCE AVAILABLE"
        && current.status != "TEMPORARY STORAGE UNAVAILABLE / REFERENCE AVAILABLE")
        line.text = "B PLAYING";
    layout.line = line;
    auto available = statusArea;  // REF の中では 1・2・REVEAL を置いた残り（レイアウトが入れ物の大きさで決める）
    // 承認・VERSION BLIND のボタンの手前まで（ボタンの幅は文字に合わせて決まる、layoutStatusRow）。
    if (blindButton.isVisible() && blindButton.getParentComponent() == &statusStrip)
        available.setRight (juce::jmin (available.getRight(), blindButton.getX() - 8));
    if (actionButton.isVisible() && actionButton.getParentComponent() == &statusStrip)
        available.setRight (juce::jmin (available.getRight(), actionButton.getX() - 6));
    // 鳴っている役の gain の読みは要るだけの幅（右）。残りを状態の文に渡す（C は MATCH の横に出す）。案内が出ている
    // あいだと Blind の間は出さない（Blind では gain がどちらが鳴っているかの手がかりになる）。
    const bool gainShown = detailedLayout() && current.bSelected && std::isfinite (current.appliedGainDb) && current.comparisonSlot != 2
                        && ! guideShown && ! isBlindSession (current.blindPhase);
    const auto gainFont = labelFont (presentationContext, typography::TextRole::status, typography::Composition::information);
    layout.gain = gainShown ? gainReadout (current) : juce::String();
    layout.primary = available;
    layout.gainArea = gainShown ? layout.primary.removeFromRight (juce::jmin (available.getWidth() / 2,
                                      juce::roundToInt (std::ceil (text_style::shownWidth (gainFont, layout.gain))) + 12))
                                : juce::Rectangle<int>();
    layout.textArea = layout.primary.reduced (4, 0).withTrimmedLeft (12);
    const auto font = labelFont (presentationContext, typography::TextRole::readout, typography::Composition::information);
    const auto shown = text_style::shownText (line.text);
    layout.text = fittedSegments (shown, font, static_cast<float> (layout.textArea.getWidth()));
    layout.cut = layout.text != shown || text_style::shownWidth (font, layout.text) > static_cast<float> (layout.textArea.getWidth());
    return layout;
}

// 状態の文が切れているときの全文（どの大きさでも指すと読める：300% は足元の段の全幅、ほかは吹き出し）。
juce::String Component::statusLineWhole() const
{
    if (statusRowConcealed() || ! statusLineShown() || ! statusStrip.isVisible()) return {};
    const auto layout = statusTextLayout (statusStrip.getLocalBounds());
    return layout.cut ? layout.line.text : juce::String {};
}

// 足元で状態の文が切れているときは、文を指すと全文を足元の段の全幅に出す（PluginEditorHelpLine.cpp）。
juce::String Component::statusLineHelp (juce::Point<int> stripPoint) const
{
    const auto whole = statusLineWhole();
    return whole.isNotEmpty() && statusTextLayout (statusStrip.getLocalBounds()).primary.contains (stripPoint) ? whole
                                                                                                           : juce::String {};
}

void Component::paintStatusRow (juce::Graphics& g, juce::Rectangle<int> statusArea) const
{
    if (statusRowConcealed()) return;
    const auto layout = statusTextLayout (statusArea);
    const auto kind = layout.line.kind;
    const auto statusColour = kind == StatusKind::ready ? COL_SPECTRUM_DELTA_BR : kind == StatusKind::waiting ? COL_FLORA_BR : COL_TEXT_SECONDARY;
    if (statusLineShown())
    {
        paintStatusDot (g, presentationContext.referenceCheck && actionButton.isVisible() && statusRowHeight() > 24
                          ? layout.primary.withHeight (18) : layout.primary, kind);
        g.setColour (presentationContext.referenceCheck || presentationContext.referenceCompact ? COL_NORMAL : statusColour.withAlpha (0.92f));
        g.setFont (labelFont (presentationContext, typography::TextRole::readout, typography::Composition::information));
        const auto full = text_style::shownText (layout.line.text);
        const auto split = full.indexOfChar (juce::juce_wchar (0x3002));
        const auto englishSplit = full.indexOf (". ");
        if (presentationContext.referenceCheck && actionButton.isVisible() && statusRowHeight() > 24 && (split >= 0 || englishSplit >= 0))
        {
            const auto end = split >= 0 ? split + 1 : englishSplit + 1;
            auto first = statusArea.removeFromTop (18).withTrimmedLeft (16);
            auto second = statusArea.removeFromTop (18).withTrimmedLeft (shortPanel() ? 2 : 16);
            if (statusRowHeight() < 50) second.setRight (actionButton.getX() - 3);
            text_style::drawEllipsized (g, full.substring (0, end), first, juce::Justification::centredLeft);
            text_style::drawEllipsized (g, full.substring (end).trimStart(), second, juce::Justification::centredLeft);
        }
        else if (presentationContext.referenceCheck && actionButton.isVisible() && statusRowHeight() > 24)
        {
            auto textArea = statusArea.withTrimmedBottom (18).withTrimmedLeft (16);
            text_style::drawLines (g, full, textArea, juce::Justification::centredLeft, textArea.getHeight() / 18);
        }
        else text_style::drawEllipsized (g, layout.text, layout.textArea, juce::Justification::centredLeft);
    }
    if (layout.gain.isNotEmpty())
    {
        g.setColour ((current.gainLimited ? COL_FLORA_BR : COL_MUTED).withAlpha (0.9f));
        g.setFont (labelFont (presentationContext, typography::TextRole::status, typography::Composition::information));
        text_style::drawEllipsized (g, layout.gain, layout.gainArea.reduced (4, 0), juce::Justification::centredRight);
    }
}
}
