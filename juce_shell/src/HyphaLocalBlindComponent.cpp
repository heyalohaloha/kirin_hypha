#include "HyphaLocalBlindComponent.h"

#include "HyphaSurfaceMaterial.h"
#include "HyphaLocalBlindFailureText.h"

#include <array>
#include <cmath>

namespace hypha::local_blind_ui
{
namespace
{
using Phase = local_blind::ProductSessionPhase;
using Failure = local_blind::ProductSessionFailure;
using Answer = local_blind::TrialAnswer;

juce::String timeline (std::int64_t sample, std::uint32_t sampleRate)
{
    if (sampleRate == 0)
        return {};
    const auto signedMilliseconds = static_cast<std::int64_t> (
        std::llround (static_cast<double> (sample) * 1000.0
                      / static_cast<double> (sampleRate)));
    const auto milliseconds = std::abs (signedMilliseconds);
    const auto minutes = milliseconds / 60'000;
    const auto seconds = (milliseconds / 1'000) % 60;
    const auto millis = milliseconds % 1'000;
    return juce::String (sample < 0 ? "-" : "")
        + juce::String (minutes).paddedLeft ('0', 2) + ":"
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


juce::String answerText (Answer answer)
{
    if (answer == Answer::one) return "ANSWER: PREFER 1";
    if (answer == Answer::two) return "ANSWER: PREFER 2";
    if (answer == Answer::noPreference) return "ANSWER: NO PREFERENCE";
    if (answer == Answer::cannotDistinguish) return "ANSWER: CANNOT TELL";
    return {};
}

bool trackStemPolicy (const local_blind::ProductSessionView& state) noexcept
{
    return state.gainPolicy == local_blind::GainMatchPolicy::exactTrackEventEnergyV1;
}

juce::String contextTag (bool trackStem)
{
    return trackStem ? "TRACK / STEM" : "2MIX";
}
}

Component::Component()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    setComponentID ("local-blind-screen");

    const auto configureLabel = [this] (juce::Label& label, const juce::String& id,
                                         typography::TextRole role, juce::Colour colour)
    {
        label.setComponentID (id);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (labelFont (presentationContext, role,
                                  typography::Composition::information));
        label.setColour (juce::Label::textColourId, colour);
        addAndMakeVisible (label);
    };
    configureLabel (titleLabel, "local-blind-title", typography::TextRole::sectionTitle,
                    COL_NORMAL);
    configureLabel (statusLabel, "local-blind-status", typography::TextRole::status,
                    COL_FLORA_BR);
    configureLabel (detailLabel, "local-blind-detail", typography::TextRole::body,
                    COL_MUTED.brighter (0.25f));
    configureLabel (resultLabel, "local-blind-result", typography::TextRole::secondaryValue,
                    COL_NORMAL);
    detailLabel.setMinimumHorizontalScale (1.0f);
    resultLabel.setMinimumHorizontalScale (1.0f);

    styleButton (sourceOne, "local-blind-source-1", "Listen to hidden source 1");
    styleButton (sourceTwo, "local-blind-source-2", "Listen to hidden source 2");
    styleButton (answerOne, "local-blind-answer-1", "Choose source 1");
    styleButton (answerTwo, "local-blind-answer-2", "Choose source 2");
    styleButton (noPreference, "local-blind-answer-neither", "Choose no preference");
    styleButton (cannotDistinguish, "local-blind-answer-same", "Choose cannot tell apart");
    styleButton (startButton, "local-blind-start", "Start the prepared comparison");
    styleButton (revealButton, "local-blind-reveal", "Reveal the hidden source assignment");
    styleButton (captureButton, "local-blind-capture", "Capture one exact four second range");
    contextChoice.setComponentID ("local-blind-context");
    contextChoice.setTitle ("Gain Match mode for this comparison");
    contextChoice.setTooltip ("2MIX: continuous sections. TRACK / STEM: short or sparse events. Applies to this comparison.");
    contextChoice.setDescription (contextChoice.getTooltip());
    contextChoice.setColour (juce::ComboBox::textColourId, COL_NORMAL);
    contextChoice.setColour (juce::ComboBox::arrowColourId, COL_FLORA);
    contextChoice.setLookAndFeel (&contextLookAndFeel);
    contextChoice.addItem ("2MIX", 1);
    contextChoice.addItem ("TRACK / STEM", 2);
    contextChoice.setSelectedId (1, juce::dontSendNotification);
    addChildComponent (contextChoice);
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
    captureButton.onClick = [this] { if (onCapture) onCapture(); };
    contextChoice.onChange = [this]
    {
        if (canChooseContext())
            setMeterContext (contextChoice.getSelectedId() == 2
                ? meter_context::MeterContext::trackStem : meter_context::MeterContext::twoMix);
        else contextChoice.setSelectedId (preflightContext == meter_context::MeterContext::trackStem ? 2 : 1,
                                          juce::dontSendNotification);
    };
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
    if (next.phase != current.phase)
        actionNotice.clear();
    current = next;
    refreshPresentation();
    resized();
    repaint();
}

void Component::setActionNotice (juce::String next)
{
    if (actionNotice == next) return;
    actionNotice = std::move (next);
    refreshPresentation();
    repaint();
}

void Component::clearActionNotice()
{
    setActionNotice ({});
}

void Component::setMeterContext (meter_context::MeterContext next)
{
    if (preflightContext == next) return;
    preflightContext = next;
    contextChoice.setSelectedId (next == meter_context::MeterContext::trackStem ? 2 : 1,
                                 juce::dontSendNotification);
    actionNotice.clear();
    if (canChooseContext())
    {
        refreshPresentation();
        resized();
        repaint();
    }
}

bool Component::canChooseContext() const noexcept
{ return current.phase == Phase::idle || current.phase == Phase::failed; }

void Component::refreshPresentation()
{
    const auto phase = current.phase;
    const bool hidden = phase == Phase::armed || phase == Phase::listening;
    const bool trackStem = canChooseContext()
        ? preflightContext == meter_context::MeterContext::trackStem
        : trackStemPolicy (current);
    const auto tag = contextTag (trackStem);
    titleLabel.setText (juce::String (hidden ? "BLIND COMPARE"
                                            : phase == Phase::revealed ? "BLIND RESULT"
                                                                      : "PRE / POST BLIND")
                           + (canChooseContext() ? juce::String() : " / " + tag),
                        juce::dontSendNotification);
    juce::String status;
    juce::String detail;
    juce::String result;
    if (phase == Phase::idle)
    {
        status = "READY TO CAPTURE";
        detail = "Play the section you want to compare, then capture 4 seconds.";
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
        detail = "Start Blind, then play from before " + rangeText (current) + ".";
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
            startButton.setTitle (
                "Start the prepared comparison. PRE is matched to POST with fixed gain; DAW Solo and routing stay unchanged");
            startButton.setDescription (startButton.getTitle());
            startButton.setTooltip (startButton.getTitle());
        }
    }
    else if (phase == Phase::armed)
    {
        status = "WAITING FOR CAPTURED RANGE START";
        detail = "Play from before " + rangeText (current) + ". The full range will be heard.";
    }
    else if (phase == Phase::listening)
    {
        status = current.trial.passComplete
            ? "PASS COMPLETE / SOURCE " + juce::String (current.trial.activeStimulus)
            : current.trial.pendingStimulus != 0
            ? "SWITCHING TO SOURCE " + juce::String (current.trial.pendingStimulus)
            : current.trial.activeStimulus != 0
                ? "LISTENING TO SOURCE " + juce::String (current.trial.activeStimulus)
                : "WAITING FOR AUDIBLE PLAYBACK";
        detail = current.trial.canAnswer
            ? "Both complete passes were heard. Choose your answer, then reveal."
            : current.trial.passComplete
                ? "Select the other source, then play from before "
                    + timeline (current.start, current.sampleRate) + "."
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
            : failureText (current, preflightContext);
        result = "Press RETURN TO LIVE, then resume playback.";
    }
    else if (phase == Phase::returned)
    {
        status = "LIVE SIGNAL RESTORED";
        detail = "The comparison is closed. Live output is restored.";
    }
    else
    {
        status = current.failure == Failure::preparation
            && current.preparationFailure == local_blind::PreparationFailure::gainUnavailable
                ? "GAIN MATCH UNAVAILABLE" : "COMPARISON NOT PREPARED";
        detail = failureText (current, preflightContext);
    }
    if (actionNotice.isNotEmpty())
        result = actionNotice;
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
    captureButton.setVisible (canChooseContext());
    captureButton.setEnabled (phase == Phase::idle || current.canRecapture);
    captureButton.setButtonText (phase == Phase::failed ? "CAPTURE AGAIN" : "CAPTURE 4 S");
    contextChoice.setVisible (canChooseContext());
    contextChoice.setAccessible (canChooseContext());
    revealButton.setVisible (phase == Phase::listening);
    revealButton.setEnabled (current.trial.canAnswer && current.trial.answer != Answer::none);
    stopButton.setVisible (phase == Phase::capturing || phase == Phase::preparing
                           || phase == Phase::ready || phase == Phase::armed
                           || phase == Phase::listening || phase == Phase::revealed);
    stopButton.setButtonText (phase == Phase::capturing || phase == Phase::preparing
                                ? "CANCEL" : phase == Phase::revealed ? "END" : "STOP");
    returnButton.setVisible (phase == Phase::returnPending);
    closeButton.setVisible (phase == Phase::idle || phase == Phase::returned
                            || phase == Phase::failed);
    closeButton.setButtonText (phase == Phase::idle ? "BACK" : "CLOSE");
}

void Component::paint (juce::Graphics& g)
{
    g.fillAll (BG);
    const auto area = getLocalBounds().toFloat().reduced (10.0f);
    surface_material::paintPanel (g, area, 0.94f, 7.0f);
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
    const int gap = presentation::densityIndex (presentationContext.density) <= 1 ? 4 : 6;
    const auto font = monoFont (presentationContext, typography::TextRole::action);
    juce::Array<int> minimumWidths;
    int totalMinimum = 0;
    for (const auto* button : visible)
    {
        const auto minimum = juce::roundToInt (std::ceil (font.getStringWidthFloat (button->getButtonText()))) + 12;
        minimumWidths.add (minimum);
        totalMinimum += minimum;
    }
    const auto spare = juce::jmax (0, area.getWidth() - gap * (visible.size() - 1) - totalMinimum);
    for (int index = 0; index < visible.size(); ++index)
    {
        visible[index]->setBounds (area.removeFromLeft (
            index + 1 == visible.size() ? area.getWidth() : minimumWidths[index] + spare / visible.size()));
        if (index + 1 != visible.size()) area.removeFromLeft (gap);
    }
}

void Component::resized()
{
    const bool compact = presentation::densityIndex (presentationContext.density) <= 1;
    const bool medium = ! compact
                     && presentationContext.density != observatory::Density::inspection;
    const auto margin = compact ? 8 : medium ? 15 : juce::jmax (18, getWidth() / 24);
    const auto titleHeight = compact ? 20 : medium ? 30 : 42;
    const auto statusHeight = compact ? 24 : medium ? 34 : 48;
    const auto detailHeight = compact ? 28 : medium ? 42 : 58;
    const auto resultHeight = compact ? 18 : medium ? 24 : 34;
    const auto sourceHeight = compact ? 26 : medium ? 34 : 48;
    const auto answerHeight = compact ? 26 : medium ? 32 : 42;
    const auto actionHeight = compact ? 28 : medium ? 36 : 48;
    const auto gap = compact ? 2 : medium ? 4 : 6;
    noPreference.setButtonText (compact ? "NO PREF" : "NO PREFERENCE");
    cannotDistinguish.setButtonText (compact ? "CAN'T TELL" : "CANNOT TELL");
    titleLabel.setFont (labelFont (presentationContext, typography::TextRole::sectionTitle,
                                   typography::Composition::information));
    statusLabel.setFont (labelFont (presentationContext, typography::TextRole::status,
                                    typography::Composition::information));
    detailLabel.setFont (labelFont (presentationContext, typography::TextRole::body,
                                    typography::Composition::information));
    resultLabel.setFont (labelFont (presentationContext, typography::TextRole::secondaryValue,
                                    typography::Composition::information));

    if (canChooseContext()) { layoutPreflight(); return; }
    titleLabel.setJustificationType (juce::Justification::centred);

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
               { &captureButton, &startButton, &revealButton, &stopButton,
                 &returnButton, &closeButton });
}

