#include "HyphaLiveBlindComponent.h"
#include "HyphaSurfaceMaterial.h"
#include <cmath>

namespace hypha::live_blind_ui
{
using Stage = live_compare::BlindStage;
Component::Component()
{
    setComponentID ("live-blind-screen");
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setLookAndFeel (&look);
    int index = 0;
    for (auto* label : { &title, &status, &detail })
    {
        label->setComponentID ("live-blind-text-" + juce::String (++index));
        label->setJustificationType (juce::Justification::centred);
        label->setMinimumHorizontalScale (1.0f);
        label->setColour (juce::Label::textColourId, COL_NORMAL);
        addAndMakeVisible (*label);
    }
    index = 0;
    for (auto* button : { &one, &two, &answer, &end, &approve })
    {
        const char* ids[] { "source-1", "source-2", "answer", "end", "approve" };
        button->setComponentID ("live-blind-" + juce::String (ids[index++]));
        button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        button->setWantsKeyboardFocus (true);
        addAndMakeVisible (*button);
    }
    one.onClick = [this] { if (onSelect) onSelect (1); };
    two.onClick = [this] { if (onSelect) onSelect (2); };
    answer.onClick = [this] { if (onAnswer) onAnswer(); };
    end.onClick = [this] { if (onEnd) onEnd(); };
    approve.onClick = [this] { if (onApprove) onApprove(); };
    refresh();
}

Component::~Component() { setLookAndFeel (nullptr); }

void Component::setState (const live_compare::LiveBlindStatus& next, bool hostPlaying, float postActual)
{
    current = next;
    playing = hostPlaying;
    actualPost = postActual;
    refresh();
}

void Component::refresh()
{
    const bool live = current.stage == Stage::active && ! current.trial.invalidated;
    const bool revealed = live && current.trial.revealed;
    const bool finishing = current.stage == Stage::finishing;
    title.setText (revealed ? "BLIND RESULT" : "LIVE BLIND", juce::dontSendNotification);
    const auto returnDb = actualPost > 0.0f ? -20.0 * std::log10 (actualPost) : 0.0;
    juce::String instruction, explanation = returnDb > 0.05
        ? "END returns +" + juce::String (returnDb, 1) + " dB" : "END restores normal level";
    if (current.stage == Stage::failed)
        instruction = current.waiting == live_compare::MatchFailure::outOfRange
            ? "MATCH over 24 dB" : "MATCH failed; try again";
    else if (current.stage == Stage::invalidated || current.trial.invalidated)
    {
        instruction = "Blind stopped; POST plays";
        const auto db = actualPost > 0.0f ? -20.0 * std::log10 (actualPost) : 0.0;
        explanation = db > 0.05 ? "END returns +" + juce::String (db, 1) + " dB" : explanation;
    }
    else if (finishing)
        instruction = playing ? "Returning to normal level" : "Return waiting for audio";
    else if (current.stage == Stage::approval)
    {
        instruction = "Lower POST to match levels?";
        explanation = "Lower " + juce::String (std::abs (current.lowerPostDb), 1)
            + " dB; END returns the same amount";
    }
    else if (revealed)
    {
        const char* answers[] { "", "ANSWER: PREFER 1", "ANSWER: PREFER 2", "ANSWER: NO PREFERENCE", "ANSWER: CANNOT TELL" };
        instruction = answers[juce::jlimit (0, 4, current.trial.answer)];
    }
    else if (live)
        instruction = current.trial.played == 3 ? "Choose an answer when ready" : "Try both sources while playing";
    else if (! playing)
        instruction = "Play the DAW to begin";
    else if (current.waiting == live_compare::MatchFailure::outOfRange)
        instruction = "MATCH over 24 dB";
    else if (current.waiting == live_compare::MatchFailure::notEnoughSignal)
        instruction = "MATCH needs more signal";
    else
        instruction = "Matching levels; POST plays";
    status.setText (instruction, juce::dontSendNotification);
    detail.setText (explanation, juce::dontSendNotification);
    one.setButtonText (revealed ? (current.trial.firstPre ? "1: PRE" : "1: POST") : "SOURCE 1");
    two.setButtonText (revealed ? (current.trial.firstPre ? "2: POST" : "2: PRE") : "SOURCE 2");
    one.setToggleState (live && current.trial.audible == 1, juce::dontSendNotification);
    two.setToggleState (live && current.trial.audible == 2, juce::dontSendNotification);
    for (auto* button : { &one, &two })
    {
        button->setVisible (live);
        button->setEnabled (live);
        button->setTitle (button->getButtonText());
        button->setDescription (button->getButtonText());
        button->setTooltip (button->getButtonText());
    }
    answer.setVisible (live && ! revealed);
    answer.setEnabled (live && ! revealed && current.trial.played == 3);
    answer.setTitle ("Choose an answer and reveal");
    answer.setDescription (answer.getTitle());
    approve.setVisible (current.stage == Stage::approval);
    approve.setTitle ("Lower POST and begin Blind");
    approve.setDescription (detail.getText());
    end.setEnabled (! finishing);
    end.setTitle ("End and restore normal level");
    end.setDescription (end.getTitle() + " / " + explanation);
    end.setTooltip (end.getDescription());
    resized();
    repaint();
}

void Component::resized()
{
    context = presentation::forEditor (getWidth(), getHeight());
    const bool compact = getWidth() < 450;
    for (auto* button : { &one, &two, &answer, &end, &approve }) button->setPresentationContext (context);
    title.setFont (labelFont (context, typography::TextRole::sectionTitle, typography::Composition::information));
    status.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    detail.setFont (labelFont (context, typography::TextRole::status, typography::Composition::information));
    auto area = getLocalBounds().reduced (compact ? 12 : 24);
    title.setBounds (area.removeFromTop (compact ? 24 : 40));
    status.setBounds (area.removeFromTop (compact ? 30 : 48));
    detail.setBounds (area.removeFromBottom (compact ? 34 : 46));
    auto actions = area.removeFromBottom (compact ? 28 : 40);
    end.setBounds (actions.removeFromRight (actions.getWidth() / 2).reduced (3, 0));
    answer.setBounds (actions.reduced (3, 0));
    area.reduce (0, compact ? 5 : 12);
    approve.setBounds (area);
    one.setBounds (area.removeFromLeft (area.getWidth() / 2).reduced (3, 0));
    two.setBounds (area.reduced (3, 0));
}

void Component::paint (juce::Graphics& g)
{
    g.fillAll (BG);
    surface_material::paintInstrumentFrame (g, getLocalBounds().toFloat(), false);
}
}
