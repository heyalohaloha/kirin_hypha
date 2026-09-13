#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
void Component::resized()
{
    const int comparisonWidth = detailedLayout() ? 62 : current.separateComparisons ? 36 : 48;
    connectionStatus.setBounds (getWidth() - comparisonWidth * (current.separateComparisons ? 3 : 2) - 39, 6, 24, 18);
    auto area = getLocalBounds().reduced (6);
    auto header = area.removeFromTop (detailedLayout() ? 42 : 34);
    const int buttonWidth = detailedLayout() ? 62 : 48;
    const auto place = [&header] (juce::Component& button, int width)
    {
        button.setBounds (header.removeFromRight (width));
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
        if (current.separateComparisons) place (cButton, comparisonWidth);
        place (bButton, comparisonWidth);
        place (aButton, comparisonWidth);
    }
    if (current.separateComparisons && ! blindSession)
    {
        auto top = area.removeFromTop (detailedLayout() ? 38 : 24);
        if (detailedLayout())
        {
            const auto width = (top.getWidth() - 5) * 3 / 4;
            presetBox.setBounds (top.removeFromLeft (width).removeFromBottom (22));
            top.removeFromLeft (5);
            cueBox.setBounds (top.removeFromBottom (22));
        }
        else presetBox.setBounds (top);
        area.removeFromTop (4);
        auto row = area.removeFromTop (detailedLayout() ? 40 : 24);
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
    }
    else if (detailedLayout() && ! blindSession)
    {
        area.removeFromTop (4);
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
    else if (! blindSession && (presetBox.isVisible() || checkBox.isVisible() || candidateBox.isVisible()))
    {
        if (presetBox.isVisible()) { auto row = area.removeFromTop (24); presetBox.setBounds (row); }
        area.removeFromTop (4);
        auto selector = area.removeFromTop (24);
        constexpr int gap = 5;
        if (checkBox.isVisible() && candidateBox.isVisible())
        {
            auto checkSelector = selector.removeFromLeft ((selector.getWidth() - gap) * 5 / 12);
            selector.removeFromLeft (gap);
            checkSelector.removeFromLeft (42);
            selector.removeFromLeft (14);
            checkBox.setBounds (checkSelector);
            candidateBox.setBounds (selector);
        }
        else if (checkBox.isVisible())
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
    auto footer = area.removeFromBottom (detailedLayout() ? 24 : 18);
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
        if (answerButton.isVisible()) placeRight (answerButton, detailedLayout() ? 88 : 70);
    }
    else if (blindButton.isVisible())
    {
        blindButton.setBounds (footer.removeFromRight (detailedLayout() ? 112 : 84));
        footer.removeFromRight (detailedLayout() ? 8 : 6);
    }
    if (actionButton.isVisible())
        actionButton.setBounds (footer.removeFromRight (detailedLayout() ? 188 : 116));
}

}
