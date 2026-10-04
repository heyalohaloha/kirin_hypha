#include "HyphaReferenceComponent.h"

#include "HyphaReferenceSelectorLookAndFeel.h"
#include "HyphaReferenceMetricPainter.h"
#include "HyphaReferenceVisuals.h"
#include "HyphaReferenceBalance.h"
#include "HyphaReferenceStatusModel.h"
#include "HyphaReferenceDisplayText.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"
#include "HyphaReferenceHelpText.h"

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
    addMouseListener (this, true);  // 子の上でも、指している項目の説明を下の行に出す（HyphaReferenceHoverHelp.cpp）
    addChildComponent (comparisonView); addChildComponent (tonalView);
    comparisonView.onCapturedRange=[this](double start,double end)
    {if(onCapturedTonalRange)onCapturedTonalRange(start,end);};
    connectionStatus.setComponentID ("reference-connection");
    connectionStatus.setText ("OS", juce::dontSendNotification);
    connectionStatus.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (connectionStatus);
    // Labels and buttons show their text in the current language (INV-S40).
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
    actionButton.setComponentID ("reference-action");
    aButton.setTitle ("Audition A");
    bButton.setTitle ("Audition V");
    cButton.setTitle ("Audition C Check");
    blindButton.setTitle ("Start Version Blind");
    aButton.setTooltip ("Return to the live DAW mix (A).");
    bButton.setTooltip ("Audition the Version from Kirin OS (V).");
    blindButton.setTooltip (
        "Start a separate Version Blind trial. Check Preset settings and facts are hidden.");
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
    statusStrip.addChildComponent (blindButton);
    statusStrip.addChildComponent (actionButton);
    statusStrip.paintRow = [this] (juce::Graphics& g, juce::Rectangle<int> row, bool) { paintStatusRow (g, row); };
    statusStrip.layoutRow = [this] (juce::Rectangle<int> row) { layoutStatusRow (row); };
    statusStrip.footerFill = BG;
    addChildComponent (statusStrip);
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
    connectionStatus.setVisible (! blindSession);
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
    blindButton.setVisible (! blindSession
        && (!current.separateComparisons || current.comparisonSlot == 1)
        && (versionChosen || canStartBlind (current)));
    blindButton.setEnabled (!current.blindLargeScreen || canStartBlind (current));
    blindButton.setButtonText (current.blindLargeScreen ? "VERSION BLIND" : "BLIND 300%");
    blindButton.setTitle (current.blindLargeScreen ? "Start Version Blind" : "Open Blind at 300%");
    syncSelectionControl (presetBox, current.presets, current.presetId);
    syncSelectionControl (versionBox, current.versions, current.versionId);
    syncSelectionControl (checkBox, current.checks, current.checkId);
    syncSelectionControl (candidateBox, current.candidates, current.candidateId);
    syncSelectionControl (cueBox, current.cues, current.cueId);
    cueBox.setEnabled (cueBox.isEnabled() && ! current.candidatePreparationPending);
    const bool showDetailedSelectors = detailedLayout() && ! blindSession;
    presetBox.setVisible (!blindSession && !current.presets.empty());
    checkBox.setVisible (! blindSession && ! current.checks.empty());
    candidateBox.setVisible (! blindSession && ! current.separateComparisons && ! current.candidates.empty());
    cueBox.setVisible ((showDetailedSelectors || (current.separateComparisons && !blindSession))
        && !current.cues.empty());
    syncRoles (blindSession);
    syncCheckPage (blindSession);
    actionButton.setButtonText (current.actionText);
    actionButton.setAttention (current.sampleRateApprovalRequired);
    actionButton.setTooltip (current.sampleRateApprovalRequired
        ? "Approve " + juce::String (current.sampleRateApprovalSlot == 1 ? "V " : "C ") + juce::String (current.sourceSampleRateHz / 1000.0, 1) + " to " + juce::String (current.hostSampleRateHz / 1000.0, 1) + " kHz for the audition copy only. A stays unchanged."
        : current.actionText == "EDIT GENRE" ? "Open this Balance Check in Kirin OS."
        : "Continue with the safe next action.");
    actionButton.setVisible (! blindSession && current.actionText.isNotEmpty());
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
    statusStrip.repaint();  // 足元の段にあるときは REF の子ではないので、別に描き直す
}

bool Component::detailedLayout() const noexcept
{
    return observatory::isFullDensity (presentationContext.density);
}


