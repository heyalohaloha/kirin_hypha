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
