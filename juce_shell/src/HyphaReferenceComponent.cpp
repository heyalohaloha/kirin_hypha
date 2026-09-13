#include "HyphaReferenceComponent.h"

#include "HyphaReferenceSelectorLookAndFeel.h"
#include "HyphaReferenceMetricPainter.h"
#include "HyphaReferenceVisuals.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <utility>

namespace hypha::reference_ui
{
namespace
{
void configureSelector (juce::ComboBox& box, const juce::String& componentId,
                        const juce::String& title, const juce::String& tooltip)
{
    box.setComponentID (componentId);
    box.setTitle (title);
    box.setDescription (tooltip);
    box.setTooltip (tooltip);
    box.setWantsKeyboardFocus (true);
    box.setColour (juce::ComboBox::backgroundColourId, kFieldFill.withAlpha (0.94f));
    box.setColour (juce::ComboBox::outlineColourId, COL_MUTED.withAlpha (0.46f));
    box.setColour (juce::ComboBox::textColourId, COL_NORMAL);
    box.setColour (juce::ComboBox::arrowColourId, COL_FLORA.withAlpha (0.84f));
}

}

Component::Component()
{
    setOpaque (false);
    connectionStatus.setComponentID ("reference-connection");
    connectionStatus.setText ("OS", juce::dontSendNotification);
    connectionStatus.setJustificationType (juce::Justification::centred);
    connectionStatus.setFont (juce::Font (9.0f));
    addAndMakeVisible (connectionStatus);
    presetBox.setLookAndFeel (&selectorLookAndFeel);
    checkBox.setLookAndFeel (&selectorLookAndFeel);
    candidateBox.setLookAndFeel (&selectorLookAndFeel);
    cueBox.setLookAndFeel (&selectorLookAndFeel);
    configureSelector (presetBox, "reference-preset", "Check Preset",
                       "Temporarily call a Check Preset received from Kirin OS.");
    configureSelector (checkBox, "reference-check", "Check",
                       "Temporarily call a Check received from Kirin OS.");
    configureSelector (candidateBox, "reference-candidate", "B Source",
                       "Replace B with a prepared past Version or Reference for this audition.");
    configureSelector (cueBox, "reference-cue", "Cue",
                       "Choose a prepared listening position for this audition.");
    aButton.setComponentID ("reference-a");
    bButton.setComponentID ("reference-b");
    blindButton.setComponentID ("reference-blind");
    oneButton.setComponentID ("reference-blind-1");
    twoButton.setComponentID ("reference-blind-2");
    answerButton.setComponentID ("reference-blind-answer");
    revealButton.setComponentID ("reference-blind-reveal");
    endBlindButton.setComponentID ("reference-blind-end");
    actionButton.setComponentID ("reference-action");
    aButton.setTitle ("Audition A");
    bButton.setTitle ("Audition B");
    blindButton.setTitle ("Start Version Blind");
    oneButton.setTitle ("Audition blind source 1");
    twoButton.setTitle ("Audition blind source 2");
    answerButton.setTitle ("Choose the audible blind source as your answer");
    revealButton.setTitle ("Reveal blind sources");
    endBlindButton.setTitle ("End Blind Compare");
    aButton.setTooltip ("Return to the live DAW mix (A).");
    bButton.setTooltip ("Audition the Kirin OS prepared Reference (B).");
    blindButton.setTooltip (
        "Start a separate Version Blind trial. Check Preset settings and facts are hidden.");
    oneButton.setTooltip ("Audition source 1. Its identity remains hidden.");
    twoButton.setTooltip ("Audition source 2. Its identity remains hidden.");
    answerButton.setTooltip ("Record the source you are hearing as your answer before reveal.");
    revealButton.setTooltip ("Reveal which source is A and which source is B.");
    endBlindButton.setTooltip ("End Blind Compare and return to live A.");
    actionButton.setTooltip ("Continue with the safe next action.");
    presetBox.onChange = [this]
    {
        const auto id = selectedOptionId (presetBox, current.presets);
        if (id.isNotEmpty() && id != current.presetId && onSelectPreset) onSelectPreset (id);
    };
    checkBox.onChange = [this]
    {
        const auto id = selectedOptionId (checkBox, current.checks);
        if (id.isNotEmpty() && id != current.checkId && onSelectCheck) onSelectCheck (id);
    };
    candidateBox.onChange = [this]
    {
        const auto id = selectedOptionId (candidateBox, current.candidates);
        if (id.isNotEmpty() && id != current.candidateId && onSelectCandidate) onSelectCandidate (id);
    };
    cueBox.onChange = [this]
    {
        const auto id = selectedOptionId (cueBox, current.cues);
        if (id.isNotEmpty() && id != current.cueId && onSelectCue) onSelectCue (id);
    };
    aButton.onClick = [this] { if (onSelectA) onSelectA(); };
    bButton.onClick = [this] { if (onSelectB) onSelectB(); };
    blindButton.onClick = [this] { if (onStartBlind) onStartBlind(); };
    oneButton.onClick = [this] { if (onSelectBlindStimulus) onSelectBlindStimulus (1); };
    twoButton.onClick = [this] { if (onSelectBlindStimulus) onSelectBlindStimulus (2); };
    answerButton.onClick = [this]
    {
        if (current.activeBlindStimulus != 0 && onAnswerBlind)
            onAnswerBlind (current.activeBlindStimulus);
    };
    revealButton.onClick = [this] { if (onRevealBlind) onRevealBlind(); };
    endBlindButton.onClick = [this] { if (onEndBlind) onEndBlind(); };
    actionButton.onClick = [this] { if (onAction) onAction(); };
    addAndMakeVisible (presetBox);
    addAndMakeVisible (checkBox);
    addAndMakeVisible (candidateBox);
    addAndMakeVisible (cueBox);
    addAndMakeVisible (aButton);
    addAndMakeVisible (bButton);
    addChildComponent (blindButton);
    addChildComponent (oneButton);
    addChildComponent (twoButton);
    addChildComponent (answerButton);
    addChildComponent (revealButton);
    addChildComponent (endBlindButton);
    addChildComponent (actionButton);
}

void Component::setState (State next)
{
    current = std::move (next);
    connectionStatus.setTooltip (current.osOnline ? "Kirin OS connected"
        : current.libraryReceived ? "Kirin OS offline / received presets available" : "Waiting for Kirin OS");
    connectionStatus.setTitle (connectionStatus.getTooltip());
    connectionStatus.setColour (juce::Label::textColourId, current.osOnline ? COL_FLORA : COL_MUTED);
    const bool blindSession = isBlindSession (current.blindPhase);
    connectionStatus.setVisible (! blindSession);
    const bool blindAudition = isBlindAudition (current.blindPhase);
    aButton.setToggleState (! current.bSelected, juce::dontSendNotification);
    bButton.setToggleState (current.bSelected, juce::dontSendNotification);
    bButton.setEnabled (canSelectB (current));
    aButton.setVisible (! blindSession);
    bButton.setVisible (! blindSession);
    blindButton.setVisible (! blindSession && canStartBlind (current));
    blindButton.setButtonText (current.blindLargeScreen ? "VERSION BLIND" : "BLIND 300%");
    blindButton.setTitle (current.blindLargeScreen ? "Start Version Blind" : "Open Blind at 300%");
    oneButton.setVisible (blindAudition);
    twoButton.setVisible (blindAudition);
    const bool bothHeard = current.blindStimulusOneHeard && current.blindStimulusTwoHeard;
    answerButton.setVisible (current.blindPhase == BlindPhase::active
                             && bothHeard && current.activeBlindStimulus != 0);
    answerButton.setButtonText (current.answeredBlindStimulus == current.activeBlindStimulus
        ? "CHOSEN " + juce::String (current.activeBlindStimulus)
        : "CHOOSE " + juce::String (current.activeBlindStimulus));
    revealButton.setVisible (current.blindPhase == BlindPhase::active
                             && current.answeredBlindStimulus != 0);
    const bool heldA = current.blindPhase == BlindPhase::invalidated
                    && current.blindRequiredAAttenuationDb > 0.0;
    endBlindButton.setButtonText (heldA
        ? "RETURN A +" + juce::String (current.blindRequiredAAttenuationDb, 1) + " dB"
        : "END");
    endBlindButton.setTooltip (heldA
        ? "Return the live A level after the interrupted Blind Compare."
        : "End Blind Compare and return to live A.");
    endBlindButton.setVisible (blindSession);
    oneButton.setToggleState (current.activeBlindStimulus == 1, juce::dontSendNotification);
    twoButton.setToggleState (current.activeBlindStimulus == 2, juce::dontSendNotification);
    oneButton.setEnabled (current.pendingBlindStimulus != 1);
    twoButton.setEnabled (current.pendingBlindStimulus != 2);
    syncSelectionControl (presetBox, current.presets, current.presetId);
    syncSelectionControl (checkBox, current.checks, current.checkId);
    syncSelectionControl (candidateBox, current.candidates, current.candidateId);
    syncSelectionControl (cueBox, current.cues, current.cueId);
    cueBox.setEnabled (cueBox.isEnabled() && ! current.candidatePreparationPending);
    const bool showDetailedSelectors = detailedLayout() && ! blindSession;
    presetBox.setVisible (! blindSession && ! current.presets.empty());
    checkBox.setVisible (! blindSession && ! current.checks.empty());
    candidateBox.setVisible (! blindSession && ! current.candidates.empty());
    cueBox.setVisible (showDetailedSelectors && ! current.cues.empty());
    actionButton.setButtonText (current.actionText);
    actionButton.setVisible (! blindSession && current.actionText.isNotEmpty());
    resized();
    repaint();
}

bool Component::detailedLayout() const noexcept
{
    return observatory::isFullDensity (presentationContext.density);
}


void Component::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (6);
    auto header = area.removeFromTop (detailedLayout() ? 42 : 34);
    const bool blindActive = current.blindPhase == BlindPhase::active;
    const bool blindStarting = current.blindPhase == BlindPhase::starting;
    const bool blindInvalidated = current.blindPhase == BlindPhase::invalidated;
    const bool blindRevealed = current.blindPhase == BlindPhase::revealed;
    const bool blindSession = isBlindSession (current.blindPhase);
    if (detailedLayout() && ! blindSession)
    {
        auto selectors = area.removeFromTop (50);
        const int gap = 5;
        const int columnWidth = (selectors.getWidth() - gap * 3) / 4;
        const auto drawSelectorLabel = [this, &g] (juce::Rectangle<int> cell,
                                             const juce::String& text)
        {
            g.setColour (COL_MUTED.withAlpha (0.82f));
            g.setFont (labelFont (presentationContext, typography::TextRole::metricLabel,
                                  typography::Composition::information));
            g.drawText (text, cell.removeFromTop (15), juce::Justification::centredLeft);
        };
        drawSelectorLabel (selectors.removeFromLeft (columnWidth), "CHECK PRESET");
        selectors.removeFromLeft (gap);
        drawSelectorLabel (selectors.removeFromLeft (columnWidth), "CHECK");
        selectors.removeFromLeft (gap);
        drawSelectorLabel (selectors.removeFromLeft (columnWidth), "B SOURCE");
        selectors.removeFromLeft (gap);
        drawSelectorLabel (selectors, "CUE");
    }
    else if (! detailedLayout() && ! blindSession
             && (presetBox.isVisible() || checkBox.isVisible() || candidateBox.isVisible()))
    {
        if (presetBox.isVisible()) area.removeFromTop (24);
        area.removeFromTop (4);
        auto selector = area.removeFromTop (24);
        g.setColour (COL_TEXT_TERTIARY.withAlpha (0.92f));
        g.setFont (labelFont (presentationContext, typography::TextRole::metricLabel,
                              typography::Composition::information));
        constexpr int gap = 5;
        if (checkBox.isVisible() && candidateBox.isVisible())
        {
            auto checkSelector = selector.removeFromLeft ((selector.getWidth() - gap) * 5 / 12);
            selector.removeFromLeft (gap);
            text_style::drawEllipsized (g, "CHECK", checkSelector.removeFromLeft (38),
                                        juce::Justification::centredLeft);
            text_style::drawEllipsized (g, "B", selector.removeFromLeft (10),
                                        juce::Justification::centredLeft);
        }
        else
        {
            text_style::drawEllipsized (g, checkBox.isVisible() ? "CHECK" : "B SOURCE",
                                        selector.removeFromLeft (checkBox.isVisible() ? 38 : 64),
                                        juce::Justification::centredLeft);
        }
    }
    int controlsWidth = (detailedLayout() ? 62 : 48) * 2 + 3;
    if (isBlindSession (current.blindPhase))
        controlsWidth = blindInvalidated ? (detailedLayout() ? 132 : 94)
                                         : (detailedLayout() ? 62 : 48);
    header.removeFromRight (controlsWidth + (blindSession ? 0 : 32));
    const auto navigationHeight = juce::roundToInt (typography::resolve (
        presentationContext, typography::TextRole::navigation,
        typography::Composition::information).lineHeight);
    if (blindStarting || blindActive || blindInvalidated)
    {
        g.setColour (COL_FLORA.withAlpha (0.86f));
        g.setFont (labelFont (presentationContext, typography::TextRole::navigation,
                              typography::Composition::information));
        text_style::draw (g, "REFERENCE / VERSION BLIND",
                          header.removeFromTop (navigationHeight), presentationContext,
                          typography::TextRole::navigation, juce::Justification::centredLeft,
                          1, typography::Composition::information);
        g.setColour (COL_OBSERVATORY_VALUE);
        g.setFont (labelFont (presentationContext, typography::TextRole::sectionTitle,
                              typography::Composition::information));
        text_style::drawEllipsized (g, "SOURCE IDENTITY HIDDEN", header,
                                    juce::Justification::centredLeft);

        area.removeFromTop (4);
        const auto footerHeight = detailedLayout() ? 24 : 18;
        if (blindActive) area.removeFromBottom (footerHeight);
        auto statusArea = area.removeFromTop (footerHeight);
        area.removeFromTop (2);
        juce::String status = blindInvalidated || blindStarting
            ? current.status : "SELECT 1 OR 2";
        if (! blindInvalidated && current.pendingBlindStimulus != 0)
            status = "SWITCHING TO " + juce::String (current.pendingBlindStimulus);
        else if (! blindInvalidated && current.activeBlindStimulus != 0)
            status = "AUDIBLE SOURCE " + juce::String (current.activeBlindStimulus)
                   + " / CONFIRMED";
        if (! blindInvalidated && current.answeredBlindStimulus != 0)
            status = "CHOSEN " + juce::String (current.answeredBlindStimulus)
                   + " / REVEAL WHEN READY";
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.92f));
        g.setFont (labelFont (presentationContext, typography::TextRole::status,
                              typography::Composition::information));
        text_style::drawEllipsized (g, status, statusArea.reduced (4, 0),
                                    juce::Justification::centredLeft);
        reference_metric_painter::paintPanel (g, area.toFloat(), 0.72f);
        reference_metric_painter::paintComparisonRoots (g, area.toFloat());
        g.setColour (COL_NORMAL.withAlpha (0.9f));
        g.setFont (monoFont (presentationContext, typography::TextRole::primaryValue,
                             typography::Composition::information));
        text_style::draw (g, blindInvalidated || blindStarting
                              ? "NO COMPARISON SHOWN" : "1      2",
                          area.toNearestInt(), presentationContext,
                          typography::TextRole::primaryValue, juce::Justification::centred,
                          1, typography::Composition::information);
        return;
    }
    g.setColour (COL_FLORA.withAlpha (0.86f));
    g.setFont (labelFont (presentationContext, typography::TextRole::navigation,
                          typography::Composition::information));
    text_style::draw (g, "REFERENCE / CHECK",
                      header.removeFromTop (navigationHeight), presentationContext,
                      typography::TextRole::navigation, juce::Justification::centredLeft,
                      1, typography::Composition::information);
    g.setColour (COL_OBSERVATORY_VALUE);
    auto title = current.checkLabel.isNotEmpty() ? current.checkLabel : juce::String { "CHECK" };
    if (current.title.isNotEmpty())
        title += "  /  B: " + current.title;
    g.setFont (displayTextFont (title, presentationContext,
                                typography::TextRole::sectionTitle,
                                typography::Composition::information));
    text_style::drawEllipsized (g, title, header, juce::Justification::centredLeft);

    area.removeFromTop (4);
    auto statusArea = area.removeFromBottom (detailedLayout() ? 24 : 18);
    const auto statusColour = current.readiness == Readiness::rejected
        ? COL_LED_YELLOW : current.bSelected ? COL_SPECTRUM_DELTA_BR : COL_MUTED;
    g.setColour (statusColour.withAlpha (0.92f));
    g.setFont (labelFont (presentationContext, typography::TextRole::status,
                          typography::Composition::information));
    if (! blindSession)
    {
        g.setColour (current.osOnline ? COL_FLORA : COL_MUTED);
        g.fillEllipse (static_cast<float> (connectionStatus.getX() - 4), 10.0f, 4.0f, 4.0f);
        g.setColour (statusColour.withAlpha (0.92f));
    }
    auto statusText = blindRevealed && current.blindReveal.isNotEmpty()
        ? "REVEALED / " + current.blindReveal : current.status;
    if (current.bSelected && ! blindRevealed)
        statusText = detailedLayout()
            ? juce::String { "B / " }
                + (current.alignmentLabel == "PROJECT TIMELINE" ? "TIMELINE" : "CUE")
                + " / PRE " + delta() + " PAUSED"
            : juce::String { "B / PRE " } + delta() + " PAUSED";
    else if (detailedLayout() && current.alignmentLabel.isNotEmpty())
        statusText += (statusText.isNotEmpty() ? "  /  " : "") + current.alignmentLabel;
    auto availableStatusArea = statusArea;
    if (blindRevealed)
        availableStatusArea.removeFromLeft ((detailedLayout() ? 62 : 48) * 2 + 6);
    if (blindButton.isVisible())
        availableStatusArea.removeFromRight (detailedLayout() ? 120 : 90);
    if (actionButton.isVisible())
        availableStatusArea.removeFromRight (detailedLayout() ? 194 : 122);
    auto primaryStatusArea = availableStatusArea;
    auto gainStatusArea = availableStatusArea;
    if (detailedLayout() && current.bSelected)
        primaryStatusArea = gainStatusArea.removeFromLeft (
            juce::roundToInt (gainStatusArea.getWidth() * 0.42f));
    text_style::drawEllipsized (g, statusText, primaryStatusArea.reduced (4, 0),
                                juce::Justification::centredLeft);

    if (detailedLayout())
    {
        if (! paintConfiguredReferenceViews (g, area.toFloat(), current, presentationContext))
        {
            auto metrics = area;
            const float gap = 6.0f;
            const float width = (metrics.getWidth() - gap) * 0.5f;
            reference_metric_painter::paintMetric (
                        g, metrics.removeFromLeft (juce::roundToInt (width)).toFloat(),
                        "INTEGRATED LOUDNESS", "LUFS", current.aIntegratedLoudness,
                        current.adjustedBIntegratedLoudness, current.loudnessDeltaBMinusA,
                        presentationContext);
            metrics.removeFromLeft (juce::roundToInt (gap));
            reference_metric_painter::paintMetric (
                        g, metrics.toFloat(), "MAXIMUM TRUE PEAK", "dBTP",
                        current.aMaximumTruePeakDbtp, current.adjustedBMaximumTruePeakDbtp,
                        current.truePeakDeltaBMinusA, presentationContext);
        }
        if (current.bSelected && std::isfinite (current.appliedGainDb))
        {
            const auto gain = "B " + fmtDelta (current.appliedGainDb) + " dB  /  "
                + (current.comparisonFallbackOriginal
                       ? (current.gainLimited ? "ORIGINAL / MATCH UNAVAILABLE"
                                              : "ORIGINAL / FACT UNAVAILABLE")
                   : current.gainLimited ? "LIMITED" : "MATCHED")
                + "  /  NO LIMITER / PEAK CEILING";
            g.setColour ((current.gainLimited ? COL_FLORA_BR : COL_MUTED).withAlpha (0.9f));
            g.setFont (labelFont (presentationContext, typography::TextRole::status,
                                  typography::Composition::information));
            text_style::drawEllipsized (g, gain, gainStatusArea.reduced (4, 0),
                                        juce::Justification::centredRight);
        }
    }
    else
    {
        const int gap = 4;
        auto left = area.removeFromLeft ((area.getWidth() - gap) / 2);
        area.removeFromLeft (gap);
        reference_metric_painter::paintCompactDelta (
            g, left.toFloat(), "LUFS-I", current.loudnessDeltaBMinusA, "LU",
            presentationContext);
        reference_metric_painter::paintCompactDelta (
            g, area.toFloat(), "MAX TP", current.truePeakDeltaBMinusA, "dB",
            presentationContext);
    }
}
}
