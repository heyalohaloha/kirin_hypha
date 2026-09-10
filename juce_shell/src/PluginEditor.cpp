#include "PluginEditor.h"
#include "HyphaDisplayContract.h"
#if ! KIRIN_HYPHA_PRE_DISPLAY
 #include "HyphaAttackUiContract.h"
#endif

#include <cmath>

using hypha::COL_FLORA;
using hypha::COL_FLORA_BR;
using hypha::COL_MUTED;
using hypha::COL_NORMAL;
using hypha::COL_SPECTRUM_DELTA;
using hypha::COL_SPECTRUM_POST;

namespace
{
    namespace ui = hypha::ui_contract;
    namespace display = hypha::display_contract;

    juce::Rectangle<int> juceRect (ui::Rect rect)
    {
        return { rect.x, rect.y, rect.width, rect.height };
    }

    juce::String metricHelp (ui::Metric metric)
    {
        switch (metric)
        {
            case ui::Metric::lufs:      return hypha::helpLufsM();
            case ui::Metric::truePeak:  return hypha::helpTp();
            case ui::Metric::maxTruePeak:return hypha::helpTp();
            case ui::Metric::crest:     return hypha::helpCrest();
            case ui::Metric::psr:       return hypha::helpPsr();
            case ui::Metric::integrated:return hypha::helpLufsI();
            case ui::Metric::sharpness: return hypha::helpSharp();
        }
        return {};
    }


}

