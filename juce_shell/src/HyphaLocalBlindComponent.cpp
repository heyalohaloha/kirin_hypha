#include "HyphaLocalBlindComponent.h"

#include <array>
#include <cmath>

namespace hypha::local_blind_ui
{
namespace
{
using Phase = local_blind::ProductSessionPhase;
using Failure = local_blind::ProductSessionFailure;
using Answer = local_blind::TrialAnswer;

bool trialActive (Phase phase) noexcept
{
    return phase == Phase::armed || phase == Phase::listening
        || phase == Phase::revealed || phase == Phase::returnPending;
}

juce::String timeline (std::int64_t sample, std::uint32_t sampleRate)
{
    if (sample < 0 || sampleRate == 0)
        return {};
    const auto milliseconds = static_cast<std::int64_t> (
        std::llround (static_cast<double> (sample) * 1000.0
                      / static_cast<double> (sampleRate)));
    const auto minutes = milliseconds / 60'000;
    const auto seconds = (milliseconds / 1'000) % 60;
    const auto millis = milliseconds % 1'000;
    return juce::String (minutes).paddedLeft ('0', 2) + ":"
        + juce::String (seconds).paddedLeft ('0', 2) + "."
        + juce::String (millis).paddedLeft ('0', 3);
}

juce::String rangeText (const local_blind::ProductSessionView& state)
{
    if (state.sampleRate == 0 || state.frames <= 0)
        return {};
    return timeline (state.start, state.sampleRate) + " - "
        + timeline (state.start + state.frames, state.sampleRate);
}

juce::String failureText (const local_blind::ProductSessionView& state)
{
    if (state.failure == Failure::pairChanged)
        return "The selected pair changed. Return to the live signal, then capture again.";
    if (state.failure == Failure::captureRequest)
        return "The exact range could not be scheduled. Keep playback running and try again.";
    if (state.failure == Failure::captureResult)
        return "The exact PRE and POST range was not completed.";
    if (state.failure == Failure::preparation)
        return "This range did not contain enough matched audio for a fair comparison.";
    if (state.failure == Failure::publication)
        return "The prepared comparison could not be made available.";
    switch (state.trial.failure)
    {
        case local_blind::TrialFailure::format:
            return "The channel layout or sample rate changed.";
        case local_blind::TrialFailure::transport:
            return "Playback stopped, bypassed, or entered offline render.";
        case local_blind::TrialFailure::epochs:
            return "The comparison ownership changed.";
        case local_blind::TrialFailure::discontinuity:
            return "Playback jumped outside the exact captured sequence.";
        case local_blind::TrialFailure::range:
            return "Playback did not begin at the captured range start.";
        case local_blind::TrialFailure::none:
            break;
    }
    return "The comparison stopped before it could be completed.";
}

juce::String answerText (Answer answer)
{
    if (answer == Answer::one) return "ANSWER: PREFER 1";
    if (answer == Answer::two) return "ANSWER: PREFER 2";
    if (answer == Answer::noPreference) return "ANSWER: NO PREFERENCE";
    if (answer == Answer::cannotDistinguish) return "ANSWER: CANNOT TELL";
    return {};
}
}

bool blocksDisclosure (const local_blind::ProductSessionView& state) noexcept
{
    return state.phase == Phase::capturing || state.phase == Phase::preparing
        || state.phase == Phase::ready || trialActive (state.phase);
}

bool needsRecoveryScreen (const local_blind::ProductSessionView& state) noexcept
{
    return blocksDisclosure (state);
}

Component::Component()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    setComponentID ("local-blind-screen");

    const auto configureLabel = [this] (juce::Label& label, const juce::String& id,
                                         float height, juce::Colour colour)
    {
        label.setComponentID (id);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (labelFont (height));
        label.setColour (juce::Label::textColourId, colour);
        addAndMakeVisible (label);
    };
    configureLabel (titleLabel, "local-blind-title", 18.0f, COL_NORMAL);
    configureLabel (statusLabel, "local-blind-status", 24.0f, COL_FLORA_BR);
    configureLabel (detailLabel, "local-blind-detail", 12.0f, COL_MUTED.brighter (0.25f));
    configureLabel (resultLabel, "local-blind-result", 13.0f, COL_NORMAL);
    detailLabel.setMinimumHorizontalScale (0.72f);
    resultLabel.setMinimumHorizontalScale (0.72f);

