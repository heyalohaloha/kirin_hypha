#include "PluginEditor.h"

#include "HyphaHelpLineText.h"

// 2026-10-04：300% 以上ではどの画面も同じ説明の行を使い、項目が何で、どう使うかまで言う。
// 300% 以上の LEVEL・TIME・FREQ・SPACE と REF の B・C・V で項目を指すと、
// その説明を足元に一行で出し、離すと戻る（HyphaHelpLineBar.h）。図・値・タブを指しているあいだは足元の段の全幅
// （何かと使い方まで書ける）、足元のボタンを指しているあいだはボタンを隠さないよう左の状態の所だけ。説明は部品の今の
// 説明（吹き出しの文。LEVEL の値の欄と履歴は View の metricHelpAt、REF は helpAt）で、ここでは吹き出しを出さない
// （HoverHelpTooltipWindow がエディターの印を見る）。長すぎる説明は HyphaHelpLineText.h の版。REF の A・Blind の
// 画面と 200% 以下は今までどおり吹き出し。

bool KirinHyphaEditor::helpLineActive() const
{
    using namespace hypha::observatory;
    if (densityForWidth (displayViewport (getWidth(), getHeight()).width) != Density::inspection
        || observatoryView.hybridVuVisible() || observatoryView.footerBounds().isEmpty())
        return false;
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (localBlindOpen || liveBlindOpen) return false;
    if (observatoryView.domain() == Domain::reference)
        return isPost && referenceView.isVisible() && referenceView.helpInLine();
   #endif
    return observatoryView.domain() != Domain::reference;
}

KirinHyphaEditor::HelpLine KirinHyphaEditor::helpLineAt (juce::Point<int> point)
{
    if (! helpLineActive() || ! hypha::HoverHelpPreference::shared().isEnabled()) return {};
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    // A REF status cut to fit the footer reads whole, across the footer row, while it is pointed at (2026-10-05).
    if (auto& strip = referenceView.footerStatusStrip(); isPost && strip.isShowing() && strip.getParentComponent() == &scaleRoot)
        if (const auto whole = referenceView.statusLineHelp (strip.getLocalPoint (this, point)); whole.isNotEmpty())
            return { whole, true };
   #endif
    // In the footer row only the footer's own controls speak, and only in the status at its left.
    const bool wholeRow = ! getLocalArea (&scaleRoot, observatoryView.footerBounds()).contains (point);
    for (auto* under = getComponentAt (point); under != nullptr && under != this; under = under->getParentComponent())
    {
        // The status tells its whole story there when it has one; otherwise its tooltip only repeats
        // the line it already shows, and the status is not a help.
        if (under == &feedbackStrip || under == &observatoryView.feedbackDetailsAnchor())
        {
            const auto story = statusStory();
            return story.isEmpty() ? HelpLine {} : HelpLine { story.joinIntoString (" "), true };
        }
        juce::String text;
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        if (under == &referenceView)
            text = referenceView.helpAt (referenceView.getLocalPoint (this, point));
        else
       #endif
        if (under == &observatoryView)
            text = observatoryView.metricHelpAt (observatoryView.getLocalPoint (this, point));
        else if (auto* client = dynamic_cast<juce::TooltipClient*> (under))
            text = client->getTooltip();
        if (text.isNotEmpty()) return { hypha::help_line::forLine (text, wholeRow), wholeRow };
    }
    return {};
}

void KirinHyphaEditor::updateHelpLine()
{
    const bool active = helpLineActive();
    const auto& property = hypha::reference_ui::help::shownInLineProperty;
    if (static_cast<bool> (getProperties().getWithDefault (property, false)) != active)
        getProperties().set (property, active);
    const auto line = active && isMouseOverOrDragging (true) ? helpLineAt (getMouseXYRelative()) : HelpLine {};
    const auto footer = observatoryView.footerBounds();
    auto area = footer;
    if (line.text.isNotEmpty() && ! line.wholeRow)
    {
        // The status at the left: up to the first control in the footer row, so none is hidden.
        std::vector<juce::Component*> owners { &observatoryView };
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        if (isPost && referenceView.footerStatusStrip().getParentComponent() == &scaleRoot)
            owners.push_back (&referenceView.footerStatusStrip());
       #endif
        for (auto* owner : owners)
            for (auto* child : owner->getChildren())
                if (const auto bounds = scaleRoot.getLocalArea (owner, child->getBounds());
                    child->isVisible() && child != &observatoryView.feedbackDetailsAnchor()
                    && dynamic_cast<juce::Button*> (child) != nullptr && footer.contains (bounds.getCentre()))
                    area.setRight (juce::jmin (area.getRight(), bounds.getX() - 4));
    }
    helpLineBar.show (line.text, area, footer, logicalPresentationContext());
}

juce::StringArray KirinHyphaEditor::statusStory() const
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (! liveCompareStory.isEmpty() && observatoryView.feedback() == liveCompareWarning)
        return liveCompareStory;
   #endif
    return {};
}

void KirinHyphaEditor::mouseMove (const juce::MouseEvent&) { updateHelpLine(); }
void KirinHyphaEditor::mouseEnter (const juce::MouseEvent&) { updateHelpLine(); }
void KirinHyphaEditor::mouseExit (const juce::MouseEvent&) { updateHelpLine(); }
