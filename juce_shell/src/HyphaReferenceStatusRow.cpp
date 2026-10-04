#include "HyphaReferenceComponent.h"

#include "HyphaReferenceStatusModel.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <cmath>

// REF の状態の行（H9：聴ける／準備中／できない）。2026-10-04 から、足元の段があるときはどの画面でも足元の左に出す
// （StatusStrip、エディターが置く）。足元の段が無い 100%・125% は REF の中の一番下。描き方はどちらも同じ。
namespace hypha::reference_ui
{
int Component::statusRowHeight() const noexcept
{
    return detailedLayout() && current.sampleRateApprovalRequired ? 32 : detailedLayout() ? 24 : 18;
}

bool Component::rolePage() const noexcept
{
    return detailedLayout() && current.separateComparisons && ! isBlindSession (current.blindPhase);
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

// 案内が次の一手を言っているあいだは行を出さない。断り・アクション・待ちの超過のときは出す（H6）。
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
        actionButton.setBounds (row.removeFromRight (detailedLayout() && current.sampleRateApprovalRequired ? 238 : detailedLayout() ? 188 : 116));
}

void Component::paintStatusRow (juce::Graphics& g, juce::Rectangle<int> statusArea) const
{
    if (statusRowConcealed()) return;
    const auto line = referenceStatusLine (current);
    const auto statusColour = line.kind == StatusKind::ready ? COL_SPECTRUM_DELTA_BR : line.kind == StatusKind::waiting ? COL_FLORA_BR : COL_TEXT_SECONDARY;
    g.setColour (statusColour.withAlpha (0.92f));
    g.setFont (labelFont (presentationContext, typography::TextRole::readout, typography::Composition::information));
    const auto& statusText = line.text;
    auto available = statusArea;  // REF の中では 1・2・REVEAL を置いた残り（レイアウトが入れ物の大きさで決める）
    if (blindButton.isVisible() && blindButton.getParentComponent() == &statusStrip) available.removeFromRight (detailedLayout() ? 120 : 90);
    if (actionButton.isVisible())
        available.removeFromRight (detailedLayout() && current.sampleRateApprovalRequired ? 244 : detailedLayout() ? 194 : 122);
    // 鳴っている役の gain の読みは要るだけの幅（右）。残りを状態の文に渡す（C は MATCH の横に出す）。案内が出ている
    // あいだと Blind の間は出さない（Blind では gain がどちらが鳴っているかの手がかりになる）。
    const bool gainShown = detailedLayout() && current.bSelected && std::isfinite (current.appliedGainDb) && ! checkPage()
                        && ! guideShown && ! isBlindSession (current.blindPhase);
    const auto gainFont = labelFont (presentationContext, typography::TextRole::status, typography::Composition::information);
    const auto gain = gainShown ? gainReadout (current) : juce::String();
    auto primary = available;
    const auto gainArea = gainShown ? primary.removeFromRight (juce::jmin (available.getWidth() / 2,
                              juce::roundToInt (std::ceil (text_style::shownWidth (gainFont, gain))) + 12))
                                    : juce::Rectangle<int>();
    const bool shown = statusLineShown();
    if (shown) paintStatusDot (g, primary, line.kind);
    if (shown)
        text_style::drawEllipsized (g, statusText, primary.reduced (4, 0).withTrimmedLeft (12),
                                    juce::Justification::centredLeft);
    if (gainShown)
    {
        g.setColour ((current.gainLimited ? COL_FLORA_BR : COL_MUTED).withAlpha (0.9f));
        g.setFont (labelFont (presentationContext, typography::TextRole::status, typography::Composition::information));
        text_style::drawEllipsized (g, gain, gainArea.reduced (4, 0), juce::Justification::centredRight);
    }
}
}
