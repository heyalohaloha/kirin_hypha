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

void KirinHyphaEditor::setObservatoryDomain (hypha::observatory::Domain domain)
{
    const auto role = isPost ? hypha::observatory::Role::post : hypha::observatory::Role::pre;
    domain = hypha::observatory::sanitizeDomain (role, domain);
    observatoryDomain = domain;
    processorRef.setObservatoryDomainPreference (hypha::observatory::stateValue (domain));
    observatoryView.setDomain (domain);
#if ! KIRIN_HYPHA_PRE_DISPLAY
    spectrumView.setAbsoluteObservation (
        observatoryView.target() == hypha::observatory::ObservationTarget::absolute);
    const auto page = domain == hypha::observatory::Domain::frequency
        ? AnalysisPage::spectrum : AnalysisPage::meters;
    setAnalysisPage (page);
    analysisModeToggle.setVisible (domain == hypha::observatory::Domain::time);
#endif
    resized();
    repaint();
}

void KirinHyphaEditor::refreshObservatory()
{
    const auto role = isPost ? hypha::observatory::Role::post : hypha::observatory::Role::pre;
    const auto restoredDomain = hypha::observatory::domainFromState (
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
       #endif
    }
    const auto restoredRange = hypha::observatory::timeRangeFromState (
        processorRef.observatoryTimeRangePreference());
    if (restoredRange != observatoryView.selectedTimeRange())
        observatoryView.setTimeRange (restoredRange);
    const auto restoredSize = juce::jmin (
        (size_t) processorRef.spectrumSizePreference(),
        hypha::observatory::sizePresets.size() - 1u);
    if (restoredSize != observatorySizeIndex)
    {
        observatorySizeIndex = restoredSize;
        const auto preset = hypha::observatory::sizePresets[restoredSize];
        setSize (preset.width, preset.height);
    }

    KirinObservatoryFrame frame {};
    observatoryView.setObservatoryFrame (frame, processorRef.pollObservatoryFrame (frame));

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
