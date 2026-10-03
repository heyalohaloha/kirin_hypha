#include "HyphaReferenceComponent.h"

#include "HyphaReferenceSelectorLookAndFeel.h"
#include "HyphaReferenceMetricPainter.h"
#include "HyphaReferenceVisuals.h"
#include "HyphaReferenceBalance.h"
#include "HyphaReferenceStatusModel.h"
#include "HyphaReferenceDisplayText.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <algorithm>
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
    addChildComponent (comparisonView); addChildComponent (tonalView);
    addChildComponent(workflowControls);
    workflowControls.onStart=[this]{if(onStartReview)onStartReview();};
    workflowControls.onBookmark=[this]{if(onStartBookmark)onStartBookmark();};
    workflowControls.onBack=[this]{if(onWorkflowBack)onWorkflowBack();};
    workflowControls.onConfirmed=[this]{if(onWorkflowConfirmed)onWorkflowConfirmed();};
    workflowControls.onDeferred=[this]{if(onWorkflowDeferred)onWorkflowDeferred();};
    workflowControls.onEnd=[this]{if(onWorkflowEnd)onWorkflowEnd();};
    comparisonView.onCapturedRange=[this](double start,double end)
    {if(onCapturedTonalRange)onCapturedTonalRange(start,end);};
    connectionStatus.setComponentID ("reference-connection");
    connectionStatus.setText ("OS", juce::dontSendNotification);
    connectionStatus.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (connectionStatus);
    // Labels and the review workflow's buttons show their text in the current language (INV-S40).
    setLookAndFeel (&selectorLookAndFeel);
    presetBox.setLookAndFeel (&selectorLookAndFeel);
    versionBox.setLookAndFeel (&selectorLookAndFeel);
    checkBox.setLookAndFeel (&selectorLookAndFeel);
    candidateBox.setLookAndFeel (&selectorLookAndFeel);
    cueBox.setLookAndFeel (&selectorLookAndFeel);
    configureSelector (presetBox, "reference-preset", "Check Preset",
                       "Temporarily call a Check Preset received from Kirin OS.");
    configureSelector (versionBox, "reference-version", "V Version",
                       "Choose the registered Version for V. A stays the current DAW input.");
    versionBox.setTextWhenNothingSelected ("Choose Version");
    configureSelector (checkBox, "reference-check", "Check",
                       "Temporarily call a Check received from Kirin OS.");
    configureSelector (candidateBox, "reference-candidate", "B Source",
                       "Replace B with a prepared past Version or Reference for this audition.");
    configureSelector (cueBox, "reference-cue", "Cue",
                       "Choose a prepared listening position for this audition.");
    aButton.setComponentID ("reference-a");
    bButton.setComponentID ("reference-b");
    cButton.setComponentID ("reference-c");
    blindButton.setComponentID ("reference-blind");
    oneButton.setComponentID ("reference-blind-1");
    twoButton.setComponentID ("reference-blind-2");
    revealButton.setComponentID ("reference-blind-reveal");
    endBlindButton.setComponentID ("reference-blind-end");
    actionButton.setComponentID ("reference-action");
    aButton.setTitle ("Audition A");
    bButton.setTitle ("Audition V");
    cButton.setTitle ("Audition C Check");
    blindButton.setTitle ("Start Version Blind");
    oneButton.setTitle ("Audition blind source 1");
    twoButton.setTitle ("Audition blind source 2");
    revealButton.setTitle ("Reveal blind sources");
    endBlindButton.setTitle ("End Blind Compare");
    aButton.setTooltip ("Return to the live DAW mix (A).");
    bButton.setTooltip ("Audition the Version from Kirin OS (V).");
    blindButton.setTooltip (
        "Start a separate Version Blind trial. Check Preset settings and facts are hidden.");
    oneButton.setTooltip ("Audition source 1. Its identity remains hidden.");
    twoButton.setTooltip ("Audition source 2. Its identity remains hidden.");
    revealButton.setTooltip ("Reveal which source is A and which source is V.");
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
    bButton.onClick = [this] { if (! openLarge (1) && ! explainUnavailable (true) && onSelectB) onSelectB(); };
    blindButton.onClick = [this] { if (onStartBlind) onStartBlind(); };
    oneButton.onClick = [this] { if (onSelectBlindStimulus) onSelectBlindStimulus (1); };
    twoButton.onClick = [this] { if (onSelectBlindStimulus) onSelectBlindStimulus (2); };
    revealButton.onClick = [this] { if (onRevealBlind) onRevealBlind(); };
    endBlindButton.onClick = [this] { if (onEndBlind) onEndBlind(); };
    actionButton.onClick = [this] { if (onAction) onAction(); };
    cButton.onClick = [this] { if (! openLarge (2) && ! explainUnavailable (false) && onSelectC) onSelectC(); };
    versionBox.onChange = [this]
    { if (onSelectVersion) onSelectVersion (selectedOptionId (versionBox, current.versions)); };
    for (size_t i = 0; i < selectionReadouts.size(); ++i)
    {
        auto& label = selectionReadouts[i];
        label.setComponentID ("reference-selection-value-" + juce::String (static_cast<int> (i)));
        label.setColour (juce::Label::textColourId, COL_TEXT_SECONDARY);
        label.setMinimumHorizontalScale (1.0f);
        addChildComponent (label);
    }
    addAndMakeVisible (versionBox);
    addAndMakeVisible (cButton);
    addAndMakeVisible (presetBox);
    addAndMakeVisible (checkBox);
    addAndMakeVisible (candidateBox);
    addAndMakeVisible (cueBox);
    addAndMakeVisible (aButton);
    addAndMakeVisible (bButton);
    addChildComponent (blindButton);
    addChildComponent (oneButton);
    addChildComponent (twoButton);
    addChildComponent (revealButton);
    addChildComponent (endBlindButton);
    addChildComponent (actionButton);
    configureRoles();
    configureCheckPage();
}