void Component::paint (juce::Graphics& g)
{
    lastGuideFit = {};
    const help::Collector collect (helpRegions);  // 描く図が添える説明の場所（HyphaReferenceHelp.h）
    getProperties().set (help::shownInLineProperty, helpInLine());  // 部品の説明も吹き出しでなく下の行に
    auto area = panelArea();
    auto header = area.removeFromTop (panelHeaderHeight());
    // 2026-10-04：始めた VERSION BLIND は、エディターが PRE/POST Blind と同じ画面で窓全体に出す
    // （HyphaVersionBlindScreen.h）。その間 REF は何も描かない（曲名・図・状態の文は手がかりになる。R-28）。
    if (isBlindSession (current.blindPhase)) return;
    if (checkPage() || versionPage()) { area.removeFromTop (panelGap() + (checkPage() ? checkPageRows : versionPageRows)); paintCheckPageLabels (g); }
    else if (rolePage() && current.comparisonSlot == 3)
    {
        area.removeFromTop (panelGap());  // B SET と曲はボタンの段
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (presentationContext, typography::TextRole::unit, typography::Composition::information));
        for (const auto& [box, text] : { std::pair<const juce::ComboBox*, const char*> { &songSetBox, "B SET" }, { &songBox, "B / REF" } })
            if (box->isVisible() || selectionVisible (*box))
                text_style::drawEllipsized (g, text, box->getBounds().withY (box->getY() - 17).withHeight (15), juce::Justification::centredLeft);
    }
    else if (current.separateComparisons)
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
    else if (detailedLayout())
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
    else if (! detailedLayout()
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
    const int controlsWidth = comparisonButtonWidth() * (current.separateComparisons ? 4 : 2)
        + (current.separateComparisons ? 9 : 3);
    header.removeFromRight (controlsWidth + 32);
    const auto navigationHeight = juce::roundToInt (typography::resolve (
        presentationContext, typography::TextRole::navigation,
        typography::Composition::information).lineHeight);
    if (! rolePage())  // 300% の B・C・V では、ボタンの段の選択欄が見出しを兼ねる（同じ曲名を 2 度出さない）
    {
        g.setColour (COL_FLORA.withAlpha (0.86f));
        g.setFont (labelFont (presentationContext, typography::TextRole::navigation, typography::Composition::information));
        text_style::draw (g, "REFERENCE", header.removeFromTop (navigationHeight), presentationContext,
                          typography::TextRole::navigation, juce::Justification::centredLeft,
                          1, typography::Composition::information);
        g.setColour (COL_OBSERVATORY_VALUE);
        const auto title = current.title.isNotEmpty() ? current.title
            : current.separateComparisons && current.comparisonSlot == 1 ? juce::String { "VERSION" }
            : current.checkLabel;
        g.setFont (displayTextFont (title, presentationContext, typography::TextRole::body, typography::Composition::information));
        if (! shortPanel()) text_style::drawEllipsized (g, title, header, juce::Justification::centredLeft);
    }

    area.removeFromTop (panelGap());
    // 状態の行は StatusStrip が描く（300% の B・C・V では足元の段、ほかは REF の一番下。HyphaReferenceStatusRow.cpp）。
    const bool rowInPanel = ! statusInFooter();
    auto statusArea = rowInPanel ? area.removeFromBottom (statusRowHeight()) : juce::Rectangle<int> {};
    g.setColour (current.osOnline ? COL_LED_BLUE : COL_MUTED);
    g.fillEllipse (static_cast<float> (connectionStatus.getX() - 4), 10.0f, 4.0f, 4.0f);
    const auto side = current.separateComparisons ? roleLetter (current.comparisonSlot) : "B";
    const bool statusShown = statusLineShown();
    if (guideShown)
    {
        const bool footerFree = rowInPanel && ! statusShown && ! blindButton.isVisible();
        lastGuideFit = paintGuide (g, footerFree ? area.getUnion (statusArea) : area, guide (current),
                                   presentationContext);
        return;
    }

    if (detailedLayout())
    {
        area = paintCheckFooter (g, area);  // H12: C の画面の 4 帯域と Cue の時間軸
        if (songList.isVisible()) paintReferenceBalance (g, area.withTrimmedLeft (songList.getWidth() + 6).toFloat(), current, presentationContext);
        else if (!comparisonView.isVisible() && !tonalView.isVisible() && checkPage() && listeningCheck())
            paintListeningPanel (g, area);  // 耳で聴き比べる Check（C−A がいつも 0.0 の箱を出していた）
        else if (!comparisonView.isVisible() && !tonalView.isVisible()
            && !paintConfiguredReferenceViews (g, area.toFloat(), current, presentationContext))
        {
            auto metrics = area;
            const float gap = 6.0f;
            const float width = (metrics.getWidth() - gap) * 0.5f;
            help::note (metrics.withWidth (juce::roundToInt (width)), help_text::integrated);
            help::note (metrics.withTrimmedLeft (juce::roundToInt (width + gap)), help_text::truePeak);
            reference_metric_painter::paintMetric (
                        g, metrics.removeFromLeft (juce::roundToInt (width)).toFloat(),
                        "INTEGRATED LOUDNESS", "LUFS", current.aIntegratedLoudness,
                        current.adjustedBIntegratedLoudness, current.loudnessDeltaBMinusA, AWords::loudness,
                        presentationContext, side);
            metrics.removeFromLeft (juce::roundToInt (gap));
            reference_metric_painter::paintMetric (
                        g, metrics.toFloat(), "MAXIMUM TRUE PEAK", "dBTP",
                        current.aMaximumTruePeakDbtp, current.adjustedBMaximumTruePeakDbtp,
                        current.truePeakDeltaBMinusA, AWords::level, presentationContext, side);
        }
    }
    else if (!comparisonView.isVisible() && !tonalView.isVisible())
    {
        const int gap = 4;
        auto left = area.removeFromLeft ((area.getWidth() - gap) / 2);
        area.removeFromLeft (gap);
        reference_metric_painter::paintCompactDelta (
            g, left.toFloat(), "LUFS-I", current.loudnessDeltaBMinusA, "LU", AWords::loudness,
            presentationContext, side);
        reference_metric_painter::paintCompactDelta (
            g, area.toFloat(), "MAX TP", current.truePeakDeltaBMinusA, "dB", AWords::level,
            presentationContext, side);
    }
}
}
