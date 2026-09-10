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
                        const juce::String& tooltip)
{
    box.setComponentID (componentId);
    box.setTooltip (tooltip);
    box.setWantsKeyboardFocus (true);
    box.setColour (juce::ComboBox::backgroundColourId, kFieldFill.withAlpha (0.94f));
    box.setColour (juce::ComboBox::outlineColourId, COL_MUTED.withAlpha (0.46f));
    box.setColour (juce::ComboBox::textColourId, COL_NORMAL);
    box.setColour (juce::ComboBox::arrowColourId, COL_FLORA.withAlpha (0.84f));
}

}

Component::SideButton::SideButton (const juce::String& text) : juce::TextButton (text)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Component::SideButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const bool selected = getToggleState();
    g.setColour ((selected ? COL_SPECTRUM_POST : kFieldFill)
                     .withAlpha (selected ? 0.24f : highlighted ? 0.86f : 0.62f));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour ((selected ? COL_SPECTRUM_DELTA_BR : COL_MUTED)
                     .withAlpha (isEnabled() ? (down ? 1.0f : 0.82f) : 0.28f));
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, selected ? 1.2f : 0.7f);
    g.setColour (! isEnabled() ? COL_MUTED.withAlpha (0.32f)
                               : selected ? COL_OBSERVATORY_VALUE : COL_NORMAL);
    g.setFont (labelFont (presentationContext, typography::TextRole::action,
                          typography::Composition::information));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