void Component::setState (State next)
{
    current = std::move (next);
    prepareDisplayNames (current);
    setTitle (current.title);
    connectionStatus.setFont (labelFont (presentationContext, typography::TextRole::unit, typography::Composition::information));
    connectionStatus.setTooltip (current.osOnline ? "Kirin OS connected"
        : current.libraryReceived ? "Kirin OS offline / received presets available" : "Waiting for Kirin OS");
    connectionStatus.setTitle (connectionStatus.getTooltip());
    connectionStatus.setColour (juce::Label::textColourId, current.osOnline ? COL_LED_BLUE : COL_MUTED);
    const bool blindSession = isBlindSession (current.blindPhase);
    const bool workflowActive = current.workflow.mode != reference_audition::WorkflowView::Mode::normal
        && current.workflow.status != reference_audition::WorkflowView::Status::resumeAvailable;
    connectionStatus.setVisible (! blindSession);
    const bool blindAudition = isBlindAudition (current.blindPhase);
    aButton.setToggleState (! current.bSelected, juce::dontSendNotification);
    bButton.setToggleState (current.bSelected && (! current.separateComparisons
        || current.audibleComparisonSlot == 1), juce::dontSendNotification);
    cButton.setToggleState (current.bSelected && current.audibleComparisonSlot == 2, juce::dontSendNotification);
    syncSourceButtons();
    aButton.setVisible (! blindSession);
    bButton.setVisible (! blindSession);
    cButton.setVisible (! blindSession && current.separateComparisons);
    versionBox.setVisible (! blindSession && current.separateComparisons);
    const bool versionChosen = current.separateComparisons && current.versionId.isNotEmpty()
        && current.libraryReceived && current.osAccess != os_access::State::unowned;
    blindButton.setVisible (! blindSession && ! workflowActive
        && (!current.separateComparisons || current.comparisonSlot == 1)
        && (versionChosen || canStartBlind (current)));
    blindButton.setEnabled (!current.blindLargeScreen || canStartBlind (current));
    blindButton.setButtonText (current.blindLargeScreen ? "VERSION BLIND" : "BLIND 300%");
    blindButton.setTitle (current.blindLargeScreen ? "Start Version Blind" : "Open Blind at 300%");
    oneButton.setVisible (blindAudition);
    twoButton.setVisible (blindAudition);
    const bool bothHeard = current.blindStimulusOneHeard && current.blindStimulusTwoHeard;
    revealButton.setVisible (current.blindPhase == BlindPhase::active);
    revealButton.setEnabled (bothHeard);
    revealButton.setTooltip (bothHeard ? "Reveal both sources without recording a preference."
        : "Listen to both sources before revealing them.");
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
    oneButton.setEnabled (!current.blindPaused && current.pendingBlindStimulus != 1);
    twoButton.setEnabled (!current.blindPaused && current.pendingBlindStimulus != 2);
    syncSelectionControl (presetBox, current.presets, current.presetId);
    syncSelectionControl (versionBox, current.versions, current.versionId);
    syncSelectionControl (checkBox, current.checks, current.checkId);
    syncSelectionControl (candidateBox, current.candidates, current.candidateId);
    syncSelectionControl (cueBox, current.cues, current.cueId);
    cueBox.setEnabled (cueBox.isEnabled() && ! current.candidatePreparationPending);
    const bool showDetailedSelectors = detailedLayout() && ! blindSession;
    presetBox.setVisible (!blindSession && !workflowActive && !current.presets.empty());
    checkBox.setVisible (! blindSession && ! workflowActive && ! current.checks.empty());
    candidateBox.setVisible (! blindSession && ! current.separateComparisons && ! current.candidates.empty());
    cueBox.setVisible ((showDetailedSelectors || (current.separateComparisons && !blindSession))
        && !workflowActive && !current.cues.empty());
    syncRoles (blindSession, workflowActive);
    syncCheckPage (blindSession, workflowActive);
    actionButton.setButtonText (current.actionText);
    actionButton.setAttention (current.sampleRateApprovalRequired);
    actionButton.setTooltip (current.sampleRateApprovalRequired
        ? "Approve " + juce::String (current.sampleRateApprovalSlot == 1 ? "V " : "C ") + juce::String (current.sourceSampleRateHz / 1000.0, 1) + " to " + juce::String (current.hostSampleRateHz / 1000.0, 1) + " kHz for the audition copy only. A stays unchanged."
        : current.actionText == "EDIT GENRE" ? "Open this Balance Check in Kirin OS."
        : "Continue with the safe next action.");
    actionButton.setVisible (! blindSession && current.actionText.isNotEmpty());
    workflowControls.update(current.workflow,blindSession,!detailedLayout());
    workflowControls.setVisible(!blindSession&&(current.workflow.reviewAvailable
        ||current.workflow.bookmarkAvailable
        ||current.workflow.mode!=reference_audition::WorkflowView::Mode::normal));
    comparisonView.setVisible (! guideShown && current.separateComparisons && (current.comparisonSlot == 1 || (current.captureAccess && current.captureAccess->capturedView)) && !blindSession);
    const auto emptyB = current.versionId.isEmpty() ? juce::String ("Choose Version")
        : current.versionStep == SourceStep::ready ? juce::String ("Preparing V overview")
        : stepText (current.versionStep);
    comparisonView.update (current.visualTimeline, current.visualPositionSeconds, presentationContext,
                           blindSession, current.visualPreferences, emptyB);
    const bool tonalSelected = std::find (current.viewBindings.begin(), current.viewBindings.end(),
                                          "balance") != current.viewBindings.end();
    tonalView.setVisible (! guideShown && ! blindSession && ! comparisonView.isVisible() && tonalSelected);
    tonalView.update (current.visualTimeline, presentationContext, blindSession,
                      current.candidateName, current.cueLabel);
    resized();
    repaint();
}

