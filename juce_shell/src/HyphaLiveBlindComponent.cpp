#include "HyphaLiveBlindComponent.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaLiveCompareRecoveryText.h"
#include "HyphaLanguage.h"
#include <cmath>
#include <tuple>

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
    for (auto* label : { &title, &status, &detail, &cause, &recovery })
    {
        label->setComponentID ("live-blind-text-" + juce::String (++index));
        label->setJustificationType (juce::Justification::centred);
        label->setMinimumHorizontalScale (1.0f);
        label->setColour (juce::Label::textColourId, COL_NORMAL);
        addAndMakeVisible (*label);
    }
    index = 0;
    for (auto* button : { &one, &two, &reveal, &end, &approve })
    {
        const char* ids[] { "source-1", "source-2", "reveal", "end", "approve" };
        button->setComponentID ("live-blind-" + juce::String (ids[index++]));
        button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        button->setWantsKeyboardFocus (true);
        addAndMakeVisible (*button);
    }
    one.onClick = [this] { if (onSelect) onSelect (1); };
    two.onClick = [this] { if (onSelect) onSelect (2); };
    reveal.onClick = [this] { if (onReveal) onReveal(); };
    end.onClick = [this] { if (onEnd) onEnd(); };
    approve.onClick = [this] { if (onApprove) onApprove(); };
    refresh();
}

Component::~Component() { setLookAndFeel (nullptr); }

void Component::setState (const live_compare::LiveBlindStatus& next, bool hostPlaying, float postActual)
{
    const auto key = [] (const live_compare::LiveBlindStatus& state)
    {
        const auto& trial = state.trial;
        return std::make_tuple (state.stage, state.waiting, state.lowerPostDb, state.generation,
            state.reason, state.observation, state.contentHeld, state.compensationOff,
            trial.active, trial.revealed, trial.invalidated, trial.firstPre, trial.audible, trial.played, trial.epoch);
    };
    const bool languageChanged = languageRevision != i18n::revision();
    if (! languageChanged && key (next) == key (current) && playing == hostPlaying && std::abs (actualPost - postActual) <= 0.0f)
        return;
    languageRevision = i18n::revision();
    current = next;
    playing = hostPlaying;
    actualPost = postActual;
    refresh();
    if (languageChanged) resized();
}

void Component::refresh()
{
    const bool live = current.stage == Stage::active && ! current.trial.invalidated;
    const bool revealed = live && current.trial.revealed;
    const bool finishing = current.stage == Stage::finishing;
    const bool stopped = current.stage == Stage::invalidated || current.trial.invalidated;
    const auto guidance = live_compare_ui::blindRecovery (current, stopped);
    const bool waiting = (current.stage == Stage::preparing || current.stage == Stage::settling)
        && (current.contentHeld || current.compensationOff
            || (playing && current.observation != live_compare::RecoveryReason::none));
    cause.setVisible (stopped || waiting);
    recovery.setVisible (stopped || waiting);
    const auto visibleCause = stopped ? guidance.reason : current.contentHeld ? live_compare::RecoveryReason::contentChanged
        : current.compensationOff ? live_compare::RecoveryReason::compensationOff : guidance.reason;
    cause.setText (live_compare_ui::cause (visibleCause), juce::dontSendNotification);
    recovery.setText (guidance.instruction, juce::dontSendNotification);
    title.setText (revealed ? "BLIND RESULT" : "LIVE BLIND", juce::dontSendNotification);
    const auto returnDb = actualPost > 0.0f ? -20.0 * std::log10 (actualPost) : 0.0;
    juce::String instruction, explanation = returnDb > 0.05
        ? "END returns +" + juce::String (returnDb, 1) + " dB" : "END restores normal level";
    if (current.stage == Stage::failed)
        instruction = current.waiting == live_compare::MatchFailure::outOfRange
            ? "MATCH over 24 dB" : "MATCH failed; try again";
    else if (stopped)
    {
        instruction = current.reason == live_compare::RecoveryReason::outputTaken
            ? "Blind stopped; output released" : "Blind stopped; POST output";
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
        instruction = "Sources revealed; keep comparing";
    else if (live)
        instruction = current.trial.played == 3 ? "Reveal the sources when ready" : "Try both sources while playing";
    else if (! playing)
        instruction = "Play the DAW to begin";
    else if (waiting && current.observation == live_compare::RecoveryReason::loopUnproven)
        instruction = "Loop comparison is unavailable";
    else if (waiting)
        instruction = "Waiting for comparison; POST plays";
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
    reveal.setVisible (live && ! revealed);
    reveal.setEnabled (live && ! revealed && current.trial.played == 3);
    reveal.setTitle ("Reveal the sources without an answer");
    reveal.setDescription (reveal.getTitle());
    reveal.setTooltip (reveal.getTitle());
    approve.setVisible (current.stage == Stage::approval);
    approve.setTitle ("Lower POST and begin Blind");
    approve.setDescription (detail.getText());
    end.setEnabled (! finishing);
    end.setTitle ("End and restore normal level");
    end.setDescription (end.getTitle() + " / " + explanation);
    end.setTooltip (end.getDescription());
    repaint();
}

void Component::resized()
{
    context = presentation::forEditor (getWidth(), getHeight());
    const bool compact = getWidth() < 450;
    for (auto* button : { &one, &two, &reveal, &end, &approve }) button->setPresentationContext (context);
    title.setFont (labelFont (context, typography::TextRole::sectionTitle, typography::Composition::information));
    status.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    detail.setFont (labelFont (context, typography::TextRole::status, typography::Composition::information));
    cause.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    recovery.setFont (labelFont (context, typography::TextRole::body, typography::Composition::information));
    auto area = getLocalBounds().reduced (compact ? 12 : 24);
    title.setBounds (area.removeFromTop (compact ? 24 : 40));
    status.setBounds (area.removeFromTop (compact ? 30 : 48));
    detail.setBounds (area.removeFromBottom (compact ? 34 : 46));
    auto actions = area.removeFromBottom (compact ? 28 : 40);
    end.setBounds (actions.removeFromRight (actions.getWidth() / 2).reduced (3, 0));
    reveal.setBounds (actions.reduced (3, 0));
    area.reduce (0, compact ? 5 : 12);
    approve.setBounds (area);
    auto guidance = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 90));
    cause.setBounds (guidance.removeFromTop (guidance.getHeight() / 2));
    recovery.setBounds (guidance);
    one.setBounds (area.removeFromLeft (area.getWidth() / 2).reduced (3, 0));
    two.setBounds (area.reduced (3, 0));
}

void Component::paint (juce::Graphics& g)
{
    g.fillAll (BG);
    surface_material::paintInstrumentFrame (g, getLocalBounds().toFloat(), false);
}
}
