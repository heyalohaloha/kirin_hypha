#include "PluginEditor.h"

using hypha::COL_LED_BLUE;
using hypha::COL_MUTED;

namespace
{
juce::String observatoryPairText (int status)
{
    if (status == KIRIN_PAIR_STATUS_PAIRED) return juce::CharPointer_UTF8 ("PAIR ●");
    if (status == KIRIN_PAIR_STATUS_WAITING) return juce::CharPointer_UTF8 ("PAIR ◌");
    return juce::CharPointer_UTF8 ("PAIR —");
}

juce::Colour observatoryPairColour (int status)
{
    if (status == KIRIN_PAIR_STATUS_PAIRED) return COL_LED_BLUE;
    if (status == KIRIN_PAIR_STATUS_WAITING) return hypha::COL_FLORA;
    return COL_MUTED;
}
}

void KirinHyphaEditor::setObservatoryDomain (hypha::observatory::Domain domain)
{
    observatoryDomain = domain;
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
    KirinMeterSession meter {};
    observatoryView.setMeterSnapshot (meter, processorRef.pollMeterSession (meter));

    KirinDelta delta {};
    const auto pairStatus = processorRef.pairStatus();
    const bool haveDelta = isPost && pairStatus != KIRIN_PAIR_STATUS_UNPAIRED
                        && processorRef.pollDelta (delta);
    observatoryView.setDeltaSnapshot (delta, haveDelta);

    if (observatoryDomain == hypha::observatory::Domain::time
        && observatoryView.target() == hypha::observatory::ObservationTarget::absolute)
    {
        const auto request = observatoryView.historyRequest();
        std::vector<KirinMeterHistoryEntry> history;
        if (processorRef.pollMeterHistory (request.resolution, history, request.maxEntries))
            observatoryView.setHistory (std::move (history));
    }

    observatoryView.setConnectionText (observatoryPairText (pairStatus),
                                       observatoryPairColour (pairStatus));

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