    styleButton (sourceOne, "local-blind-source-1", "Listen to hidden source 1");
    styleButton (sourceTwo, "local-blind-source-2", "Listen to hidden source 2");
    styleButton (answerOne, "local-blind-answer-1", "Choose source 1");
    styleButton (answerTwo, "local-blind-answer-2", "Choose source 2");
    styleButton (noPreference, "local-blind-answer-neither", "Choose no preference");
    styleButton (cannotDistinguish, "local-blind-answer-same", "Choose cannot tell apart");
    styleButton (startButton, "local-blind-start", "Start the prepared comparison");
    styleButton (revealButton, "local-blind-reveal", "Reveal the hidden source assignment");
    styleButton (stopButton, "local-blind-stop", "Stop the comparison safely");
    styleButton (returnButton, "local-blind-return", "Return explicitly to the live signal");
    styleButton (closeButton, "local-blind-close", "Close Blind Compare");

    sourceOne.onClick = [this] { if (onSelectStimulus) onSelectStimulus (1); };
    sourceTwo.onClick = [this] { if (onSelectStimulus) onSelectStimulus (2); };
    answerOne.onClick = [this] { if (onAnswer) onAnswer (Answer::one); };
    answerTwo.onClick = [this] { if (onAnswer) onAnswer (Answer::two); };
    noPreference.onClick = [this] { if (onAnswer) onAnswer (Answer::noPreference); };
    cannotDistinguish.onClick = [this] { if (onAnswer) onAnswer (Answer::cannotDistinguish); };
    startButton.onClick = [this]
    { if (onStart) onStart (current.trial.lowerPostApprovalRequired); };
    revealButton.onClick = [this] { if (onReveal) onReveal(); };
    stopButton.onClick = [this] { if (onStop) onStop(); };
    returnButton.onClick = [this] { if (onReturn) onReturn(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    refreshPresentation();
}

void Component::styleButton (juce::Button& button, const juce::String& id,
                             const juce::String& title)
{
    button.setComponentID (id);
    button.setTitle (title);
    button.setDescription (title);
    button.setTooltip (title);
    button.setColour (juce::TextButton::buttonColourId, kFieldFill);
    button.setColour (juce::TextButton::textColourOnId, COL_FLORA_BR);
    button.setColour (juce::TextButton::textColourOffId, COL_NORMAL);
    addChildComponent (button);
}

void Component::setState (local_blind::ProductSessionView next)
{
    current = next;
    refreshPresentation();
    resized();
    repaint();
}

void Component::refreshPresentation()
{
    const auto phase = current.phase;
    const bool hidden = phase == Phase::armed || phase == Phase::listening;
    titleLabel.setText (hidden ? "BLIND COMPARE"
                              : phase == Phase::revealed ? "BLIND RESULT"
                                                        : "PRE / POST BLIND",
                        juce::dontSendNotification);
    juce::String status;
    juce::String detail;
    juce::String result;
    if (phase == Phase::idle)
    {
        status = "READY TO CAPTURE";
        detail = "Select one exact PRE pair and keep playback running.";
    }
    else if (phase == Phase::capturing)
    {
        status = "CAPTURING EXACT 4 SECOND RANGE";
        detail = "Keep playback running without seeking, looping, bypassing, or changing PDC.";
    }
    else if (phase == Phase::preparing)
    {
        status = "PREPARING COMPARISON";
        detail = "The live signal remains unchanged while the fixed copies are checked.";
    }
    else if (phase == Phase::ready)
    {
        status = "EXACT RANGE READY";
        detail = "Set the DAW playhead to " + rangeText (current)
               + " and play or loop this exact range.";
        if (current.trial.lowerPostApprovalRequired)
        {
            const auto attenuation = std::abs (current.lowerPostGainDb);
            startButton.setButtonText ("LOWER POST " + juce::String (attenuation, 1)
                                       + " dB & START");
            startButton.setTitle ("Approve fixed POST attenuation and start Blind Compare");
            startButton.setDescription (startButton.getTitle());
            startButton.setTooltip (startButton.getTitle());
        }
        else
        {
            startButton.setButtonText ("START BLIND");
            startButton.setTitle ("Start the prepared comparison");
            startButton.setDescription (startButton.getTitle());
            startButton.setTooltip (startButton.getTitle());
        }
    }
    else if (phase == Phase::armed)
    {
        status = "WAITING FOR CAPTURED RANGE START";
        detail = "Play from " + rangeText (current) + ". No source has been heard yet.";
    }
    else if (phase == Phase::listening)
    {
        status = current.trial.pendingStimulus != 0
            ? "SWITCHING TO SOURCE " + juce::String (current.trial.pendingStimulus)
            : current.trial.activeStimulus != 0
                ? "LISTENING TO SOURCE " + juce::String (current.trial.activeStimulus)
                : "WAITING FOR AUDIBLE PLAYBACK";
        detail = current.trial.canAnswer
            ? "Both complete passes were heard. Choose your answer, then reveal."
            : "Listen to one complete pass of Source 1 and Source 2.";
        result = answerText (current.trial.answer);
    }
    else if (phase == Phase::revealed)
    {
        status = current.trial.revealedOneSide == 1
            ? "SOURCE 1 = PRE   /   SOURCE 2 = POST"
            : "SOURCE 1 = POST   /   SOURCE 2 = PRE";
        detail = "The assignment is revealed. Stop to leave the comparison.";
        result = answerText (current.trial.answer);
    }
    else if (phase == Phase::returnPending)
    {
        status = "RETURN TO LIVE SIGNAL";
        detail = current.failure == Failure::none
                     && current.trial.failure == local_blind::TrialFailure::none
            ? "Confirm the return to the unchanged live signal."
            : failureText (current);
        result = "The comparison remains reserved until live output confirms the return.";
    }
    else if (phase == Phase::returned)
    {
        status = "LIVE SIGNAL RESTORED";
        detail = "The Audio Thread confirmed normal output and released the comparison.";
    }
    else
    {
        status = "COMPARISON NOT PREPARED";
        detail = failureText (current);
    }
    statusLabel.setText (status, juce::dontSendNotification);
    detailLabel.setText (detail, juce::dontSendNotification);
    resultLabel.setText (result, juce::dontSendNotification);

    const bool selectable = (phase == Phase::armed || phase == Phase::listening)
        && current.trial.failure == local_blind::TrialFailure::none;
    for (auto* button : { &sourceOne, &sourceTwo })
    {
        button->setVisible (phase == Phase::armed || phase == Phase::listening
                            || phase == Phase::revealed);
        button->setEnabled (selectable);
    }
    sourceOne.setToggleState (current.trial.activeStimulus == 1, juce::dontSendNotification);
    sourceTwo.setToggleState (current.trial.activeStimulus == 2, juce::dontSendNotification);

    const bool answersVisible = phase == Phase::listening;
    for (auto* button : { &answerOne, &answerTwo, &noPreference, &cannotDistinguish })
    {
        button->setVisible (answersVisible);
        button->setEnabled (answersVisible && current.trial.canAnswer);
    }
    answerOne.setToggleState (current.trial.answer == Answer::one, juce::dontSendNotification);
    answerTwo.setToggleState (current.trial.answer == Answer::two, juce::dontSendNotification);
    noPreference.setToggleState (current.trial.answer == Answer::noPreference,
                                 juce::dontSendNotification);
    cannotDistinguish.setToggleState (current.trial.answer == Answer::cannotDistinguish,
                                      juce::dontSendNotification);

    startButton.setVisible (phase == Phase::ready);
    revealButton.setVisible (phase == Phase::listening);
    revealButton.setEnabled (current.trial.canAnswer && current.trial.answer != Answer::none);
    stopButton.setVisible (phase == Phase::capturing || phase == Phase::preparing
                           || phase == Phase::ready || phase == Phase::armed
                           || phase == Phase::listening || phase == Phase::revealed);
    stopButton.setButtonText (phase == Phase::capturing || phase == Phase::preparing
                                ? "CANCEL" : phase == Phase::revealed ? "END" : "STOP");
    returnButton.setVisible (phase == Phase::returnPending);
    closeButton.setVisible (phase == Phase::returned || phase == Phase::failed);
}

void Component::paint (juce::Graphics& g)
{
    g.fillAll (BG);
    const auto area = getLocalBounds().toFloat().reduced (10.0f);
    g.setColour (kFieldFill.withAlpha (0.94f));
    g.fillRoundedRectangle (area, 7.0f);
    g.setColour (COL_FLORA.withAlpha (0.52f));
    g.drawRoundedRectangle (area.reduced (0.5f), 7.0f, 1.0f);
    g.setColour (COL_LED_BLUE.withAlpha (0.34f));
    g.fillEllipse (area.getX() + 18.0f, area.getY() + 18.0f, 7.0f, 7.0f);
}

void Component::layoutRow (juce::Rectangle<int> area,
                           std::initializer_list<juce::Button*> buttons)
{
    juce::Array<juce::Button*> visible;
    for (auto* button : buttons)
        if (button->isVisible()) visible.add (button);
    if (visible.isEmpty()) return;
    constexpr int gap = 6;
    const auto width = (area.getWidth() - gap * (visible.size() - 1)) / visible.size();
    for (int index = 0; index < visible.size(); ++index)
    {
        visible[index]->setBounds (area.removeFromLeft (
            index + 1 == visible.size() ? area.getWidth() : width));
        if (index + 1 != visible.size()) area.removeFromLeft (gap);
    }
}

void Component::resized()
{
    const bool compact = getHeight() < 300;
    const bool medium = ! compact && getHeight() < 480;
    const auto margin = compact ? 8 : medium ? 15 : juce::jmax (18, getWidth() / 24);
    const auto titleHeight = compact ? 20 : medium ? 30 : 42;
    const auto statusHeight = compact ? 24 : medium ? 34 : 48;
    const auto detailHeight = compact ? 28 : medium ? 42 : 58;
    const auto resultHeight = compact ? 18 : medium ? 24 : 34;
    const auto sourceHeight = compact ? 26 : medium ? 34 : 48;
    const auto answerHeight = compact ? 26 : medium ? 32 : 42;
    const auto actionHeight = compact ? 28 : medium ? 36 : 48;
    const auto gap = compact ? 2 : medium ? 4 : 6;
    titleLabel.setFont (labelFont (compact ? 12.5f : medium ? 15.0f : 18.0f));
    statusLabel.setFont (labelFont (compact ? 13.0f : medium ? 18.0f : 24.0f));
    detailLabel.setFont (labelFont (compact ? 9.0f : medium ? 10.5f : 12.0f));
    resultLabel.setFont (labelFont (compact ? 9.0f : medium ? 11.0f : 13.0f));

    auto area = getLocalBounds().reduced (margin);
    titleLabel.setBounds (area.removeFromTop (titleHeight));
    area.removeFromTop (gap);
    statusLabel.setBounds (area.removeFromTop (statusHeight));
    detailLabel.setBounds (area.removeFromTop (detailHeight));
    resultLabel.setBounds (area.removeFromTop (resultHeight));
    area.removeFromTop (gap);

    layoutRow (area.removeFromTop (sourceHeight), { &sourceOne, &sourceTwo });
    area.removeFromTop (gap);
    layoutRow (area.removeFromTop (answerHeight),
               { &answerOne, &answerTwo, &noPreference, &cannotDistinguish });
    area.removeFromTop (compact ? 3 : medium ? 6 : 10);
    layoutRow (area.removeFromTop (actionHeight),
               { &startButton, &revealButton, &stopButton, &returnButton, &closeButton });
}
}
