#include "PluginEditor.h"

using hypha::COL_LED_BLUE;
using hypha::COL_MUTED;

namespace
{
juce::String observatoryPairText (bool isPost, int status, const juce::String& name)
{
    if (! isPost) return name.isNotEmpty() ? "SOURCE " + name : juce::String ("SOURCE");
    if (status == KIRIN_PAIR_STATUS_PAIRED)
        return name.isNotEmpty() ? "PAIR " + name : juce::String (juce::CharPointer_UTF8 ("PAIR ●"));
    if (status == KIRIN_PAIR_STATUS_WAITING) return juce::CharPointer_UTF8 ("PAIR ◌");
    return juce::CharPointer_UTF8 ("PAIR —");
}

juce::Colour observatoryPairColour (bool isPost, int status)
{
    if (! isPost) return hypha::COL_SPECTRUM_POST;
    if (status == KIRIN_PAIR_STATUS_PAIRED) return COL_LED_BLUE;
    if (status == KIRIN_PAIR_STATUS_WAITING) return hypha::COL_FLORA;
    return COL_MUTED;
}

hypha::observatory::ConnectionState observatoryConnectionState (bool isPost, int status)
{
    if (! isPost) return hypha::observatory::ConnectionState::source;
    if (status == KIRIN_PAIR_STATUS_PAIRED)
        return hypha::observatory::ConnectionState::paired;
    if (status == KIRIN_PAIR_STATUS_WAITING)
        return hypha::observatory::ConnectionState::waiting;
    return hypha::observatory::ConnectionState::unpaired;
}
}

void KirinHyphaEditor::applyPresentationContext()
{
    const auto context = hypha::presentation::forEditor (getWidth(), getHeight());
    nameField.setPresentationContext (context);
    loudnessSelector.setPresentationContext (context);
    for (auto& cell : cells) cell.setPresentationContext (context);
    pairStatusLabel.setFont (hypha::monoFont (context, hypha::typography::TextRole::status));
    feedbackLabel.setFont (hypha::monoFont (context, hypha::typography::TextRole::status));
    guideConnectButton.setPresentationContext (context);
    if (postControls != nullptr) postControls->setPresentationContext (context);
#if ! KIRIN_HYPHA_PRE_DISPLAY
    spectrumView.setPresentationContext (context);
    perceptualView.setPresentationContext (context);
    absoluteView.setPresentationContext (context);
    attackView.setPresentationContext (context);
    referenceView.setPresentationContext (context);
    referenceAccessView.setPresentationContext (context);
    localBlindView.setPresentationContext (context);
    timePageNavigation.setPresentationContext (context);
    spectrumToggle.setPresentationContext (context);
    spectrumSizeToggle.setPresentationContext (context);
#endif
}

void KirinHyphaEditor::configureMeterContext()
{
    observatoryView.setMeterContext (processorRef.meterContextPreference());
    observatoryView.setScaleMode (processorRef.scaleModePreference());
    observatoryView.onContextChange = [this] (hypha::meter_context::MeterContext context)
    { applyMeterContextChoice (context); };
    observatoryView.onContextMenu = [this]
    { showMeterContextMenu (observatoryView.contextMenuAnchor()); };
    observatoryView.onScaleChange = [this] (hypha::meter_context::ScaleMode scale)
    {
        observatoryView.setScaleMode (scale);
        processorRef.setScaleModePreference (scale);
    };
    observatoryView.onDomainMenu = [this] { showDomainMenu(); };
    observatoryView.onSizeMenu = [this] { showSizeMenu(); };
    observatoryView.onOperationsMenu = [this] { showOperationsMenu(); };
    observatoryView.onGuideDetails = [this] { showGuideInformationMenu(); };
    observatoryView.onFeedbackDetails = [this] { showFeedbackInformationMenu(); };
    observatoryView.onStop = [this] { processorRef.stopPair(); };
    observatoryView.onReset = [this]
    {
        if (! processorRef.resetMeterSession())
        {
            showToast ("Meter Session could not be reset");
            return;
        }
        watchMaximum = {};
        observatoryWatchDisplay = {};
        haveWatchMaximum = false;
        haveObservatoryWatchDisplay = false;
        observatoryView.setWatchDisplay ({}, false);
    };
    observatoryView.onClearPeakClipHolds = [this]
    {
        if (! processorRef.clearMeterPeakClipHolds())
            showToast ("TP / Clip CLEAR failed");
    };
    observatoryView.onHybridVuChange = [this] (bool visible)
    {
        processorRef.setManualHybridVuSelection (
            observatoryView.manualHybridVuVisible());
        resized();
        if (visible) observatoryView.toFront (false);
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        syncAnalysisDemand();
       #endif
    };
    observatoryView.onNote = [this] { showNoteDialog(); };
}