Component::Component()
{
    setOpaque (false);
    presetBox.setLookAndFeel (&selectorLookAndFeel);
    checkBox.setLookAndFeel (&selectorLookAndFeel);
    candidateBox.setLookAndFeel (&selectorLookAndFeel);
    cueBox.setLookAndFeel (&selectorLookAndFeel);
    configureSelector (presetBox, "reference-preset", "Choose a Kirin OS Reference Preset.");
    configureSelector (checkBox, "reference-check", "Choose what you want to check.");
    configureSelector (candidateBox, "reference-candidate", "Choose the comparison track.");
    configureSelector (cueBox, "reference-cue", "Choose the saved listening position.");
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
    blindButton.setTitle ("Start Blind Compare");
    oneButton.setTitle ("Audition blind source 1");
    twoButton.setTitle ("Audition blind source 2");
    answerButton.setTitle ("Choose the audible blind source as your answer");
    revealButton.setTitle ("Reveal blind sources");
    endBlindButton.setTitle ("End Blind Compare");
    aButton.setTooltip ("Return to the live DAW mix (A).");
    bButton.setTooltip ("Audition the Kirin OS prepared Reference (B).");
    blindButton.setTooltip ("Hide the A/B assignment and compare as source 1 and 2.");
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

void Component::syncSelectionControl (juce::ComboBox& box,
                                      const std::vector<SelectionOption>& options,
                                      const juce::String& selectedId)
{
    bool same = box.getNumItems() == static_cast<int> (options.size());
    for (int index = 0; same && index < box.getNumItems(); ++index)
        same = box.getItemText (index) == options[static_cast<size_t> (index)].label;
    if (! same)
    {
        box.clear (juce::dontSendNotification);
        for (size_t index = 0; index < options.size(); ++index)
            box.addItem (options[index].label, static_cast<int> (index) + 1);
    }
    int selected = 0;
    for (size_t index = 0; index < options.size(); ++index)
        if (options[index].id == selectedId)
            selected = static_cast<int> (index) + 1;
    box.setSelectedId (selected, juce::dontSendNotification);
    box.setEnabled (options.size() > 1);
}

juce::String Component::selectedOptionId (const juce::ComboBox& box,
                                          const std::vector<SelectionOption>& options)
{
    const int index = box.getSelectedId() - 1;
    return index >= 0 && index < static_cast<int> (options.size())
        ? options[static_cast<size_t> (index)].id : juce::String {};
}

void Component::setState (State next)
{
    current = std::move (next);
    const bool blindSession = isBlindSession (current.blindPhase);
    const bool blindAudition = isBlindAudition (current.blindPhase);
    aButton.setToggleState (! current.bSelected, juce::dontSendNotification);
    bButton.setToggleState (current.bSelected, juce::dontSendNotification);
    bButton.setEnabled (canSelectB (current));
    aButton.setVisible (! blindSession);
    bButton.setVisible (! blindSession);
    blindButton.setVisible (! blindSession && canStartBlind (current));
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
    const bool showSelectors = detailedLayout() && ! blindSession;
    presetBox.setVisible (showSelectors && ! current.presets.empty());
    checkBox.setVisible (showSelectors && ! current.checks.empty());
    candidateBox.setVisible (showSelectors && ! current.candidates.empty());
    cueBox.setVisible (showSelectors && ! current.cues.empty());
    actionButton.setButtonText (current.actionText);
    actionButton.setVisible (! blindSession && current.actionText.isNotEmpty());
    resized();
    repaint();
}

bool Component::detailedLayout() const noexcept
{
    return observatory::isFullDensity (presentationContext.density);
}

void Component::resized()
{
    auto area = getLocalBounds().reduced (6);
    auto header = area.removeFromTop (detailedLayout() ? 42 : 34);
    const int buttonWidth = detailedLayout() ? 62 : 48;
    const auto place = [&header] (juce::Component& button, int width)
    {
        button.setBounds (header.removeFromRight (width));
        header.removeFromRight (3);
    };
    if (isBlindSession (current.blindPhase))
    {
        place (endBlindButton, current.blindPhase == BlindPhase::invalidated
            ? (detailedLayout() ? 132 : 94) : buttonWidth);
        if (revealButton.isVisible())
            place (revealButton, detailedLayout() ? 78 : 62);
        if (answerButton.isVisible())
            place (answerButton, detailedLayout() ? 88 : 70);
        place (twoButton, buttonWidth);
        place (oneButton, buttonWidth);
    }
    else
    {
        if (blindButton.isVisible())
            place (blindButton, detailedLayout() ? 78 : 62);
        place (bButton, buttonWidth);
        place (aButton, buttonWidth);
    }
    const bool blindSession = isBlindSession (current.blindPhase);
    if (detailedLayout() && ! blindSession)
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
    if (actionButton.isVisible())
        actionButton.setBounds (area.removeFromBottom (detailedLayout() ? 24 : 18)
                                    .removeFromRight (detailedLayout() ? 188 : 116));
}

void Component::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (6);
    auto header = area.removeFromTop (detailedLayout() ? 42 : 34);
    const bool blindActive = current.blindPhase == BlindPhase::active;
    const bool blindStarting = current.blindPhase == BlindPhase::starting;
    const bool blindInvalidated = current.blindPhase == BlindPhase::invalidated;
    const bool blindRevealed = current.blindPhase == BlindPhase::revealed;
    if (detailedLayout() && ! blindStarting && ! blindActive
        && ! blindInvalidated && ! blindRevealed)
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
        drawSelectorLabel (selectors.removeFromLeft (columnWidth), "PRESET");
        selectors.removeFromLeft (gap);
        drawSelectorLabel (selectors.removeFromLeft (columnWidth), "CHECK");
        selectors.removeFromLeft (gap);
        drawSelectorLabel (selectors.removeFromLeft (columnWidth), "REFERENCE");
        selectors.removeFromLeft (gap);
        drawSelectorLabel (selectors, "CUE");
    }
    int controlsWidth = (detailedLayout() ? 62 : 48) * 2 + 3;
    if (blindInvalidated)
        controlsWidth = detailedLayout() ? 132 : 94;
    else if (blindActive)
    {
        controlsWidth += (detailedLayout() ? 62 : 48) + 6;
        if (revealButton.isVisible()) controlsWidth += (detailedLayout() ? 78 : 62) + 3;
        if (answerButton.isVisible()) controlsWidth += (detailedLayout() ? 88 : 70) + 3;
    }
    else if (blindRevealed)
        controlsWidth += (detailedLayout() ? 62 : 48) + 3;
    else if (blindButton.isVisible())
        controlsWidth += (detailedLayout() ? 78 : 62) + 3;
    header.removeFromRight (controlsWidth);
    const auto navigationHeight = juce::roundToInt (typography::resolve (
        presentationContext, typography::TextRole::navigation,
        typography::Composition::information).lineHeight);
    if (blindStarting || blindActive || blindInvalidated)
    {
        g.setColour (COL_FLORA.withAlpha (0.86f));
        g.setFont (labelFont (presentationContext, typography::TextRole::navigation,
                              typography::Composition::information));
        text_style::draw (g, "REFERENCE / BLIND COMPARE",
                          header.removeFromTop (navigationHeight), presentationContext,
                          typography::TextRole::navigation, juce::Justification::centredLeft,
                          1, typography::Composition::information);
        g.setColour (COL_OBSERVATORY_VALUE);
        g.setFont (labelFont (presentationContext, typography::TextRole::sectionTitle,
                              typography::Composition::information));
        text_style::draw (g, "SOURCE IDENTITY HIDDEN", header, presentationContext,
                          typography::TextRole::sectionTitle, juce::Justification::centredLeft,
                          1, typography::Composition::information);

        area.removeFromTop (4);
        auto statusArea = area.removeFromBottom (detailedLayout() ? 24 : 18);
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
        text_style::draw (g, status, statusArea.reduced (4, 0), presentationContext,
                          typography::TextRole::status, juce::Justification::centredLeft,
                          1, typography::Composition::information);
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
    const auto source = current.sourceLabel.isNotEmpty() ? current.sourceLabel : "KIRIN OS";
    g.setColour (COL_FLORA.withAlpha (0.86f));
    g.setFont (labelFont (presentationContext, typography::TextRole::navigation,
                          typography::Composition::information));
    text_style::draw (g, "REFERENCE / " + source,
                      header.removeFromTop (navigationHeight), presentationContext,
                      typography::TextRole::navigation, juce::Justification::centredLeft,
                      1, typography::Composition::information);
    g.setColour (COL_OBSERVATORY_VALUE);
    const auto title = current.title.isNotEmpty() ? current.title : juce::String { "REFERENCE" };
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
    auto statusText = blindRevealed && current.blindReveal.isNotEmpty()
        ? "REVEALED / " + current.blindReveal : current.status;
    if (detailedLayout() && current.alignmentLabel.isNotEmpty())
        statusText += (statusText.isNotEmpty() ? "  /  " : "") + current.alignmentLabel;
    auto primaryStatusArea = statusArea;
    if (actionButton.isVisible())
        primaryStatusArea.removeFromRight (detailedLayout() ? 194 : 122);
    if (detailedLayout() && current.bSelected)
        primaryStatusArea = statusArea.removeFromLeft (
            juce::roundToInt (statusArea.getWidth() * 0.52f));
    text_style::draw (g, statusText, primaryStatusArea.reduced (4, 0), presentationContext,
                      typography::TextRole::status, juce::Justification::centredLeft,
                      1, typography::Composition::information);

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
            const auto gain = "B GAIN " + fmtDelta (current.appliedGainDb) + " dB  /  "
                + (current.comparisonFallbackOriginal
                       ? (current.gainLimited ? "ORIGINAL / MATCH UNAVAILABLE"
                                              : "ORIGINAL / FACT UNAVAILABLE")
                   : current.gainLimited ? "MATCH LIMITED" : "MATCH APPLIED")
                + "  /  NO LIMITER / SOURCE PEAK CEILING";
            g.setColour ((current.gainLimited ? COL_FLORA_BR : COL_MUTED).withAlpha (0.9f));
            g.setFont (labelFont (presentationContext, typography::TextRole::status,
                                  typography::Composition::information));
            text_style::draw (g, gain, statusArea.reduced (4, 0), presentationContext,
                              typography::TextRole::status, juce::Justification::centredRight,
                              1, typography::Composition::information);
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
