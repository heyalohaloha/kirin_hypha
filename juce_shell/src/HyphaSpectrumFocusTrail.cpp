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

    bool reset = false;
    if (! empty())
    {
        const int64_t newestEndpoint = endpointAt (count - 1u);
        if (sampleRate == incomingSampleRate && presentationEndSamples == newestEndpoint)
            return AppendResult::duplicateIgnored;
        const int64_t cadence = static_cast<int64_t> (
            incomingSampleRate / static_cast<uint32_t> (ui_contract::spectrumPresentationHz));
        if (sampleRate != incomingSampleRate
            || presentationEndSamples <= newestEndpoint
            || presentationEndSamples - newestEndpoint != cadence)
        {
            clear();
            reset = true;
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
    return reset ? AppendResult::discontinuityReset : AppendResult::appended;
}

void FocusTrailHistory::clear() noexcept
{
    start = 0u;
    count = 0u;
    sampleRate = 0u;
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
}
