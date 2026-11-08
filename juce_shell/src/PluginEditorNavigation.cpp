#include "PluginEditor.h"
#include "HyphaSpectrumUiContract.h"

#include <algorithm>

void KirinHyphaEditor::configureNavigationActions()
{
    observatoryView.onDomainChange = [this] (auto domain) { requestObservatoryDomain (domain); };
    observatoryView.onTargetChange = [this] (auto target) { requestObservationTarget (target); };
    configureShortcuts();
}

void KirinHyphaEditor::requestObservatoryDomain (hypha::observatory::Domain domain)
{
    if (refusedByBlind()) return;
    if (! isPost && (domain == hypha::observatory::Domain::frequency
                     || domain == hypha::observatory::Domain::reference))
    {
        showToast ("Available in POST");
        return;
    }
    if (processorRef.surroundMeasurementOnly() && domain != hypha::observatory::Domain::level
        && domain != hypha::observatory::Domain::time)
    {
        showToast ("Mono / stereo only");
        return;
    }
    if (observatoryView.dismissHybridVuForCurrentRecording()) resized();
    setObservatoryDomain (domain);
}

void KirinHyphaEditor::requestObservationTarget (hypha::observatory::ObservationTarget target)
{
    if (refusedByBlind()) return;
    if (! observatoryView.capabilities().targetSelectable) return;
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (target == hypha::observatory::ObservationTarget::delta
        && hypha::ui_contract::deltaBlockedByMidSide (
            observatoryDomain == hypha::observatory::Domain::frequency,
            spectrumView.isPsbObservation(), spectrumView.isMidSideObservation())) return;
   #endif
    nextTimeSnapshotMs = 0.0;
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    attackView.retireV2();
    nextDrumSummaryMs = 0.0;
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
}

#if ! KIRIN_HYPHA_PRE_DISPLAY
void KirinHyphaEditor::requestTimePage (AnalysisPage page)
{
    if (refusedByBlind() || observatoryDomain != hypha::observatory::Domain::time) return;
    if (page == AnalysisPage::attack
        && ! hypha::meter_context::drumAttackAvailable (processorRef.meterContextPreference())) return;
    setAnalysisPage (page);
}

// The pages of TIME in their order, round and round. DRUM only where it can be read.
void KirinHyphaEditor::stepTimePage (int direction)
{
    std::vector<AnalysisPage> pages;
    for (auto page : hypha::analysis_navigation::timePages)
        if (page != AnalysisPage::attack
            || hypha::meter_context::drumAttackAvailable (processorRef.meterContextPreference()))
            pages.push_back (page);
    const auto count = static_cast<int> (pages.size());
    const auto at = std::find (pages.begin(), pages.end(), analysisPage);
    const auto index = at == pages.end() ? 0 : static_cast<int> (std::distance (pages.begin(), at));
    requestTimePage (pages[static_cast<size_t> ((index + direction + count) % count)]);
}
#endif