KirinHyphaEditor::KirinHyphaEditor (KirinHyphaProcessorBase& p)
    : juce::AudioProcessorEditor (&p), processorRef (p), isPost (p.isPostRole()),
      observatoryView (isPost ? hypha::observatory::Role::post
                              : hypha::observatory::Role::pre)
{
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
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio (1.5);
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
        observatoryView.setTarget (target);
        processorRef.setObservatoryTargetPreference (hypha::observatory::stateValue (target));
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
    scaleRoot.addAndMakeVisible (observatoryView);

    scaleRoot.addAndMakeVisible (led);
    for (auto& c : cells)
        scaleRoot.addAndMakeVisible (c);
    loudnessSelector.setShortTerm (processorRef.useShortTermLoudness());
    loudnessSelector.onChange = [this] (bool shortTerm)
    {
        processorRef.setUseShortTermLoudness (shortTerm);
        observatoryView.setShortTermLoudness (shortTerm);
        configureForKind (currentKind);
    };
    observatoryView.onLoudnessChange = [this] (bool shortTerm)
    {
        processorRef.setUseShortTermLoudness (shortTerm);
        loudnessSelector.setShortTerm (shortTerm);
        observatoryView.setShortTermLoudness (shortTerm);
    };
    scaleRoot.addAndMakeVisible (loudnessSelector);

    scaleRoot.addAndMakeVisible (nameField);

    pairStatusLabel.setFont (hypha::monoFont (
        hypha::presentation::defaultContext(), hypha::typography::TextRole::status));
    pairStatusLabel.setJustificationType (juce::Justification::centredRight);
    pairStatusLabel.setInterceptsMouseClicks (true, false);
    scaleRoot.addAndMakeVisible (pairStatusLabel);

    if (isPost)
    {
        nameField.setPrefix ("PAIR ");
        nameField.setFallback ("SELECT PRE");
        nameField.setLockedTooltip (juce::CharPointer_UTF8 ("Pair selection is locked during playback"));
        nameField.setEnabledTooltip ("Click to choose one exact PRE.");
        nameField.setModelName (processorRef.pairDisplayName());
        nameField.onSelect = [this] { showCandidateMenu(); };

        postControls = std::make_unique<hypha::PostControls>();
        scaleRoot.addAndMakeVisible (*postControls);
        postControls->onKeep = [this] {
            if (processorRef.keepPair()) return;
            const juce::String notice = processorRef.drainKeepActionNotice();
            if (notice.isNotEmpty()) { showToast (notice); return; }
            const juce::String err = processorRef.recordErrorMessage();
            if (err.isNotEmpty()) { showToast (err); return; }
            // B-118 (①): keep 失敗 = 非Os か no-PRE（egui trigger_keep の LicenseDenied / None と同文言）。
            showToast (processorRef.licenseIsOs() ? "No PRE Paired" : "Record requires Kirin OS license");
        };
        postControls->onStop = [this] { processorRef.stopPair(); };
        postControls->onSenseHint = [this]
        {
            if (! juce::URL ("https://kirinmastering.com").launchInDefaultBrowser())
                showToast ("Could not open browser");
        };

        // B-102/B-492: vector-arrow dropdown beside the pair field — All Keep / All Stop / candidates.
        pairDropdown.setTitle ("Pair, Keep, and display menu");
        pairDropdown.setDescription (
            "Choose an exact PRE pair, control Keep, or change hover help");
        pairDropdown.setTooltip ("Pair, Keep, and display options.");
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
        spectrumView.onChannelModeChange = [this] (uint8_t channelMode)
        {
            return processorRef.setSpectrumChannelMode (channelMode);
        };
        spectrumView.onSubviewChange = [this] { configureSpectrumAnalysis(); };
        perceptualView.onChannelModeChange = [this] (uint8_t channelMode)
        {
            return processorRef.setSpectrumChannelMode (channelMode);
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

    guideConnectButton.setColour (juce::TextButton::buttonColourId, hypha::kFieldFill);
    guideConnectButton.setColour (juce::TextButton::buttonOnColourId, hypha::kFieldFill);
    guideConnectButton.setColour (juce::TextButton::textColourOffId, COL_FLORA_BR);
    guideConnectButton.setColour (juce::TextButton::textColourOnId, COL_FLORA_BR);
    guideConnectButton.onClick = [this]
    {
        if (! processorRef.acceptPreDisplayConnection())
            showToast (processorRef.licenseIsOs() ? "Connection request is no longer available"
                                                  : "Kirin OS is required for Work connection");
    };
    scaleRoot.addChildComponent (guideConnectButton);

    configureForKind (Kind::WatchAbs6); // retained display compatibility; Observatory owns chrome
    for (auto& cell : cells)
        cell.setVisible (false);
    loudnessSelector.setVisible (false);
    pairStatusLabel.setVisible (false);
    if (postControls != nullptr)
        postControls->setVisible (false);
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
    if (openAttackAtLaunch)
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
        pairDropdown.setBounds (connection.removeFromRight (18));
    }
    const bool showName = true; nameField.setVisible (showName);
    observatoryView.setExternalConnectionLabelVisible (showName);
    if (showName)
        nameField.setBounds (connection);
    pairStatusLabel.setVisible (false);
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
    guideConnectButton.setBounds (observatoryView.guideBounds());
    feedbackLabel.setBounds (observatoryView.sessionBounds());
    feedbackLabel.toFront (false);
    if (observatoryView.hybridVuVisible()) observatoryView.toFront (false);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (isPost) layoutLocalBlindProduct();
   #endif
}
void KirinHyphaEditor::layoutMetrics (bool)
{
    for (int i = 0; i < 6; ++i)
        cells[(size_t) i].setBounds (juceRect (ui::metricCellBounds (i, metricTop, getWidth())));
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

void KirinHyphaEditor::configureForKind (Kind k)
{
    const bool watch = (k == Kind::WatchAbs6 || k == Kind::WatchDelta6);
    const bool dlt = (k == Kind::WatchDelta6 || k == Kind::Delta6);
    const juce::String d = hypha::delta();

    const auto& specs = watch ? ui::watchMetrics : ui::recordMetrics;
    for (int i = 0; i < (int) specs.size(); ++i)
    {
        const auto spec = specs[(size_t) i];
        const auto text = ui::metricText (spec.metric);
        const bool deltaCell = dlt && spec.deltaEligible;
        const juce::String label = i == 0
                                     ? juce::String()
                                     : spec.maximum
                                     ? juce::String (ui::maximumLabel)
                                     : (deltaCell ? d + text.deltaSuffix
                                                  : juce::String (text.absoluteLabel));
        const juce::String unit = deltaCell ? text.deltaUnit : text.absoluteUnit;
        const auto help = spec.metric == ui::Metric::lufs
                            && processorRef.useShortTermLoudness()
                              ? hypha::helpLufsS()
                              : metricHelp (spec.metric);
        cells[(size_t) i].configure (label, unit, help, ui::metricMinimumLabelWidth);
        cells[(size_t) i].setVisible (false);
    }
    currentKind = k;
    currentSix  = true;
    loudnessSelector.setShortTerm (processorRef.useShortTermLoudness());
    loudnessSelector.setDeltaMode (dlt);
    loudnessSelector.setVisible (false);
    layoutMetrics (true);
    loudnessSelector.toFront (false);
}

void KirinHyphaEditor::fillAbs (int cell, double v, bool isTp, bool muted)
{
    const juce::Colour col = std::isnan (v) ? COL_MUTED
                                            : (muted ? COL_MUTED
                                                     : (isTp ? hypha::tpColour (v) : hypha::valColour (v)));
    cells[(size_t) cell].setValue (hypha::fmtVal (v), col);
}

void KirinHyphaEditor::fillDelta (int cell, double v, bool isTp, juce::Colour deltaBase, bool tpWarn, bool muted)
{
    const juce::Colour col = std::isnan (v) ? COL_MUTED
                                            : (muted ? COL_MUTED
                                                     : ((isTp && tpWarn) ? COL_FLORA_BR : deltaBase));
    cells[(size_t) cell].setValue (hypha::fmtDelta (v), col);
}

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
