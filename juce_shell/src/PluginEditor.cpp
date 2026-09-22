#include "PluginEditor.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
 #include "HyphaAttackUiContract.h"
#endif

using hypha::COL_FLORA;
using hypha::COL_MUTED;
using hypha::COL_NORMAL;
using hypha::COL_SPECTRUM_DELTA;

namespace
{
    namespace ui = hypha::ui_contract;
}

KirinHyphaEditor::KirinHyphaEditor (KirinHyphaProcessorBase& p)
    : juce::AudioProcessorEditor (&p), processorRef (p), isPost (p.isPostRole()),
      observatoryView (isPost ? hypha::observatory::Role::post
                              : hypha::observatory::Role::pre)
{
    analysisOwnerToken = processorRef.beginAnalysisUiSession();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    const bool openAttackAtLaunch = isPost
        && juce::SystemStats::getEnvironmentVariable (
            hypha::attack_ui::activationEnvironmentVariable, {})
               .trim() == hypha::attack_ui::activationValue;
   #endif
    tooltip.setLookAndFeel (&tooltipLookAndFeel);
    setWantsKeyboardFocus (true);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    // One opaque Observatory root lets Windows present a completed frame instead of compositing
    // intermediate transformed children.
    scaleRoot.setOpaque (true);
    addAndMakeVisible (scaleRoot);
    observatorySizeIndex = juce::jmin (
        (size_t) processorRef.spectrumSizePreference(),
        hypha::observatory::sizePresets.size() - 1u);
    observatoryDomain = hypha::observatory::domainFromState (
        isPost ? hypha::observatory::Role::post : hypha::observatory::Role::pre,
        processorRef.observatoryDomainPreference());
    observatoryView.setTarget (hypha::observatory::targetFromState (
        isPost ? hypha::observatory::Role::post : hypha::observatory::Role::pre,
        processorRef.observatoryTargetPreference()));
    observatoryView.setTimeRange (hypha::observatory::timeRangeFromState (
        processorRef.observatoryTimeRangePreference()));
    configureMeterContext(); setResizable (true, false);
    setResizeLimits (300, 200, 900, 600);
    if (auto* aspectConstrainer = getConstrainer())
        aspectConstrainer->setFixedAspectRatio (1.5);
    const auto storedEditorSize = hypha::observatory::unpackEditorSize (
        processorRef.observatoryEditorSizePreference());
    auto initialWidth = storedEditorSize.width;
    auto initialHeight = storedEditorSize.height;
    if (! hypha::observatory::validEditorSize (initialWidth, initialHeight))
    {
        const auto fallback = hypha::observatory::sizePresets[observatorySizeIndex];
        initialWidth = fallback.width;
        initialHeight = fallback.height;
    }
    editorSizePersistenceReady = true;
    setSize (initialWidth, initialHeight);
    observatoryView.setHybridVuOnRecordEnabled (processorRef.hybridVuOnRecordPreference());
    observatoryView.setManualHybridVuVisible (processorRef.manualHybridVuSelection());
    observatoryView.onDomainChange = [this] (hypha::observatory::Domain domain) {
        if (observatoryView.dismissHybridVuForCurrentRecording()) resized();
        setObservatoryDomain (domain);
    };
    observatoryView.onTargetChange = [this] (hypha::observatory::ObservationTarget target)
    {
        if (! observatoryView.capabilities().targetSelectable) return;
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        if (target == hypha::observatory::ObservationTarget::delta
            && hypha::ui_contract::deltaBlockedByMidSide (
                observatoryDomain == hypha::observatory::Domain::frequency,
                spectrumView.isPsbObservation(), spectrumView.isMidSideObservation())) return;
       #endif
        observatoryView.setTarget (target);
        processorRef.setObservatoryTargetPreference (hypha::observatory::stateValue (target));
        if (target == hypha::observatory::ObservationTarget::delta)
        {
            comparisonActionAwaitingResult = true;
            comparisonActionAfterGeneration = comparisonObservedGeneration > 0
                ? comparisonObservedGeneration - 1 : 0;
        }
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        spectrumView.setAbsoluteObservation (
            target == hypha::observatory::ObservationTarget::absolute);
        if (analysisPage == AnalysisPage::spectrum) configureSpectrumAnalysis();
       #endif
    };
    observatoryView.onTimeRangeChange = [this] (hypha::observatory::TimeRange range)
    {
        processorRef.setObservatoryTimeRangePreference (hypha::observatory::stateValue (range));
    };
    observatoryView.onSizeChange = [this] (hypha::observatory::SizePreset preset)
    {
        for (size_t index = 0; index < hypha::observatory::sizePresets.size(); ++index)
            if (hypha::observatory::sizePresets[index].width == preset.width)
            {
                observatorySizeIndex = index;
                processorRef.setSpectrumSizePreference ((uint8_t) index);
            }
        setSize (preset.width, preset.height);
    };
    observatoryView.onCapture = [this] { beginObservatoryCapture(); };
    observatoryView.onInformation = [this] { showInformationMenu(); };
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    observatoryView.onRecordBodyOwnershipChange = [this] (bool)
    {
        updateAnalysisBodyPresentation();
    };
   #endif
    scaleRoot.addAndMakeVisible (observatoryView);

    scaleRoot.addAndMakeVisible (led);
    observatoryView.onLoudnessChange = [this] (bool shortTerm)
    {
        processorRef.setUseShortTermLoudness (shortTerm);
        observatoryView.setShortTermLoudness (shortTerm);
    };

    scaleRoot.addAndMakeVisible (nameField);

    if (isPost)
    {
        nameField.setPrefix ("PAIR ");
        nameField.setFallback ("SELECT PRE");
        nameField.setLockedTooltip (juce::CharPointer_UTF8 ("Pair selection is locked during playback"));
        nameField.setEnabledTooltip ("Click to choose one exact PRE.");
        nameField.setModelName (processorRef.pairDisplayName());
        nameField.onSelect = [this] { selectPairPreview(); };
        nameField.onPreviewDemand = [this] { refreshPairPreview (true); };
        nameField.setWantsKeyboardFocus (true);

        // The arrow beside PAIR owns exact connection selection only.
        pairDropdown.setTitle ("Pair menu");
        pairDropdown.setDescription ("Choose one exact PRE connection");
        pairDropdown.setTooltip ("PRE connection");
        pairDropdown.setColour (juce::TextButton::buttonColourId, hypha::kFieldFill);
        pairDropdown.setColour (juce::TextButton::textColourOnId,  COL_FLORA);
        pairDropdown.setColour (juce::TextButton::textColourOffId, COL_FLORA);
        pairDropdown.onClick = [this] { showCandidateMenu(); };
        scaleRoot.addAndMakeVisible (pairDropdown);

       #if ! KIRIN_HYPHA_PRE_DISPLAY
        observatorySizeIndex = juce::jmin (
            (size_t) processorRef.spectrumSizePreference(),
            ui::spectrumSizePresets.size() - 1u);
        spectrumToggle.setButtonText ("ANALYSIS");
        spectrumToggle.setTooltip (ui::spectrumTooltip (false));
        spectrumToggle.setColour (juce::TextButton::buttonColourId, hypha::kFieldFill);
        spectrumToggle.setColour (juce::TextButton::textColourOnId, COL_SPECTRUM_DELTA);
        spectrumToggle.setColour (juce::TextButton::textColourOffId, COL_MUTED);
        spectrumToggle.onClick = [this]
        {
            setAnalysisPage (analysisPage == AnalysisPage::meters
                               ? AnalysisPage::attack : AnalysisPage::meters);
        };
        scaleRoot.addAndMakeVisible (spectrumToggle);
        timePageNavigation.onPageChange = [this] (AnalysisPage page)
        {
            if (observatoryDomain == hypha::observatory::Domain::time)
                setAnalysisPage (page);
        };
        spectrumSizeToggle.setTitle ("Spectrum size");
        spectrumSizeToggle.setDescription (
            "Cycle POST Analysis between 100, 125, 150, and 200 percent");
        spectrumSizeToggle.setColour (juce::TextButton::buttonColourId, hypha::kFieldFill);
        spectrumSizeToggle.setColour (juce::TextButton::textColourOnId, COL_SPECTRUM_DELTA);
        spectrumSizeToggle.setColour (juce::TextButton::textColourOffId, COL_SPECTRUM_DELTA);
        spectrumSizeToggle.onClick = [this] { cycleSpectrumSize(); };
        configureSpectrumCallbacks();
        perceptualView.onChannelModeChange = [this] (uint8_t channelMode)
        {
            const auto accepted = processorRef.setSpectrumChannelMode (channelMode);
            return accepted ? (syncAnalysisDemand(), true) : false;
        };
        updateSpectrumSizeControl();
        scaleRoot.addChildComponent (timePageNavigation);
        scaleRoot.addChildComponent (spectrumSizeToggle);
        scaleRoot.addChildComponent (spectrumView);
        scaleRoot.addChildComponent (perceptualView);
        scaleRoot.addChildComponent (absoluteView);
        scaleRoot.addChildComponent (attackView);
        configureReferenceAudition();
        configureLocalBlindProduct();
       #endif
    }
    else
    {
        nameField.onCommit = [this] (const juce::String& n) { processorRef.setPreName (n); };
        nameField.setPrefix ("SOURCE ");
        nameField.setEnabledTooltip ("Click to edit this PRE name.");
        nameField.setModelName (processorRef.preName());
        nameField.setFallback (instanceId8());
    }

    // One role-independent slot owns feedback priority and the only bottom-row rectangle.
    feedbackLabel.setFont (hypha::monoFont (
        hypha::presentation::defaultContext(), hypha::typography::TextRole::status));
    feedbackLabel.setJustificationType (juce::Justification::centredLeft);
    feedbackLabel.setMinimumHorizontalScale (1.0f);
    feedbackLabel.setInterceptsMouseClicks (false, false);
    scaleRoot.addChildComponent (feedbackLabel);

   #if ! KIRIN_HYPHA_PRE_DISPLAY
    spectrumToggle.setVisible (false);
   #endif
    setObservatoryDomain (observatoryDomain);
    resized();
    refreshObservatory();
    // Producer JSON and meter snapshots advance at 100 ms. Spectrum raises this same timer to
    // 30 Hz only while its exact-pair exchange is active; every normal meter stays at 10 Hz.
    startTimerHz (ui::preDisplayPresentationHz);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (openAttackAtLaunch && ! processorRef.surroundMeasurementOnly())
    {
        observatorySizeIndex = ui::spectrumSizePresets.size() - 1u;
        observatoryDomain = hypha::observatory::Domain::time;
        observatoryView.setDomain (observatoryDomain);
        setAnalysisPage (AnalysisPage::attack);
    }
   #endif
}

