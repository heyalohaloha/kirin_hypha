#include "HyphaSpectrumFocusTrail.h"

#include <algorithm>
#include <cmath>

namespace hypha::spectrum_focus
{
AppendResult FocusTrailHistory::append (int64_t presentationEndSamples,
                                        uint32_t incomingSampleRate,
                                        const DeltaBins& displayDelta) noexcept
{
    if (incomingSampleRate < static_cast<uint32_t> (ui_contract::spectrumPresentationHz)
        || ! std::all_of (displayDelta.begin(), displayDelta.end(),
                          [] (float value) { return std::isfinite (value); }))
    {
        return AppendResult::rejected;
    }

    const int64_t cadence = static_cast<int64_t> (
        incomingSampleRate / static_cast<uint32_t> (ui_contract::spectrumPresentationHz));
    if (cadence <= 0 || presentationEndSamples % cadence != 0)
        return AppendResult::rejected;

    bool reset = false;
    bool gap = false;
    if (! empty())
    {
        const int64_t newestEndpoint = endpointAt (count - 1u);
        if (sampleRate == incomingSampleRate && presentationEndSamples == newestEndpoint)
            return AppendResult::duplicateIgnored;
        if (sampleRate != incomingSampleRate
            || presentationEndSamples <= newestEndpoint)
        {
            clear();
            reset = true;
        }
        else
        {
            // Unsigned subtraction is exact after the strict ordering check above and avoids
            // signed overflow at hostile host-position boundaries.
            const uint64_t difference = static_cast<uint64_t> (presentationEndSamples)
                                      - static_cast<uint64_t> (newestEndpoint);
            const uint64_t horizon = static_cast<uint64_t> (incomingSampleRate)
                                   * static_cast<uint64_t> (focusTrailSeconds);
            if (difference >= horizon)
            {
                clear();
                reset = true;
            }
            else if (difference % static_cast<uint64_t> (cadence) != 0u)
            {
                return AppendResult::rejected;
            }
            else
            {
                gap = difference > static_cast<uint64_t> (cadence);
            }
        }
    }

    sampleRate = incomingSampleRate;
    const size_t destination = count < focusTrailCapacity
        ? (start + count) % focusTrailCapacity
        : start;
    frames[destination].presentationEndSamples = presentationEndSamples;
    frames[destination].displayDelta = displayDelta;
    if (count < focusTrailCapacity)
        ++count;
    else
        start = (start + 1u) % focusTrailCapacity;
    trimToVisibleHorizon (presentationEndSamples);
    return reset ? AppendResult::discontinuityReset
         : gap ? AppendResult::gapAppended
               : AppendResult::appended;
}

void FocusTrailHistory::clear() noexcept
{
    start = 0u;
    count = 0u;
    sampleRate = 0u;
}

void FocusTrailHistory::discardOldest() noexcept
{
    if (count == 0u)
        return;
    start = (start + 1u) % focusTrailCapacity;
    --count;
}

void FocusTrailHistory::trimToVisibleHorizon (int64_t newestEndpoint) noexcept
{
    if (sampleRate == 0u)
        return;
    const int64_t horizon = static_cast<int64_t> (sampleRate)
                          * static_cast<int64_t> (focusTrailSeconds);
    while (count > 1u && newestEndpoint - endpointAt (0u) >= horizon)
        discardOldest();
}

size_t FocusTrailHistory::physicalIndex (size_t chronologicalIndex) const noexcept
{
    if (count == 0u)
        return 0u;
    return (start + std::min (chronologicalIndex, count - 1u)) % focusTrailCapacity;
}

int64_t FocusTrailHistory::endpointAt (size_t chronologicalIndex) const noexcept
{
    return empty() ? 0 : frames[physicalIndex (chronologicalIndex)].presentationEndSamples;
}

float FocusTrailHistory::valueAt (size_t chronologicalIndex, float normalisedBand) const noexcept
{
    if (empty())
        return 0.0f;
    const auto& bins = frames[physicalIndex (chronologicalIndex)].displayDelta;
    const float position = std::clamp (normalisedBand, 0.0f, 1.0f)
                         * static_cast<float> (KIRIN_SPECTRUM_BAND_COUNT - 1u);
    const size_t lower = static_cast<size_t> (std::floor (position));
    const size_t upper = std::min (lower + 1u,
                                   static_cast<size_t> (KIRIN_SPECTRUM_BAND_COUNT - 1u));
    const float blend = position - static_cast<float> (lower);
    return bins[lower] + blend * (bins[upper] - bins[lower]);
}

double FocusTrailHistory::ageSecondsAt (size_t chronologicalIndex) const noexcept
{
    if (empty() || sampleRate == 0u)
        return 0.0;
    const int64_t newest = endpointAt (count - 1u);
    return static_cast<double> (newest - endpointAt (chronologicalIndex))
         / static_cast<double> (sampleRate);
}

bool FocusTrailHistory::hasGapBetween (size_t earlierIndex,
                                       size_t laterIndex) const noexcept
{
    if (sampleRate == 0u || earlierIndex >= laterIndex || laterIndex >= count)
        return false;
    const int64_t cadence = static_cast<int64_t> (
        sampleRate / static_cast<uint32_t> (ui_contract::spectrumPresentationHz));
    const int64_t expected = static_cast<int64_t> (laterIndex - earlierIndex) * cadence;
    return endpointAt (laterIndex) - endpointAt (earlierIndex) != expected;
}
}
