#include "HyphaTimePageNavigation.h"

#include "HyphaAnalysisUiText.h"
#include "HyphaTheme.h"

namespace hypha
{
TimePageNavigation::TimePageNavigation()
{
    compactCycle.setTitle ("TIME detail");
    compactCycle.setDescription (
        "Cycle History, Run facts, Attack, Sharpness Delta, and POST live facts");
    compactCycle.setColour (juce::TextButton::buttonColourId, kFieldFill);
    compactCycle.setColour (juce::TextButton::textColourOnId, COL_SPECTRUM_DELTA);
    compactCycle.setColour (juce::TextButton::textColourOffId, COL_SPECTRUM_DELTA);
    compactCycle.onClick = [this]
    {
        auto next = analysis_navigation::nextTimePage (selectedPage);
        if (! runAvailable && next == Page::run)
            next = analysis_navigation::nextTimePage (next);
        choose (next);
    };
    for (auto* button : { &historyButton, &runButton, &attackButton, &sharpButton, &liveButton })
        addChildComponent (button);
    addAndMakeVisible (compactCycle);
    historyButton.setTooltip ("Session history. Direct Observatory view.");
    runButton.setTooltip ("Measured facts grouped by playback run.");
    attackButton.setTooltip ("PRE/POST transient event facts and differences.");
    sharpButton.setTooltip ("Sharpness Delta history. Unit: acum.");
    liveButton.setTooltip ("Absolute POST facts on fixed scales.");
    historyButton.onClick = [this] { choose (Page::meters); };
    runButton.onClick = [this] { choose (Page::run); };
    attackButton.onClick = [this] { choose (Page::attack); };
    sharpButton.onClick = [this] { choose (Page::perceptual); };
    liveButton.onClick = [this] { choose (Page::absolute); };
    updateControls();
}

void TimePageNavigation::setRunAvailable (bool value)
{
    if (runAvailable == value)
        return;
    runAvailable = value;
    updateControls();
    resized();
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
         + static_cast<int> (runButton.isVisible())
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
    juce::Array<juce::Button*> visible { &historyButton };
    if (runAvailable) visible.add (&runButton);
    visible.add (&attackButton); visible.add (&sharpButton); visible.add (&liveButton);
    for (int index = 0; index < visible.size(); ++index)
        visible[index]->setBounds (index + 1 == visible.size()
            ? remaining : remaining.removeFromLeft (remaining.getWidth()
                / (visible.size() - index)));
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
        : selectedPage == Page::run
            ? "Measured facts grouped by playback run"
        : selectedPage == Page::attack
            ? analysis_ui::switchViewTooltip ("Attack")
        : selectedPage == Page::perceptual
            ? analysis_ui::switchViewTooltip ("Sharpness Delta")
            : analysis_ui::switchViewTooltip ("POST live facts"));
    for (auto* button : { &historyButton, &runButton, &attackButton, &sharpButton, &liveButton })
        button->setVisible (direct);
    runButton.setVisible (direct && runAvailable);
    historyButton.setToggleState (selectedPage == Page::meters, juce::dontSendNotification);
    runButton.setToggleState (selectedPage == Page::run, juce::dontSendNotification);
    attackButton.setToggleState (selectedPage == Page::attack, juce::dontSendNotification);
    sharpButton.setToggleState (selectedPage == Page::perceptual, juce::dontSendNotification);
    liveButton.setToggleState (selectedPage == Page::absolute, juce::dontSendNotification);
}
}
