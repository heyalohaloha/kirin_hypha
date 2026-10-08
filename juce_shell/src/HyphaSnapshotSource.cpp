#include "HyphaSnapshotSource.h"
#include "PluginProcessor.h"
#include <algorithm>
namespace hypha::snapshots
{
uint8_t Source::time (const KirinTimeSnapshotRequestV2& request, KirinTimeSnapshotV2& packet,
    std::vector<KirinTimeHistoryEntryV2>& main, std::vector<KirinTimeHistoryEntryV2>& psr,
    unsigned capacity, bool showPsr) const
{
    const auto n = std::min (capacity, KIRIN_TIME_HISTORY_CAPACITY);
    std::vector<KirinTimeHistoryEntryV2> nextMain (n), nextPsr (showPsr ? n : 0);
    KirinTimeSnapshotV2 next {};
    const juce::ScopedTryLock lock (processor.handleLock);
    if (! lock.isLocked() || processor.hyphaHandle == nullptr) return KIRIN_SNAPSHOT_BUSY;
    const auto status = kirin_hypha_poll_time_snapshot_v2 (processor.hyphaHandle, &request,
        nextMain.data(), n, nextPsr.data(), static_cast<uint32_t> (nextPsr.size()), &next);
    if (status != KIRIN_SNAPSHOT_SUCCESS) return status;
    if (next.main.history_count > nextMain.size() || next.psr.history_count > nextPsr.size())
        return KIRIN_SNAPSHOT_INVALID_REQUEST;
    nextMain.resize (next.main.history_count); nextPsr.resize (next.psr.history_count);
    packet = next; main = std::move (nextMain); psr = std::move (nextPsr);
    return status;
}
uint8_t Source::navigation (uint8_t target, KirinAttackNavigationV2& out) const
{
    const KirinAttackNavigationRequestV2 request { 2, sizeof (request), target, {} };
    const juce::ScopedTryLock lock (processor.handleLock);
    return ! lock.isLocked() || processor.hyphaHandle == nullptr ? KIRIN_SNAPSHOT_BUSY
        : kirin_hypha_poll_attack_navigation_v2 (processor.hyphaHandle, sizeof (request),
            &request, sizeof (out), &out);
}
uint8_t Source::summary (uint8_t target, uint8_t band, KirinAttackBandSummaryV2& out) const
{
    const KirinAttackBandSummaryV2Request request { 2, sizeof (request), target, band, {} };
    const juce::ScopedTryLock lock (processor.handleLock);
    return ! lock.isLocked() || processor.hyphaHandle == nullptr ? KIRIN_SNAPSHOT_BUSY
        : kirin_hypha_poll_attack_band_summary_v2 (processor.hyphaHandle, sizeof (request),
            &request, sizeof (out), &out);
}
uint8_t Source::single (uint64_t token, KirinAttackSingleSnapshotV2& out) const
{
    const juce::ScopedTryLock lock (processor.handleLock);
    return ! lock.isLocked() || processor.hyphaHandle == nullptr ? KIRIN_SNAPSHOT_BUSY
        : kirin_hypha_poll_attack_single_v2 (processor.hyphaHandle, token, sizeof (out), &out);
}
uint8_t Source::requestSingle (const KirinAttackSingleV2Request& request, uint64_t& token) const
{
    const juce::ScopedTryLock lock (processor.handleLock);
    return ! lock.isLocked() || processor.hyphaHandle == nullptr ? KIRIN_SNAPSHOT_BUSY
        : kirin_hypha_request_attack_single_v2 (processor.hyphaHandle, sizeof (request), &request, &token);
}
uint8_t Source::cancelSingle (uint64_t token) const
{
    const juce::ScopedTryLock lock (processor.handleLock);
    return ! lock.isLocked() || processor.hyphaHandle == nullptr ? KIRIN_SNAPSHOT_BUSY
        : kirin_hypha_cancel_attack_single_v2 (processor.hyphaHandle, token);
}
uint8_t Source::session (KirinMeterSessionV2& out) const
{
    const juce::ScopedTryLock lock (processor.handleLock);
    return ! lock.isLocked() || processor.hyphaHandle == nullptr ? KIRIN_SNAPSHOT_BUSY
        : kirin_hypha_poll_meter_session_v2 (processor.hyphaHandle, 2, sizeof (out), &out);
}
}
