#include "HyphaSpectrumComponent.h"

#include "HyphaSpectrumPresentation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hypha
{
namespace
{
template <size_t Size>
bool finiteValues (const float (&values)[Size]) noexcept
{
    return std::all_of (std::begin (values), std::end (values),
                        [] (float value) { return std::isfinite (value); });
}

bool validMidSideSnapshot (const KirinMidSideSpectrumView& view) noexcept
{
    const uint64_t apertureNumerator = static_cast<uint64_t> (view.sample_rate) * 4'096u
                                     + 24'000u;
    const uint32_t expectedAperture = static_cast<uint32_t> (apertureNumerator / 48'000u);
    uint32_t expectedFft = 1u;
    while (expectedFft < expectedAperture * 2u && expectedFft <= 65'536u)
        expectedFft <<= 1u;
    const float expectedApproximateBelow = expectedAperture > 0u
        ? 3.0f * static_cast<float> (view.sample_rate) / static_cast<float> (expectedAperture)
        : 0.0f;
    return view.status == KIRIN_SPECTRUM_ACTIVE
        && view.has_data == 1u
        && view.channels == 2u
        && view.sample_rate >= 8'000u
        && view.sample_rate <= 384'000u
        && view.aperture_samples == expectedAperture
        && view.fft_size == expectedFft
        && std::isfinite (view.approximate_below_hz)
        && std::abs (view.approximate_below_hz - expectedApproximateBelow) < 0.001f
        && std::isfinite (view.min_hz)
        && std::isfinite (view.max_hz)
        && view.min_hz > 0.0f
        && view.max_hz > view.min_hz
        && finiteValues (view.mid_dbfs)
        && finiteValues (view.side_dbfs);
}

bool sameMidSideLayout (const KirinMidSideSpectrumView& left,
                        const KirinMidSideSpectrumView& right) noexcept
{
    const auto sameBits = [] (float first, float second) {
        return std::memcmp (&first, &second, sizeof (float)) == 0;
    };
    return left.sample_rate == right.sample_rate
        && left.aperture_samples == right.aperture_samples
        && left.fft_size == right.fft_size
        && left.channels == right.channels
        && sameBits (left.min_hz, right.min_hz)
        && sameBits (left.max_hz, right.max_hz)
        && sameBits (left.approximate_below_hz, right.approximate_below_hz);
}

KirinSpectrumView layoutView (const KirinMidSideSpectrumView& source) noexcept
{
    KirinSpectrumView view {};
    view.status = source.status;
    view.channels = source.channels;
    view.sample_rate = source.sample_rate;
    view.min_hz = source.min_hz;
    view.max_hz = source.max_hz;
    view.presentation_end_samples = source.presentation_end_samples;
    view.aperture_samples = source.aperture_samples;
    view.fft_size = source.fft_size;
    view.approximate_below_hz = source.approximate_below_hz;
    return view;
}
}

void SpectrumComponent::setAbsoluteObservation (bool absolute)
{
    if (absoluteObservation == absolute)
        return;
    absoluteObservation = absolute;
    absolutePsbAvailable = deltaPsbAvailable = false;
    psbStatus = KIRIN_SPECTRUM_WARMING_UP;
    clearInteractionState();
    if (haveSnapshot && ! midSideObservation)
    {
        const auto retained = snapshot;
        haveSnapshot = false;
        havePendingSnapshot = false;
        setSnapshot (retained);
    }
    else
        repaint();
}

void SpectrumComponent::setDisplaySelection (uint8_t selection)
{
    if (selection > KIRIN_SPECTRUM_SELECTION_MID_SIDE || channelMode == selection)
        return;
    clearSnapshot();
    channelMode = selection;
    midSideObservation = selection == KIRIN_SPECTRUM_SELECTION_MID_SIDE;
    repaint();
}

void SpectrumComponent::setMidSideSnapshot (const KirinMidSideSpectrumView& next)
{
    const bool valid = validMidSideSnapshot (next);
    const bool continuing = midSideObservation && midSideSnapshotValid && valid
                         && sameMidSideLayout (midSideSnapshot, next);
    if (! continuing)
        clearInteractionState();
    midSideSnapshotValid = valid;
    midSideSnapshot = next;
    midSideObservation = true;
    absoluteObservation = true;
    channelMode = KIRIN_SPECTRUM_SELECTION_MID_SIDE;
    inputChannels = next.channels;
    const auto nextLayout = layoutView (next);
    haveSnapshot = true;
    absoluteHistory.clear();
    if (valid)
    {
        const auto calmWeights = spectrum_presentation::lowFrequencyCalmWeights<
            KIRIN_SPECTRUM_BAND_COUNT> (next.min_hz, next.max_hz);
        pendingPre = spectrum_presentation::calmLowFrequencies (next.mid_dbfs, calmWeights);
        pendingPost = spectrum_presentation::calmLowFrequencies (next.side_dbfs, calmWeights);
        if (continuing)
        {
            pendingSnapshot = nextLayout;
            havePendingSnapshot = true;
            curveDirty = true;
            numericDirty = true;
            return;
        }
        snapshot = nextLayout;
        displayedPre = pendingPre;
        displayedPost = pendingPost;
        readoutPre = pendingPre;
        readoutPost = pendingPost;
    }
    else
    {
        snapshot = nextLayout;
        displayedPre.fill (0.0f);
        displayedPost.fill (0.0f);
        readoutPre.fill (0.0f);
        readoutPost.fill (0.0f);
    }
    displayedDelta.fill (0.0f);
    readoutDelta.fill (0.0f);
    pendingPre = displayedPre;
    pendingPost = displayedPost;
    pendingDelta.fill (0.0f);
    curveDirty = false;
    numericDirty = false;
    havePendingSnapshot = valid;
    pendingSnapshot = nextLayout;
    repaint();
}
}
