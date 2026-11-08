#include "HyphaSpectrumComponent.h"
#include "HyphaAnalysisUiText.h"

#include "HyphaSpectrumPresentation.h"
#include "HyphaSpectrumScope.h"

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
    return view.status == KIRIN_SPECTRUM_ACTIVE
        && view.has_data == 1u
        && view.channels == 2u
        && view.sample_rate >= 8'000u
        && view.sample_rate <= 384'000u
        && spectrum_scope::validLayout (view.sample_rate, view.aperture_samples, view.fft_size,
                                        view.approximate_below_hz)
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
    if (psbObservation) setTooltip (analysis_ui::psbOverviewTooltip (absolute));
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

void SpectrumComponent::setLowScope (bool low)
{
    if (lowScope == low)
        return;
    clearSnapshot();
    lowScope = low;
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

void SpectrumComponent::setMidSideSnapshot (const KirinMidSideSpectrumView& next, uint64_t continuity)
{
    const bool valid = validMidSideSnapshot (next);
    const bool continuing = midSideObservation && midSideSnapshotValid && valid
                         && sameMidSideLayout (midSideSnapshot, next)
                         && next.presentation_end_samples >= midSideSnapshot.presentation_end_samples
                         && (continuity != 0 ? continuity == midSideContinuity
                             : midSideContinuity == 0
                               && next.presentation_end_samples - midSideSnapshot.presentation_end_samples
                                  <= static_cast<int64_t> (next.sample_rate / 20u))
                         && ! resumeCurve;
    if (! continuing)
        clearInteractionState();
    resumeCurve = false;
    midSideSnapshotValid = valid;
    midSideSnapshot = next;
    midSideContinuity = continuity;
    midSideObservation = true;
    absoluteObservation = true;
    channelMode = KIRIN_SPECTRUM_SELECTION_MID_SIDE;
    inputChannels = next.channels;
    const auto nextLayout = layoutView (next);
    haveSnapshot = true;
    absoluteHistory.clear();
    if (valid)
    {
        std::copy (std::begin (next.mid_dbfs), std::end (next.mid_dbfs), pendingPre.begin());
        std::copy (std::begin (next.side_dbfs), std::end (next.side_dbfs), pendingPost.begin());
        if (continuing)
        {
            pendingSnapshot = nextLayout;
            havePendingSnapshot = true;
            curveDirty = true;
            numericDirty = true;
            return;
        }
        if (average != Average::live)
            lastCurvePresentationMs = lastNumericPresentationMs = juce::Time::getMillisecondCounterHiRes();
        lastCurveUpdateMs = juce::Time::getMillisecondCounterHiRes();
        snapshot = nextLayout;
        displayedPre = spectrum_display::spatial (pendingPre, next.min_hz, next.max_hz, displaySettings[display_settings::smooth]);
        displayedPost = spectrum_display::spatial (pendingPost, next.min_hz, next.max_hz, displaySettings[display_settings::smooth]);
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
    if (! valid) { pendingPre = displayedPre; pendingPost = displayedPost; }
    pendingDelta.fill (0.0f);
    curveDirty = false;
    numericDirty = false;
    havePendingSnapshot = valid;
    pendingSnapshot = nextLayout;
    repaint();
}
}
