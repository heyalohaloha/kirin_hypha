#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
void Component::resized()
{
    const int comparisonWidth = comparisonButtonWidth();
    connectionStatus.setBounds (getWidth() - comparisonWidth * (current.separateComparisons ? 4 : 2) - 42, 6, 24, 18);
    auto area = panelArea();
    auto header = area.removeFromTop (panelHeaderHeight());
    const int buttonWidth = detailedLayout() ? 62 : 48;
    const auto place = [this,&header] (juce::Component& button, int width)
    {
        button.setBounds (header.removeFromRight (width).reduced (0, shortPanel() ? 1 : 4));
        header.removeFromRight (3);
    };
    const bool blindSession = isBlindSession (current.blindPhase);
    if (blindSession)
    {
        place (endBlindButton, current.blindPhase == BlindPhase::invalidated
            ? (detailedLayout() ? 132 : 94) : buttonWidth);
    }
    else
    {
        // A B C V（左から）。右から V・C・B・A の順に置く（H10）。
        place (bButton, comparisonWidth);
        if (current.separateComparisons) { place (cButton, comparisonWidth); place (refButton, comparisonWidth); }
        place (aButton, comparisonWidth);
    }
    if (current.separateComparisons && ! blindSession)
    {
        area.removeFromTop (panelGap());
        auto row = area.removeFromTop (detailedLayout() ? 40 : panelPickerHeight());
        auto b = row.removeFromLeft ((row.getWidth() - 5) / 2);
        row.removeFromLeft (5);
        if (detailedLayout())
        {
            versionBox.setBounds (b.removeFromBottom (25));
            checkBox.setBounds (row.removeFromBottom (25));
        }
        else
        {
            b.removeFromLeft (14); row.removeFromLeft (14);
            versionBox.setBounds (b); checkBox.setBounds (row);
        }
        songSetBox.setBounds (versionBox.getBounds()); songBox.setBounds (checkBox.getBounds()); // B の画面では同じ場所
        auto top = area.removeFromTop (selectionVisible (presetBox) || viewButton.isVisible()
            ? (detailedLayout() ? 38 : panelPickerHeight()) : 0);
        if (viewButton.isVisible())
        {
            viewButton.setBounds (top.removeFromRight (detailedLayout() ? 96 : 72)
                .removeFromBottom (detailedLayout() ? 22 : panelPickerHeight()));
            top.removeFromRight (5);
        }
        const auto width = selectionVisible (cueBox) ? (top.getWidth() - 5) * 3 / 5 : top.getWidth();
        presetBox.setBounds (top.removeFromLeft (width).removeFromBottom (detailedLayout() ? 22 : panelPickerHeight()));
        top.removeFromLeft (5);
        cueBox.setBounds (top.removeFromBottom (detailedLayout() ? 22 : panelPickerHeight()));
    }
    else if (detailedLayout() && ! blindSession)
    {
        area.removeFromTop (panelGap());
        auto selectors = area.removeFromTop (46);
        const int gap = 5;
        const int columnWidth = (selectors.getWidth() - gap * 3) / 4;
        presetBox.setBounds (selectors.removeFromLeft (columnWidth).removeFromBottom (27));
        selectors.removeFromLeft (gap);
        checkBox.setBounds (selectors.removeFromLeft (columnWidth).removeFromBottom (27));
        selectors.removeFromLeft (gap);
        candidateBox.setBounds (selectors.removeFromLeft (columnWidth).removeFromBottom (27));
        selectors.removeFromLeft (gap);
        cueBox.setBounds (selectors.removeFromBottom (27));
    }
    else if (! blindSession && (selectionVisible (presetBox) || selectionVisible (checkBox) || selectionVisible (candidateBox)))
    {
        if (selectionVisible (presetBox)) { auto row = area.removeFromTop (panelPickerHeight()); presetBox.setBounds (row); }
        area.removeFromTop (panelGap());
        auto selector = area.removeFromTop (panelPickerHeight());
        constexpr int gap = 5;
        if (selectionVisible (checkBox) && selectionVisible (candidateBox))
        {
            auto checkSelector = selector.removeFromLeft ((selector.getWidth() - gap) * 5 / 12);
            selector.removeFromLeft (gap);
            checkSelector.removeFromLeft (42);
            selector.removeFromLeft (14);
            checkBox.setBounds (checkSelector);
            candidateBox.setBounds (selector);
        }
        else if (selectionVisible (checkBox))
        {
            selector.removeFromLeft (42);
            checkBox.setBounds (selector);
        }
        else
        {
            selector.removeFromLeft (68);
            candidateBox.setBounds (selector);
        }
    }
    area.removeFromTop (panelGap());
    if (workflowControls.isVisible())
    {
        workflowControls.setBounds (area.removeFromTop (workflowControls.preferredHeight()));
        area.removeFromTop (panelGap());
    }
    if(captureControls.isVisible()) captureControls.setBounds(area.removeFromTop(captureControls.preferredHeight(area.getWidth())).reduced(0,2));
    auto footer = area.removeFromBottom (detailedLayout() && current.sampleRateApprovalRequired ? 32 : detailedLayout() ? 24 : 18);
    comparisonView.setBounds (area);
    tonalView.setBounds (area);
    if (blindSession)
    {
        const auto placeLeft = [&footer] (juce::Component& button, int width)
        {
            button.setBounds (footer.removeFromLeft (width));
            footer.removeFromLeft (3);
        };
        const auto placeRight = [&footer] (juce::Component& button, int width)
        {
            button.setBounds (footer.removeFromRight (width));
            footer.removeFromRight (3);
        };
        if (oneButton.isVisible()) placeLeft (oneButton, buttonWidth);
        if (twoButton.isVisible()) placeLeft (twoButton, buttonWidth);
        if (revealButton.isVisible()) placeRight (revealButton, detailedLayout() ? 78 : 62);
    }
    else if (blindButton.isVisible())
    {
        blindButton.setBounds (footer.removeFromRight (detailedLayout() ? 112 : 84));
        footer.removeFromRight (detailedLayout() ? 8 : 6);
    }
    if (actionButton.isVisible())
        actionButton.setBounds (footer.removeFromRight (detailedLayout() && current.sampleRateApprovalRequired ? 238 : detailedLayout() ? 188 : 116));
    layoutSelectionReadouts();
}

}