void KirinHyphaEditor::showNoteDialog()
{
    if (! isPost || noteDialog != nullptr) return;
    noteDialog = std::make_unique<juce::AlertWindow> (
        "NOTE", "Attach a note to the current sample position.",
        juce::MessageBoxIconType::NoIcon, this);
    noteDialog->addTextEditor ("memo", {}, "NOTE");
    if (auto* editor = noteDialog->getTextEditor ("memo"))
        editor->setInputRestrictions (240);
    noteDialog->addButton ("ADD", 1, juce::KeyPress (juce::KeyPress::returnKey));
    noteDialog->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    noteDialog->centreAroundComponent (this, 360, 170);
    const juce::Component::SafePointer<KirinHyphaEditor> safe (this);
    noteDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe] (int result)
        {
            auto* owner = safe.getComponent();
            if (owner == nullptr || owner->noteDialog == nullptr) return;
            const auto memo = owner->noteDialog->getTextEditorContents ("memo").trim();
            if (result == 1)
            {
                if (memo.isEmpty()) owner->showToast ("NOTE is empty");
                else if (owner->processorRef.addNote (memo))
                    owner->showToast ("NOTE added at current sample");
                else owner->showToast ("NOTE could not be added at current sample");
            }
            owner->noteDialog.reset();
        }), false);
}

void KirinHyphaEditor::setObservatoryDomain (hypha::observatory::Domain domain)
{
    tooltip.hideTip();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (localBlindOpen) return;
   #endif
    const auto role = isPost ? hypha::observatory::Role::post : hypha::observatory::Role::pre;
    domain = hypha::observatory::sanitizeDomain (role, domain);
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (observatoryDomain == hypha::observatory::Domain::reference
        && domain != hypha::observatory::Domain::reference)
        processorRef.endReferenceBlind();
   #endif
    observatoryDomain = domain;
    processorRef.setObservatoryDomainPreference (hypha::observatory::stateValue (domain));
    observatoryView.setDomain (domain);
#if ! KIRIN_HYPHA_PRE_DISPLAY
    if (domain != hypha::observatory::Domain::frequency)
        observatoryView.setDeltaTargetEnabled (true);
    spectrumView.setAbsoluteObservation (
        observatoryView.target() == hypha::observatory::ObservationTarget::absolute);
    const auto page = domain == hypha::observatory::Domain::frequency
        ? AnalysisPage::spectrum : AnalysisPage::meters;
    setAnalysisPage (page);
    updateTimePageNavigation();
#endif
    resized();
    repaint();
}

void KirinHyphaEditor::visibilityChanged()
{
    refreshAppearance();
    // Some hosts snapshot non-parameter state when the editor becomes hidden, before destroying
    // it. Mark the already-updated exact dimensions dirty at that boundary as well as in dtor.
    if (! isVisible())
    {
        commitEditorSizeStateIfSettled (true);
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        if (localBlindOpen) processorRef.cancelLocalBlindProductSession();
        // Pro Tools can hide an editor without destroying it when another insert is opened.
        // The editor owns one typed optional-analysis request and releases it at this boundary.
        syncAnalysisDemand();
       #endif
    }
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    else if (isPost)
        syncAnalysisDemand();
   #endif
}

