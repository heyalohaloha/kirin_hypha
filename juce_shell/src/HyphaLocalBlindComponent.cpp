#include "HyphaLocalBlindComponent.h"

#include "HyphaSurfaceMaterial.h"
#include "HyphaLocalBlindPresentationState.h"

#include <array>
#include <cmath>

namespace hypha::local_blind_ui
{
namespace
{
using Phase = local_blind::ProductSessionPhase;
using Answer = local_blind::TrialAnswer;
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
    styleButton (answerOne, "local-blind-answer-1", "Choose source 1 and reveal");
    styleButton (answerTwo, "local-blind-answer-2", "Choose source 2 and reveal");
    styleButton (noPreference, "local-blind-answer-neither", "Choose no preference and reveal");
    styleButton (cannotDistinguish, "local-blind-answer-same", "Choose cannot tell apart and reveal");
    styleButton (startButton, "local-blind-start", "Start the prepared comparison");
    styleButton (revealButton, "local-blind-reveal", "Reveal the hidden source assignment");
    styleButton (captureButton, "local-blind-capture", "Capture one exact four second range");
    styleButton (repairButton, "local-blind-repair", "Resolve the capture requirement");
    repairButton.onClick = [this] { if (onRepair) onRepair(); };
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
    if (samePresentation (current, next)) return;
    if (next.phase != current.phase)
        actionNotice.clear();
    current = next;
    refreshPresentation();
    resized();
    repaint();
}

void Component::setAdmission (local_blind::CaptureAdmission next)
{
    if (admission == next) return;
    admission = next;
    actionNotice.clear();
    refreshPresentation();
    resized();
    repaint();
}

void Component::setPairName (juce::String next)
{
    if (preflightPair == next) return;
    preflightPair = std::move (next);
    refreshPresentation();
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
    auto actions = area.removeFromTop (actionHeight);
    // End/Stop stays in the same right-hand slot when answers become available.
    const auto stopArea = actions.removeFromRight (compact ? 66 : medium ? 100 : 140);
    if (stopButton.isVisible()) stopButton.setBounds (stopArea);
    actions.removeFromRight (gap);
    layoutRow (actions, { &captureButton, &startButton, &revealButton,
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
    repairButton.setBounds (actions.withSizeKeepingCentre (juce::jmin (actions.getWidth(), compact ? 180 : 280), actions.getHeight()));
    captureButton.setBounds (actions.withSizeKeepingCentre (juce::jmin (actions.getWidth(), compact ? 180 : 280), actions.getHeight()));
    area.removeFromTop (compact ? 5 : juce::jmax (12, area.getHeight() / 4));
    statusLabel.setBounds (area.removeFromTop (compact ? 24 : 42));
    detailLabel.setBounds (area.removeFromTop (compact ? 36 : 58));
    resultLabel.setBounds (area.removeFromTop (compact ? 24 : 34));
}
}
