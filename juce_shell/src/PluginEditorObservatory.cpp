#include "PluginEditor.h"

using hypha::COL_LED_BLUE;
using hypha::COL_MUTED;

namespace
{
juce::String observatoryPairText (bool isPost, int status)
{
    if (! isPost) return "SOURCE";
    if (status == KIRIN_PAIR_STATUS_PAIRED) return juce::CharPointer_UTF8 ("PAIR ●");
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

void KirinHyphaEditor::configureMeterContext()
{
    observatoryView.setMeterContext (processorRef.meterContextPreference());
    observatoryView.setScaleMode (processorRef.scaleModePreference());
    observatoryView.onContextChange = [this] (hypha::meter_context::MeterContext context)
    {
        const auto scale = hypha::meter_context::initialScaleFor (context);
        observatoryView.setMeterContext (context);
        observatoryView.setScaleMode (scale);
        processorRef.setMeterContextPreference (context);
        processorRef.setScaleModePreference (scale);
    };
    observatoryView.onScaleChange = [this] (hypha::meter_context::ScaleMode scale)
    {
        observatoryView.setScaleMode (scale);
        processorRef.setScaleModePreference (scale);
    };
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
                else owner->showToast ("NOTE requires an active Keep");
            }
            owner->noteDialog.reset();
        }), false);
}

void KirinHyphaEditor::setObservatoryDomain (hypha::observatory::Domain domain)
{
    const auto role = isPost ? hypha::observatory::Role::post : hypha::observatory::Role::pre;
    domain = hypha::observatory::sanitizeDomain (role, domain);
    if (domain == hypha::observatory::Domain::reference && ! processorRef.licenseIsOs())
        domain = hypha::observatory::Domain::level;
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (observatoryDomain == hypha::observatory::Domain::reference
        && domain != hypha::observatory::Domain::reference)
        processorRef.endReferenceBlind();
   #endif
    observatoryDomain = domain;
    processorRef.setObservatoryDomainPreference (hypha::observatory::stateValue (domain));
    observatoryView.setDomain (domain);
#if ! KIRIN_HYPHA_PRE_DISPLAY
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

void KirinHyphaEditor::refreshObservatory()
{
    const auto role = isPost ? hypha::observatory::Role::post : hypha::observatory::Role::pre;
    const bool referenceEnabled = isPost && processorRef.licenseIsOs();
    if (observatoryView.isReferenceEnabled() != referenceEnabled)
        observatoryView.setReferenceEnabled (referenceEnabled);
    auto restoredDomain = hypha::observatory::domainFromState (
        role, processorRef.observatoryDomainPreference());
    if (! referenceEnabled && restoredDomain == hypha::observatory::Domain::reference)
        restoredDomain = hypha::observatory::Domain::level;
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
       #endif
    }
    const auto restoredRange = hypha::observatory::timeRangeFromState (
        processorRef.observatoryTimeRangePreference());
    if (restoredRange != observatoryView.selectedTimeRange())
        observatoryView.setTimeRange (restoredRange);
    if (processorRef.meterContextPreference() != observatoryView.meterContext())
        observatoryView.setMeterContext (processorRef.meterContextPreference());
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
        spectrumView.setPsbSnapshot (
            haveObservatoryWatchDisplay ? observatoryWatchDisplay.current : KirinMeasureResult {},
            frame.delta, frameAvailable && frame.delta_available != 0);
    if (isPost)
        refreshReferenceAudition (frame, frameAvailable);
   #endif

    const auto pairStatus = processorRef.pairStatus();

    if (observatoryDomain == hypha::observatory::Domain::time)
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
            if (analysisPage == AnalysisPage::run
                && ! observatoryView.runSummaryAvailable())
                setAnalysisPage (AnalysisPage::meters);
            else
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

    observatoryView.setConnection (observatoryPairText (isPost, pairStatus),
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
        guideConnectButton.setButtonText (primary);
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
    guideConnectButton.setVisible (connectionPending);
#else
    observatoryView.clearGuide();
    guideConnectButton.setVisible (false);
#endif
    if (previousBody != observatoryView.bodyBounds())
        resized();
}
