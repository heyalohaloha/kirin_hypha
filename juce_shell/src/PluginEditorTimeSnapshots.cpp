#include "PluginEditor.h"
#include "HyphaSnapshotSource.h"
void KirinHyphaEditor::refreshTimeSnapshot()
{
    const auto started = juce::Time::getMillisecondCounterHiRes();
    if (started < nextTimeSnapshotMs) return;
    nextTimeSnapshotMs = started + 100.0;
    const auto range = observatoryView.historyRequest();
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (analysisPage == AnalysisPage::run)
    {
        std::vector<KirinMeterHistoryEntry> history;
        if (processorRef.pollMeterHistory (range.resolution, history, range.maxEntries, range.maxOutputEntries))
        {
            observatoryView.setHistory (std::move (history));
            updateTimePageNavigation();
        }
        return;
    }
   #endif
    const auto rate = observatoryView.meterSampleRateForSnapshot();
    if (rate <= 0.0) return;
    const auto framesPerEntry = range.resolution == KIRIN_METER_HISTORY_10_HZ ? 0.1
        : range.resolution == KIRIN_METER_HISTORY_1_HZ ? 1.0 : 10.0;
    const auto target = ! isPost ? KIRIN_TIME_PRE
        : observatoryView.target() == hypha::observatory::ObservationTarget::delta
            ? KIRIN_TIME_DELTA : KIRIN_TIME_POST;
    const KirinTimeSnapshotRequestV2 request { 2, sizeof (request), sizeof (KirinTimeSnapshotV2),
        sizeof (KirinTimeHistoryEntryV2), static_cast<uint64_t> (range.maxEntries * framesPerEntry * rate),
        static_cast<uint32_t> (target), range.resolution };
    KirinTimeSnapshotV2 packet {};
    std::vector<KirinTimeHistoryEntryV2> main, psr;
    const auto showPsr = observatoryView.presentationContext().density != hypha::observatory::Density::compact;
    if (hypha::snapshots::Source (processorRef).time (request, packet, main, psr,
            static_cast<unsigned> (range.maxOutputEntries), showPsr) == KIRIN_SNAPSHOT_SUCCESS)
        observatoryView.setTimeSnapshot (packet, std::move (main), std::move (psr), started,
            juce::Time::getMillisecondCounterHiRes(), showPsr);
}
