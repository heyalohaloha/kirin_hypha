#include "HyphaSpectrumComponent.h"
#include "HyphaPsbPainter.h"
#include "HyphaSpectrumChromePainter.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace hypha
{
namespace
{
template <typename Source>
std::array<double, 20> copyPsb (const Source& source)
{
    std::array<double, 20> values {};
    std::copy (std::begin (source), std::end (source), values.begin());
    return values;
}

bool validAbsolutePsb (const std::array<double, 20>& values) noexcept
{
    const bool finiteShares = std::all_of (values.begin(), values.end(), [] (double value) {
        return std::isfinite (value) && value >= 0.0 && value <= 1.0;
    });
    const auto sum = std::accumulate (values.begin(), values.end(), 0.0);
    return finiteShares && sum > 0.98 && sum < 1.02;
}

bool validDeltaPsb (const std::array<double, 20>& values) noexcept
{
    const bool finiteShares = std::all_of (values.begin(), values.end(), [] (double value) {
        return std::isfinite (value) && std::abs (value) <= 1.0;
    });
    const auto sum = std::accumulate (values.begin(), values.end(), 0.0);
    return finiteShares && std::abs (sum) < 0.02;
}

bool samePsb (const std::array<double, 20>& left,
    const std::array<double, 20>& right) noexcept
{
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::isnan (left[index]) && std::isnan (right[index])) continue;
        if (! std::isfinite (left[index]) || ! std::isfinite (right[index])
            || std::abs (left[index] - right[index]) > 0.0)
            return false;
    }
    return true;
}
}

void SpectrumComponent::setPsbSnapshot (
    const KirinMeasureResult& current, const KirinDelta& delta, bool deltaAvailable)
{
    const auto nextAbsolute = copyPsb (current.psb_bark);
    const auto nextDelta = copyPsb (delta.psb_bark);
    const bool nextAbsoluteAvailable = validAbsolutePsb (nextAbsolute);
    const bool nextDeltaAvailable = deltaAvailable && validDeltaPsb (nextDelta);
    if (samePsb (absolutePsb, nextAbsolute) && samePsb (deltaPsb, nextDelta)
        && absolutePsbAvailable == nextAbsoluteAvailable
        && deltaPsbAvailable == nextDeltaAvailable)
        return;
    absolutePsb = nextAbsolute;
    deltaPsb = nextDelta;
    absolutePsbAvailable = nextAbsoluteAvailable;
    deltaPsbAvailable = nextDeltaAvailable;
    if (psbObservation) repaint();
}

void SpectrumComponent::paint (juce::Graphics& g)
{
    if (psbObservation)
    {
        const auto& values = absoluteObservation ? absolutePsb : deltaPsb;
        psb_painter::paint (g, getLocalBounds().toFloat(), {
            values, absoluteObservation ? absolutePsbAvailable : deltaPsbAvailable,
            ! absoluteObservation, psbHoverBand });
    }
    else
    {
        const spectrum_chrome::PaintState state {
            snapshot, displayedPre, displayedPost, displayedDelta,
            readoutPre, readoutPost, readoutDelta, markedDelta,
            focusTrail.get(), modeActionNotice, analysisOwnerNames, guideOverlay,
            &absoluteHistory, absoluteHistory.peakHold(), absoluteObservation,
            haveSnapshot, currentSnapshotValid(),
            haveMark, hoverNormalisedX, focusFrequencyHz, channelMode, inputChannels
        };
        spectrum_chrome::paint (g, getLocalBounds().toFloat(), state);
    }
    psb_painter::paintSubviewToggle (g, getLocalBounds().toFloat(), psbObservation);
}
}