void KirinHyphaEditor::refreshObservatory()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    syncAnalysisDemand();
    if (localBlindOpen)
    {
        refreshLocalBlindProduct();
        return;
    }
   #endif
    const auto presentationNow = nowSecs();
    const auto hostHeartbeat = processorRef.hostProcessHeartbeatValue();
    if (hostHeartbeat != observedHostProcessHeartbeat)
    {
        observedHostProcessHeartbeat = hostHeartbeat;
        observedHostProcessHeartbeatAt = presentationNow;
    }
    constexpr double hostRecordingStaleSeconds = 0.35;
    const bool hostRecording = hostHeartbeat != 0u && processorRef.isHostRecording()
        && presentationNow - observedHostProcessHeartbeatAt <= hostRecordingStaleSeconds;
    const bool hybridPreferenceChanged = observatoryView.setHybridVuOnRecordEnabled (
        processorRef.hybridVuOnRecordPreference());
    const bool hostRecordingChanged = observatoryView.setHostRecording (hostRecording);
    if (hybridPreferenceChanged || hostRecordingChanged)
    {
        resized();
        if (observatoryView.hybridVuVisible())
            observatoryView.toFront (false);
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        syncAnalysisDemand();
       #endif
    }
    const auto role = isPost ? hypha::observatory::Role::post : hypha::observatory::Role::pre;
    const bool referenceOwned = isPost && processorRef.licenseIsOs();
    if (observatoryView.isReferenceOwned() != referenceOwned)
        observatoryView.setReferenceOwned (referenceOwned);
    auto restoredDomain = hypha::observatory::domainFromState (
        role, processorRef.observatoryDomainPreference());
    if (restoredDomain != observatoryDomain)
        setObservatoryDomain (restoredDomain);
    const auto restoredTarget = hypha::observatory::targetFromState (
        role, processorRef.observatoryTargetPreference());
    if (restoredTarget != observatoryView.preferredTarget())
    {
        observatoryView.setTarget (restoredTarget);
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        spectrumView.setAbsoluteObservation (
            restoredTarget == hypha::observatory::ObservationTarget::absolute);
        if (analysisPage == AnalysisPage::spectrum) configureSpectrumAnalysis();
       #endif
    }
    const auto restoredRange = hypha::observatory::timeRangeFromState (
        processorRef.observatoryTimeRangePreference());
    if (restoredRange != observatoryView.selectedTimeRange())
        observatoryView.setTimeRange (restoredRange);
    if (processorRef.meterContextPreference() != observatoryView.meterContext())
    {
        observatoryView.setMeterContext (processorRef.meterContextPreference());
       #if ! KIRIN_HYPHA_PRE_DISPLAY
        if (analysisPage == AnalysisPage::attack
            && ! hypha::meter_context::drumAttackAvailable (processorRef.meterContextPreference()))
            setAnalysisPage (AnalysisPage::meters);
        updateTimePageNavigation();
       #endif
    }
    if (processorRef.scaleModePreference() != observatoryView.scaleMode())
        observatoryView.setScaleMode (processorRef.scaleModePreference());
    const auto restoredSize = juce::jmin (
        (size_t) processorRef.spectrumSizePreference(),
        hypha::observatory::sizePresets.size() - 1u);
    const auto restoredEditorSize = hypha::observatory::unpackEditorSize (
        processorRef.observatoryEditorSizePreference());
    if (hypha::observatory::validEditorSize (
            restoredEditorSize.width, restoredEditorSize.height)
        && (restoredEditorSize.width != getWidth()
            || restoredEditorSize.height != getHeight()))
    {
        observatorySizeIndex = restoredSize;
        setSize (restoredEditorSize.width, restoredEditorSize.height);
    }

    KirinObservatoryFrame frame {};
    const bool frameAvailable = processorRef.pollObservatoryFrame (frame);
    observatoryView.setObservatoryFrame (frame, frameAvailable);
    observatoryView.setWatchDisplay (observatoryWatchDisplay, haveObservatoryWatchDisplay);
    observatoryView.setShortTermLoudness (processorRef.useShortTermLoudness());
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (isPost)
        refreshReferenceAudition (frame, frameAvailable);
   #endif

    const auto pairStatus = processorRef.pairStatus();

    if (observatoryDomain == hypha::observatory::Domain::time
        && observatoryView.capabilities().historyRange)
    {
        const auto request = observatoryView.historyRequest();
        std::vector<KirinMeterHistoryEntry> history;
        const auto historyReady = observatoryView.target()
            == hypha::observatory::ObservationTarget::absolute
            ? processorRef.pollMeterHistory (request.resolution, history, request.maxEntries,
                                             request.maxOutputEntries)
            : processorRef.pollMeterDeltaHistory (request.resolution, history, request.maxEntries,
                                                  request.maxOutputEntries);
        if (historyReady)
        {
            observatoryView.setHistory (std::move (history));
           #if ! KIRIN_HYPHA_PRE_DISPLAY
            updateTimePageNavigation();
           #endif
        }
    }
    else if (observatoryDomain == hypha::observatory::Domain::level
             && observatoryView.fullCockpit())
    {
        std::vector<KirinMeterHistoryEntry> history;
        constexpr size_t maximumEntries = 600;
        const auto maximumOutput = static_cast<size_t> (
            juce::jlimit (128, 600, observatoryView.bodyBounds().getWidth() * 2));
        const auto historyReady = observatoryView.target()
            == hypha::observatory::ObservationTarget::absolute
            ? processorRef.pollMeterHistory (KIRIN_METER_HISTORY_10_HZ, history,
                                             maximumEntries, maximumOutput)
            : processorRef.pollMeterDeltaHistory (KIRIN_METER_HISTORY_10_HZ, history,
                                                  maximumEntries, maximumOutput);
        if (historyReady)
            observatoryView.setHistory (std::move (history));
    }

    const auto sourceName = isPost ? processorRef.pairDisplayName() : processorRef.preName();
    observatoryView.setConnection (observatoryPairText (isPost, pairStatus, sourceName),
                                   observatoryPairColour (isPost, pairStatus),
                                   observatoryConnectionState (isPost, pairStatus));

    const auto previousBody = observatoryView.bodyBounds();
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    const auto connection = processorRef.pendingPreDisplayConnection();
    const bool connectionPending = connection.validAt (juce::Time::currentTimeMillis());
    if (connectionPending)
    {
        const auto title = connection.workTitle.isNotEmpty()
            ? connection.workTitle : connection.workId;
        const auto primary = "CONNECT  " + title.substring (0, 36);
        observatoryView.setGuide (primary, {}, true);
        guideConnectButton.setButtonText ("CONNECT");
        guideConnectButton.setTooltip ("Connect this Hypha session to Work: " + title);
    }
    else
    {
        const auto display = processorRef.preDisplaySnapshot();
        if (display.primary.isNotEmpty() || display.detail.isNotEmpty()
            || display.stateText.isNotEmpty())
        {
            auto detail = display.detail;
            if (display.stateText.isNotEmpty() && ! detail.contains (display.stateText))
                detail = detail.isEmpty() ? display.stateText
                                          : detail + "  " + display.stateText;
            observatoryView.setGuide (display.primary, detail,
                                      display.sectionActive || display.cueActive);
        }
        else
            observatoryView.clearGuide();
    }
    guideConnectButton.setVisible (connectionPending && ! observatoryView.hybridVuVisible());
#else
    observatoryView.clearGuide();
    guideConnectButton.setVisible (false);
#endif
    if (previousBody != observatoryView.bodyBounds())
        resized();
}