bool Component::detailedLayout() const noexcept
{
    return observatory::isFullDensity (presentationContext.density);
}


void Component::paint (juce::Graphics& g)
{
    lastGuideFit = {};
    auto area = panelArea();
    auto header = area.removeFromTop (panelHeaderHeight());
    const bool blindActive = current.blindPhase == BlindPhase::active;
    const bool blindStarting = current.blindPhase == BlindPhase::starting;
    const bool blindInvalidated = current.blindPhase == BlindPhase::invalidated;
    const bool blindRevealed = current.blindPhase == BlindPhase::revealed;
    const bool blindSession = isBlindSession (current.blindPhase);
    if (checkPage() || versionPage()) { area.removeFromTop (panelGap() + (checkPage() ? checkPageRows : versionPageRows)); paintCheckPageLabels (g); }
    else if (current.separateComparisons && ! blindSession)
    {
        area.removeFromTop ((selectionVisible (presetBox) || selectionVisible (cueBox) ? (detailedLayout() ? 38 : panelPickerHeight()) : 0)
                            + panelGap() + (detailedLayout() ? 40 : panelPickerHeight()));
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (presentationContext, typography::TextRole::unit,
                              typography::Composition::information));
        const auto label = [&] (const juce::ComboBox& box, const juce::String& text)
        {
            if (! box.isVisible()) return;  // 隠れた選択欄の見出しは描かない（前の配置の位置に残さない）
            auto bounds = box.getBounds();
            if (detailedLayout()) bounds = bounds.withY (bounds.getY() - 17).withHeight (15);
            else bounds = bounds.withX (bounds.getX() - 14).withWidth (12);
            text_style::drawEllipsized (g, text, bounds, juce::Justification::centredLeft);
        };
        const bool referenceView = current.comparisonSlot == 3;
        if (referenceView) { label (songSetBox, detailedLayout() ? "B SET" : "S"); label (songBox, detailedLayout() ? "B / REF" : "B"); }
        else { label (versionBox, detailedLayout() ? "V / VERSION" : "V"); label (checkBox, detailedLayout() ? "C / CHECK" : "C"); }
        if (detailedLayout() && ! guideShown && ! referenceView) paintSourceHints (g);
        if (detailedLayout()) { if (selectionVisible (presetBox)) label (presetBox, "PRESET"); if (selectionVisible (cueBox)) label (cueBox, "CUE"); }
    }
    else if (detailedLayout() && ! blindSession)
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
            text_style::drawText (g, text, cell.removeFromTop (15), juce::Justification::centredLeft);
        };
        drawSelectorLabel (selectors.removeFromLeft (columnWidth), "PRESET");
        selectors.removeFromLeft (gap);
        drawSelectorLabel (selectors.removeFromLeft (columnWidth), "CHECK");
        selectors.removeFromLeft (gap);
        drawSelectorLabel (selectors.removeFromLeft (columnWidth), "B SOURCE");
        selectors.removeFromLeft (gap);
        drawSelectorLabel (selectors, "CUE");
    }
    else if (! detailedLayout() && ! blindSession
             && (selectionVisible (presetBox) || selectionVisible (checkBox) || selectionVisible (candidateBox)))
    {
        if (selectionVisible (presetBox)) area.removeFromTop (panelPickerHeight());
        area.removeFromTop (panelGap());
        auto selector = area.removeFromTop (panelPickerHeight());
        g.setColour (COL_TEXT_TERTIARY.withAlpha (0.92f));
        g.setFont (labelFont (presentationContext, typography::TextRole::metricLabel,
                              typography::Composition::information));
        constexpr int gap = 5;
        if (selectionVisible (checkBox) && selectionVisible (candidateBox))
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
            text_style::drawEllipsized (g, selectionVisible (checkBox) ? "CHECK" : "B SOURCE",
                                        selector.removeFromLeft (selectionVisible (checkBox) ? 38 : 64),
                                        juce::Justification::centredLeft);
        }
    }
    int controlsWidth = comparisonButtonWidth() * (current.separateComparisons ? 4 : 2)
        + (current.separateComparisons ? 9 : 3);
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

        area.removeFromTop (panelGap());
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
        if (! blindInvalidated && current.blindStimulusOneHeard && current.blindStimulusTwoHeard)
            status = "BOTH HEARD / REVEAL WHEN READY";
        if (current.blindPaused) status = "PAUSED / PLAY TO RESUME BLIND";
        else if (current.blindOutsideSong) status = "PLAY WITHIN THE SONG";
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
    text_style::draw (g, "REFERENCE",
                      header.removeFromTop (navigationHeight), presentationContext,
                      typography::TextRole::navigation, juce::Justification::centredLeft,
                      1, typography::Composition::information);
    g.setColour (COL_OBSERVATORY_VALUE);
    const auto title = current.title.isNotEmpty() ? current.title
        : current.separateComparisons && current.comparisonSlot == 1 ? juce::String { "VERSION" }
        : current.checkLabel;
    g.setFont (displayTextFont (title, presentationContext,
                                typography::TextRole::body,
                                typography::Composition::information));
    if(!shortPanel()) text_style::drawEllipsized (g, title, header, juce::Justification::centredLeft);

    area.removeFromTop (panelGap());
    if(workflowControls.isVisible()) area.removeFromTop(workflowControls.preferredHeight()+panelGap());
    auto statusArea = area.removeFromBottom (detailedLayout() && current.sampleRateApprovalRequired ? 32 : detailedLayout() ? 24 : 18);
    const auto line = referenceStatusLine (current); // H9: 聴ける／準備中／できない
    const auto statusColour = line.kind == StatusKind::ready ? COL_SPECTRUM_DELTA_BR : line.kind == StatusKind::waiting ? COL_FLORA_BR : COL_TEXT_SECONDARY;
    g.setColour (statusColour.withAlpha (0.92f));
    g.setFont (labelFont (presentationContext, typography::TextRole::readout,
                          typography::Composition::information));
    if (! blindSession)
    {
        g.setColour (current.osOnline ? COL_LED_BLUE : COL_MUTED);
        g.fillEllipse (static_cast<float> (connectionStatus.getX() - 4), 10.0f, 4.0f, 4.0f);
        g.setColour (statusColour.withAlpha (0.92f));
    }
    auto statusText = blindRevealed && current.blindReveal.isNotEmpty()
        ? "REVEALED / " + current.blindReveal : current.status;
    const auto side = current.separateComparisons ? roleLetter (current.comparisonSlot) : "B";
    const auto audibleSide = current.separateComparisons ? roleLetter (current.audibleComparisonSlot) : "B";
    if (! blindRevealed) statusText = line.text;
    auto availableStatusArea = statusArea;
    if (blindRevealed)
        availableStatusArea.removeFromLeft ((detailedLayout() ? 62 : 48) * 2 + 6);
    if (blindButton.isVisible())
        availableStatusArea.removeFromRight (detailedLayout() ? 120 : 90);
    if (actionButton.isVisible())
        availableStatusArea.removeFromRight (detailedLayout() && current.sampleRateApprovalRequired ? 244 : detailedLayout() ? 194 : 122);
    auto primaryStatusArea = availableStatusArea;
    auto gainStatusArea = availableStatusArea;
    if (detailedLayout() && current.bSelected)
        primaryStatusArea = gainStatusArea.removeFromLeft (
            juce::roundToInt (gainStatusArea.getWidth() * 0.42f));
    // The guide already says the next step; the line stays for a rejection, an action or an overdue wait (H6).
    const bool statusShown = ! guideShown || current.readiness == Readiness::rejected
                          || actionButton.isVisible() || current.preparationOverdue.isNotEmpty();
    if (statusShown && ! blindRevealed) paintStatusDot (g, primaryStatusArea, line.kind);
    if (statusShown)
        text_style::drawEllipsized (g, statusText, primaryStatusArea.reduced (4, 0).withTrimmedLeft (blindRevealed ? 0 : 12),
                                    juce::Justification::centredLeft);
    if (guideShown)
    {
        const bool footerFree = ! statusShown && ! blindButton.isVisible();
        lastGuideFit = paintGuide (g, footerFree ? area.getUnion (statusArea) : area, guide (current),
                                   presentationContext);
        return;
    }

    if (detailedLayout())
    {
        area = paintCheckFooter (g, area);  // H12: C の画面の 4 帯域と Cue の時間軸
        if (songList.isVisible()) paintReferenceBalance (g, area.withTrimmedLeft (songList.getWidth() + 6).toFloat(), current, presentationContext);
        else if (!comparisonView.isVisible() && !tonalView.isVisible()
            && !paintConfiguredReferenceViews (g, area.toFloat(), current, presentationContext))
        {
            auto metrics = area;
            const float gap = 6.0f;
            const float width = (metrics.getWidth() - gap) * 0.5f;
            reference_metric_painter::paintMetric (
                        g, metrics.removeFromLeft (juce::roundToInt (width)).toFloat(),
                        "INTEGRATED LOUDNESS", "LUFS", current.aIntegratedLoudness,
                        current.adjustedBIntegratedLoudness, current.loudnessDeltaBMinusA,
                        presentationContext, side);
            metrics.removeFromLeft (juce::roundToInt (gap));
            reference_metric_painter::paintMetric (
                        g, metrics.toFloat(), "MAXIMUM TRUE PEAK", "dBTP",
                        current.aMaximumTruePeakDbtp, current.adjustedBMaximumTruePeakDbtp,
                        current.truePeakDeltaBMinusA, presentationContext, side);
        }
        if (current.bSelected && std::isfinite (current.appliedGainDb) && ! checkPage())  // C の画面は MATCH の横に出す
        {
            // 読みは鳴っている音の基準（承認して A を下げているなら、その量を足した後の gain）。
            const auto gain = juce::String { audibleSide } + " " + fmtDelta (current.appliedGainDb + current.heldAttenuationDb) + " dB  /  "
                + gainReadoutState (current);
            g.setColour ((current.gainLimited ? COL_FLORA_BR : COL_MUTED).withAlpha (0.9f));
            g.setFont (labelFont (presentationContext, typography::TextRole::status,
                                  typography::Composition::information));
            text_style::drawEllipsized (g, gain, gainStatusArea.reduced (4, 0),
                                        juce::Justification::centredRight);
        }
    }
    else if (!comparisonView.isVisible() && !tonalView.isVisible())
    {
        const int gap = 4;
        auto left = area.removeFromLeft ((area.getWidth() - gap) / 2);
        area.removeFromLeft (gap);
        reference_metric_painter::paintCompactDelta (
            g, left.toFloat(), "LUFS-I", current.loudnessDeltaBMinusA, "LU",
            presentationContext, side);
        reference_metric_painter::paintCompactDelta (
            g, area.toFloat(), "MAX TP", current.truePeakDeltaBMinusA, "dB",
            presentationContext, side);
    }
}
}
