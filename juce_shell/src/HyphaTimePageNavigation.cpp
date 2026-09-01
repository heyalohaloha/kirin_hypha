#include "HyphaTimePageNavigation.h"

#include "HyphaAnalysisUiText.h"
#include "HyphaTheme.h"

namespace hypha
{
TimePageNavigation::TimePageNavigation()
{
    compactCycle.setTitle ("TIME detail");
    compactCycle.setDescription (
        "Cycle History, Attack, Sharpness Delta, and POST live facts");
    compactCycle.setColour (juce::TextButton::buttonColourId, kFieldFill);
    compactCycle.setColour (juce::TextButton::textColourOnId, COL_SPECTRUM_DELTA);
    compactCycle.setColour (juce::TextButton::textColourOffId, COL_SPECTRUM_DELTA);
    compactCycle.onClick = [this]
    {
        choose (analysis_navigation::nextTimePage (selectedPage));
    };
    for (auto* button : { &historyButton, &attackButton, &sharpButton, &liveButton })
        addChildComponent (button);
    addAndMakeVisible (compactCycle);
    historyButton.setTooltip ("Session history. Direct Observatory view.");
    attackButton.setTooltip ("PRE/POST transient event facts and differences.");
    sharpButton.setTooltip ("Sharpness Delta history. Unit: acum.");
    liveButton.setTooltip ("Absolute POST facts on fixed scales.");
    historyButton.onClick = [this] { choose (Page::meters); };
    attackButton.onClick = [this] { choose (Page::attack); };
    sharpButton.onClick = [this] { choose (Page::perceptual); };
    liveButton.onClick = [this] { choose (Page::absolute); };
    updateControls();
}

void TimePageNavigation::setPage (Page value)
{
    value = analysis_navigation::isTimePage (value) ? value : Page::meters;
    if (selectedPage == value)
        return;
    selectedPage = value;
    updateControls();
}

void TimePageNavigation::setDirect (bool value)
{
    if (direct == value)
        return;
    direct = value;
    updateControls();
    resized();
}

int TimePageNavigation::visibleDirectTabCount() const noexcept
{
    return static_cast<int> (historyButton.isVisible())
         + static_cast<int> (attackButton.isVisible())
         + static_cast<int> (sharpButton.isVisible())
         + static_cast<int> (liveButton.isVisible());
}

void TimePageNavigation::resized()
{
    if (! direct)
    {
        compactCycle.setBounds (getLocalBounds().reduced (2));
        return;
    }
    auto remaining = getLocalBounds();
    const auto tabWidth = remaining.getWidth() / 4;
    historyButton.setBounds (remaining.removeFromLeft (tabWidth));
    attackButton.setBounds (remaining.removeFromLeft (tabWidth));
    sharpButton.setBounds (remaining.removeFromLeft (tabWidth));
    liveButton.setBounds (remaining);
}

void TimePageNavigation::choose (Page value)
{
    setPage (value);
    if (onPageChange)
        onPageChange (selectedPage);
}

void TimePageNavigation::updateControls()
{
    compactCycle.setVisible (! direct);
    compactCycle.setButtonText (analysis_navigation::timePageLabel (selectedPage));
    compactCycle.setTooltip (selectedPage == Page::meters
        ? "Switch TIME detail view"
        : selectedPage == Page::attack
            ? analysis_ui::switchViewTooltip ("Attack")
        : selectedPage == Page::perceptual
            ? analysis_ui::switchViewTooltip ("Sharpness Delta")
            : analysis_ui::switchViewTooltip ("POST live facts"));
    for (auto* button : { &historyButton, &attackButton, &sharpButton, &liveButton })
        button->setVisible (direct);
    historyButton.setToggleState (selectedPage == Page::meters, juce::dontSendNotification);
    attackButton.setToggleState (selectedPage == Page::attack, juce::dontSendNotification);
    sharpButton.setToggleState (selectedPage == Page::perceptual, juce::dontSendNotification);
    liveButton.setToggleState (selectedPage == Page::absolute, juce::dontSendNotification);
}
}