void Component::layoutPreflight()
{
    const bool compact = getWidth() < 450;
    const int margin = compact ? 10 : juce::jmax (18, getWidth() / 24);
    auto area = getLocalBounds().reduced (margin);
    auto header = area.removeFromTop (compact ? 22 : 34);
    const auto font = contextLookAndFeel.getComboBoxFont (contextChoice);
    const int choiceWidth = juce::roundToInt (std::ceil (font.getStringWidthFloat ("TRACK / STEM"))) + 38;
    if (compact)
    {
        titleLabel.setJustificationType (juce::Justification::centred);
        titleLabel.setBounds (header);
        area.removeFromTop (4);
        contextChoice.setBounds (area.removeFromTop (24).withSizeKeepingCentre (choiceWidth, 24));
    }
    else
    {
        contextChoice.setBounds (header.removeFromRight (choiceWidth));
        header.removeFromRight (12);
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        titleLabel.setBounds (header);
    }
    auto actions = area.removeFromBottom (compact ? 28 : 44);
    const int backWidth = compact ? 58 : 92;
    closeButton.setBounds (actions.removeFromRight (backWidth));
    actions.removeFromRight (8);
    captureButton.setBounds (actions.withSizeKeepingCentre (juce::jmin (actions.getWidth(), compact ? 180 : 280), actions.getHeight()));
    area.removeFromTop (compact ? 5 : juce::jmax (12, area.getHeight() / 4));
    statusLabel.setBounds (area.removeFromTop (compact ? 24 : 42));
    detailLabel.setBounds (area.removeFromTop (compact ? 36 : 58));
    resultLabel.setBounds (area.removeFromTop (compact ? 24 : 34));
}
}
