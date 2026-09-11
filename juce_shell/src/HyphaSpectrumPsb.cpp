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

void SpectrumComponent::setPsbSnapshot (const KirinPsbView& frame)
{
    const auto values = copyPsb (frame.shares);
    const auto nextAbsolute = frame.is_delta == 0 ? values : std::array<double, 20> {};
    const auto nextDelta = frame.is_delta != 0 ? values : std::array<double, 20> {};
    const bool ready = frame.status == KIRIN_SPECTRUM_ACTIVE && frame.has_data == 1
        && frame.is_delta <= 1 && frame.channels >= 1 && frame.channels <= 2
        && frame.sample_rate >= 8'000 && frame.sample_rate <= 384'000 && frame.sample_rate % 10 == 0
        && frame.aperture_samples == frame.sample_rate / 10
        && frame.presentation_end_samples > frame.state_epoch_samples
        && frame.presentation_end_samples % frame.aperture_samples == 0
        && frame.state_epoch_samples % frame.aperture_samples == 0;
    const bool nextAbsoluteAvailable = ready && frame.is_delta == 0 && validAbsolutePsb (nextAbsolute);
    const bool nextDeltaAvailable = ready && frame.is_delta != 0 && validDeltaPsb (nextDelta);
    if (samePsb (absolutePsb, nextAbsolute) && samePsb (deltaPsb, nextDelta)
        && absolutePsbAvailable == nextAbsoluteAvailable
        && deltaPsbAvailable == nextDeltaAvailable && psbStatus == frame.status)
        return;
    absolutePsb = nextAbsolute;
    deltaPsb = nextDelta;
    absolutePsbAvailable = nextAbsoluteAvailable;
    deltaPsbAvailable = nextDeltaAvailable;
    psbStatus = frame.status;
    if (psbObservation) repaint();
}

void SpectrumComponent::setSignalActive (bool active)
{
    if (signalActive == active) return;
    signalActive = active;
    repaint();
}

void SpectrumComponent::paint (juce::Graphics& g)
{
    if (psbObservation)
    {
        const auto& values = absoluteObservation ? absolutePsb : deltaPsb;
        const auto status = psbStatus == KIRIN_SPECTRUM_IN_USE ? "ANALYSIS SLOTS IN USE"
            : psbStatus == KIRIN_SPECTRUM_NO_PAIR ? "PRE REQUIRED FOR DELTA"
            : ! signalActive ? "INACTIVE"
            : psbStatus == KIRIN_SPECTRUM_UNAVAILABLE ? "PSB UNAVAILABLE" : "PSB WARMING";
        psb_painter::paint (g, getLocalBounds().toFloat(), {
            values, signalActive && (absoluteObservation ? absolutePsbAvailable : deltaPsbAvailable),
            ! absoluteObservation, psbHoverBand, status, presentationContext });
    }
    else
    {
        const spectrum_chrome::PaintState state {
            snapshot, displayedPre, displayedPost, displayedDelta,
            readoutPre, readoutPost, readoutDelta, markedDelta,
            focusTrail.get(), modeActionNotice, analysisOwnerNames, guideOverlay,
            &absoluteHistory, absoluteHistory.peakHold(), absoluteObservation,
            midSideObservation,
            haveSnapshot, signalActive && currentSnapshotValid(),
            haveMark, hoverNormalisedX, focusFrequencyHz, channelMode, inputChannels, signalActive,
            presentationContext
        };
        spectrum_chrome::paint (g, getLocalBounds().toFloat(), state);
    }
    psb_painter::paintSubviewToggle (g, getLocalBounds().toFloat(), psbObservation,
                                     presentationContext);
}
}
