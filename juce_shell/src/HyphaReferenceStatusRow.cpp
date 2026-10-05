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
// 状態の文は「 / 」の区切りごとに、入るところまで出す（途中で「…」にしない）。区切りは大事な順に並んでいる
// （HyphaReferenceStatusModel.cpp）。最初の区切りも入らなければ省略記号（drawEllipsized）。2026-10-05、
// 300% で「BはAに追従中（直近10…」「…/ Aを下…」と、言いかけで切れていた。
juce::String fittedSegments (const juce::String& shown, const juce::Font& font, float width)
{
    if (text_style::shownWidth (font, shown) <= width) return shown;
    juce::String fitted;
    for (int slash = shown.indexOf (" /"); slash > 0; slash = shown.indexOf (slash + 1, " /"))
    {
        if (slash + 2 >= shown.length() || shown[slash + 2] != ' ') continue;
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
    return detailedLayout() ? 24 : 18;
}

// 300% の B・C・V のページの並びかどうかは、ここだけで決める（inspection の密度）。2026-10-06：200% にも当たり、200% の
// B に 300% の欄が詰まって B SET が切れ、C・V の見出しが消えていた。
bool Component::rolePage() const noexcept
{
    return presentationContext.density == observatory::Density::inspection && current.separateComparisons
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
    return ! guideShown || current.readiness == Readiness::rejected
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
        // 承認のボタンは文字の幅に合わせる（量と鳴らす役まで言う。2026-10-05、300% で「Aを0.8 dB下げて
        // Bを…」と切れた。英語の「LOWER A 0.8 DB & PLAY B」も 188 に入らなかった）。最小は今までの幅、最大は行の半分。
        const int minimum = detailedLayout() ? 188 : 116;
        const auto font = labelFont (presentationContext, typography::TextRole::action, typography::Composition::information);
        const int fitted = juce::roundToInt (std::ceil (text_style::shownWidth (font, actionButton.getButtonText()))) + 24;
        actionButton.setBounds (row.removeFromRight (juce::jlimit (minimum, juce::jmax (minimum, row.getWidth() / 2), fitted)));
    }
}

Component::StatusTextLayout Component::statusTextLayout (juce::Rectangle<int> statusArea) const
{
    StatusTextLayout layout;
    const auto line = referenceStatusLine (current, statusInFooter() && current.heldAttenuationDb < -0.05);
    layout.line = line;
    auto available = statusArea;  // REF の中では 1・2・REVEAL を置いた残り（レイアウトが入れ物の大きさで決める）
    // 承認・VERSION BLIND のボタンの手前まで（ボタンの幅は文字に合わせて決まる、layoutStatusRow）。
    if (blindButton.isVisible() && blindButton.getParentComponent() == &statusStrip)
        available.setRight (juce::jmin (available.getRight(), blindButton.getX() - 8));
    if (actionButton.isVisible() && actionButton.getParentComponent() == &statusStrip)
        available.setRight (juce::jmin (available.getRight(), actionButton.getX() - 6));
    // 鳴っている役の gain の読みは要るだけの幅（右）。残りを状態の文に渡す（C は MATCH の横に出す）。案内が出ている
    // あいだと Blind の間は出さない（Blind では gain がどちらが鳴っているかの手がかりになる）。
    const bool gainShown = detailedLayout() && current.bSelected && std::isfinite (current.appliedGainDb) && ! checkPage()
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
        paintStatusDot (g, layout.primary, kind);
        g.setColour (statusColour.withAlpha (0.92f));
        g.setFont (labelFont (presentationContext, typography::TextRole::readout, typography::Composition::information));
        text_style::drawEllipsized (g, layout.text, layout.textArea, juce::Justification::centredLeft);
    }
    if (layout.gain.isNotEmpty())
    {
        g.setColour ((current.gainLimited ? COL_FLORA_BR : COL_MUTED).withAlpha (0.9f));
        g.setFont (labelFont (presentationContext, typography::TextRole::status, typography::Composition::information));
        text_style::drawEllipsized (g, layout.gain, layout.gainArea.reduced (4, 0), juce::Justification::centredRight);
    }
}
}
