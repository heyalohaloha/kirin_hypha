#include "PluginProcessor.h"
#include "kirin_hypha_display_ffi.h"
#include <algorithm>

bool KirinHyphaProcessorBase::pollWatchDisplay (KirinWatchDisplay& out) const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_poll_meter_display (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollRecordDisplay (KirinRecordDisplay& out) const
{
    const juce::ScopedLock sl (handleLock);
    if (hyphaHandle == nullptr)
        return false;
    return kirin_hypha_poll_record_display (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollMeterSession (KirinMeterSession& out) const
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_meter_session (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollObservatoryFrame (KirinObservatoryFrame& out) const
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_poll_observatory_frame (hyphaHandle, &out);
}

bool KirinHyphaProcessorBase::pollLevelSnapshot (
    KirinLevelSnapshot& out,
    std::vector<KirinMeterHistoryEntry>& history,
    std::array<KirinChainPoint, KIRIN_CHAIN_CAPACITY>& chain,
    size_t maxEntries, size_t maxOutputEntries,
    std::uint64_t knownChainRevision, bool latestOnly) const
{
    const auto range = std::min (maxEntries, static_cast<size_t> (KIRIN_CHAIN_CAPACITY));
    const auto capacity = std::min (maxOutputEntries, range);
    std::vector<KirinMeterHistoryEntry> candidate (capacity);
    KirinLevelSnapshot next {};
    const auto chainCapacity = role == Role::Post
        ? (latestOnly ? 1u : KIRIN_CHAIN_CAPACITY) : 0u;
    const juce::ScopedLock sl (handleLock);
    const auto ok = hyphaHandle != nullptr
        && kirin_hypha_poll_level_snapshot (
            hyphaHandle, KIRIN_LEVEL_SNAPSHOT_VERSION, static_cast<uint32_t> (range),
            candidate.data(), static_cast<uint32_t> (capacity), knownChainRevision,
            chain.data(), chainCapacity, &next);
    if (! ok || next.history_count > capacity)
        return false;
    candidate.resize (next.history_count);
    out = next;
    history = std::move (candidate);
    return true;
}

bool KirinHyphaProcessorBase::pollMeterHistory (
    uint8_t resolution,
    std::vector<KirinMeterHistoryEntry>& out,
    size_t maxEntries,
    size_t maxOutputEntries) const
{
    const auto boundedRange = std::min (maxEntries,
                                        static_cast<size_t> (KIRIN_METER_HISTORY_MAX_ENTRIES));
    const auto boundedOutput = std::min (maxOutputEntries, boundedRange);
    out.resize (boundedOutput);
    uint32_t count = 0;
    const juce::ScopedLock sl (handleLock);
    const auto ok = hyphaHandle != nullptr
                 && kirin_hypha_poll_meter_history_decimated (
                        hyphaHandle, resolution, static_cast<uint32_t> (boundedRange),
                        out.data(), static_cast<uint32_t> (boundedOutput), &count);
    if (! ok)
    {
        out.clear();
        return false;
    }
    out.resize (count);
    return true;
}

bool KirinHyphaProcessorBase::pollMeterDeltaHistory (
    uint8_t resolution,
    std::vector<KirinMeterHistoryEntry>& out,
    size_t maxEntries,
    size_t maxOutputEntries) const
{
    const auto boundedRange = std::min (maxEntries,
                                        static_cast<size_t> (KIRIN_METER_HISTORY_MAX_ENTRIES));
    const auto boundedOutput = std::min (maxOutputEntries, boundedRange);
    out.resize (boundedOutput);
    uint32_t count = 0;
    const juce::ScopedLock sl (handleLock);
    const auto ok = hyphaHandle != nullptr
                 && kirin_hypha_poll_meter_delta_history_decimated (
                        hyphaHandle, resolution, static_cast<uint32_t> (boundedRange),
                        out.data(), static_cast<uint32_t> (boundedOutput), &count);
    if (! ok)
    {
        out.clear();
        return false;
    }
    out.resize (count);
    return true;
}

bool KirinHyphaProcessorBase::resetMeterSession()
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_reset_meter_session (hyphaHandle);
}

bool KirinHyphaProcessorBase::clearMeterPeakClipHolds()
{
    const juce::ScopedLock sl (handleLock);
    return hyphaHandle != nullptr && kirin_hypha_clear_meter_peak_clip_holds (hyphaHandle);
}