juce::String KirinHyphaEditor::instanceId8() const
{
    return processorRef.instanceId().substring (0, 8);
}

void KirinHyphaEditor::paint (juce::Graphics& g)
{
    bg.draw (g, getLocalBounds()); // mycelium PNG over BG (R-12: pure chrome)

    g.setColour (COL_NORMAL);
    g.setFont (hypha::labelFont (hypha::presentation::forEditor (getWidth(), getHeight()),
                                 hypha::typography::TextRole::shellTitle));
    g.drawText (isPost ? ui::postTitle : ui::preTitle,
                titleArea,
                juce::Justification::centredLeft);

    // Spectrum page changes only this separator to its cool accent; meter pages keep flora.
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    g.setColour (analysisPage != AnalysisPage::meters
                   ? COL_SPECTRUM_DELTA : COL_FLORA);
   #else
    g.setColour (COL_FLORA);
   #endif
    g.fillRect ((float) ui::margin, (float) floraY,
                (float) (getWidth() - 2 * ui::margin), 1.0f);
}

void KirinHyphaEditor::resized()
{
    size_t nearestPreset = 0u;
    for (size_t index = 0u; index < hypha::observatory::sizePresets.size(); ++index)
        if (getWidth() >= hypha::observatory::sizePresets[index].width)
            nearestPreset = index;
    observatorySizeIndex = nearestPreset;
    if (editorSizePersistenceReady)
    {
        processorRef.setSpectrumSizePreference ((uint8_t) nearestPreset);
        if (processorRef.setObservatoryEditorSizePreference (getWidth(), getHeight()))
        {
            editorSizeStateDirty = true;
            editorSizeLastChangedAt = nowSecs();
        }
    }

    const auto viewport = hypha::observatory::displayViewport (getWidth(), getHeight());
    scaleRoot.setTransform (juce::AffineTransform());
    scaleRoot.setBounds (0, 0, viewport.width, viewport.height);
    scaleRoot.setBufferedToImage (false);
    scaleRoot.setTransform (juce::AffineTransform::scale (viewport.scale));
    observatoryView.setDisplayedEditorSize (getWidth(), getHeight());
    observatoryView.setBounds (scaleRoot.getLocalBounds());
    observatoryView.toBack();
    applyPresentationContext();
    auto connection = observatoryView.connectionBounds().reduced (4, 2);
    led.setBounds (connection.removeFromLeft (10).withSizeKeepingCentre (7, 7));
    if (isPost)
    {
        nameField.setPrefix (getWidth() < 450 ? "" : "PAIR ");
        pairDropdown.setBounds (connection.removeFromRight (ui::pairDropdownWidth));
        connection.removeFromRight (ui::pairDropdownGap);
    }
    const bool showName = true; nameField.setVisible (showName);
    observatoryView.setExternalConnectionLabelVisible (showName);
    if (showName)
        nameField.setBounds (connection);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (isPost)
    {
        spectrumToggle.setVisible (false);
        spectrumSizeToggle.setVisible (false);
        updateTimePageNavigation();
        auto analysisBody = observatoryView.analysisBodyBounds();
        timePageNavigation.setBounds (observatoryView.timeNavigationBounds());
        spectrumView.setBounds (analysisBody);
        perceptualView.setBounds (analysisBody);
        absoluteView.setBounds (analysisBody);
        attackView.setBounds (analysisBody);
        layoutReferenceAudition (analysisBody);
        timePageNavigation.toFront (false);
    }
   #endif
    feedbackLabel.setBounds (observatoryView.sessionBounds());
    feedbackLabel.toFront (false);
    if (observatoryView.hybridVuVisible()) observatoryView.toFront (false);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (isPost) layoutLocalBlindProduct();
   #endif
}
#if ! KIRIN_HYPHA_PRE_DISPLAY
void KirinHyphaEditor::cycleSpectrumSize()
{
    if (analysisPage == AnalysisPage::meters)
        return;
    observatorySizeIndex = (observatorySizeIndex + 1u) % ui::spectrumSizePresets.size();
    processorRef.setSpectrumSizePreference ((uint8_t) observatorySizeIndex);
    updateSpectrumSizeControl();
    const auto preset = ui::spectrumSizePresets[observatorySizeIndex];
    setSize (preset.width, preset.height);
}

void KirinHyphaEditor::updateSpectrumSizeControl()
{
    const auto preset = ui::spectrumSizePresets[observatorySizeIndex];
    spectrumSizeToggle.setButtonText (preset.buttonText);
    spectrumSizeToggle.setTooltip (preset.tooltip);
}
#endif

void KirinHyphaEditor::showToast (const juce::String& msg)
{
    toastUntil = nowSecs() + 3.0; // TOAST_DURATION_SECS
    toastText = msg;
    updateFeedback (nowSecs(), false, {});
}

void KirinHyphaEditor::updateFeedback (
    double now, bool keeping, const juce::String& persistentError)
{
    juce::String text;
    juce::Colour colour = COL_MUTED;

    // Direct user-action feedback must remain visible even while a persistent producer error is
    // present (R-28). After the three-second toast, the persistent error automatically returns;
    // the short acknowledgement is the lowest-priority informational state.
    if (now < toastUntil && toastText.isNotEmpty())
    {
        text = toastText;
        colour = COL_NORMAL;
    }
    else if (persistentError.isNotEmpty())
    {
        text = persistentError;
        colour = hypha::COL_LED_YELLOW;
    }
    else if (keeping)
    {
        text = "Keeping";
        colour = COL_FLORA;
    }

    if (now >= toastUntil)
        toastText.clear();

    observatoryView.setFeedback (text);
    feedbackLabel.setVisible (false);
    if (text.isNotEmpty())
    {
        feedbackLabel.setText (text, juce::dontSendNotification);
        feedbackLabel.setColour (juce::Label::textColourId, colour);
    }
}
