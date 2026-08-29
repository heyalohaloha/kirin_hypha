#include "HyphaSpectrumComponent.h"

#include "HyphaSpectrumChromePainter.h"
#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumPresentation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hypha
{
namespace
{
    template <size_t Size>
    bool finiteBins (const float (&values)[Size]) noexcept
    {
        return std::all_of (std::begin (values), std::end (values),
                            [] (float value) { return std::isfinite (value); });
    }

    bool validSnapshot (const KirinSpectrumView& view) noexcept
    {
        const uint64_t apertureNumerator = static_cast<uint64_t> (view.sample_rate) * 4'096u
                                         + 24'000u;
        const uint32_t expectedAperture = static_cast<uint32_t> (
            apertureNumerator / 48'000u);
        uint32_t expectedFft = 1u;
        while (expectedFft < expectedAperture * 2u && expectedFft <= 65'536u)
            expectedFft <<= 1u;
        const float expectedApproximateBelow = expectedAperture > 0u
            ? 3.0f * static_cast<float> (view.sample_rate)
                / static_cast<float> (expectedAperture)
            : 0.0f;
        return view.has_data != 0
            && view.status == KIRIN_SPECTRUM_ACTIVE
            && view.sample_rate >= 8000u
            && view.sample_rate <= 384000u
            && view.aperture_samples == expectedAperture
            && view.fft_size == expectedFft
            && std::isfinite (view.approximate_below_hz)
            && std::abs (view.approximate_below_hz - expectedApproximateBelow) < 0.001f
            && std::isfinite (view.min_hz)
            && std::isfinite (view.max_hz)
            && view.min_hz > 0.0f
            && view.max_hz > view.min_hz
            && view.channel_mode <= KIRIN_SPECTRUM_CHANNEL_SIDE
            && (view.channels == 1u || view.channels == 2u)
            && ! (view.channel_mode == KIRIN_SPECTRUM_CHANNEL_SIDE && view.channels != 2u)
            && finiteBins (view.pre_dbfs)
            && finiteBins (view.post_dbfs)
            && finiteBins (view.display_db);
    }

    bool sameFloatBits (float left, float right) noexcept
    {
        return std::memcmp (&left, &right, sizeof (float)) == 0;
    }
}

SpectrumComponent::SpectrumComponent()
{
    setInterceptsMouseClicks (true, false);
    setAccessible (false);
}

void SpectrumComponent::setAnalysisOwnerNames (const juce::String& names)
{
    if (analysisOwnerNames == names)
        return;
    analysisOwnerNames = names;
    repaint();
}

void SpectrumComponent::setSnapshot (const KirinSpectrumView& next)
{
    if (haveSnapshot && std::memcmp (&snapshot, &next, sizeof (snapshot)) == 0)
        return;
    const bool previousValid = haveSnapshot && validSnapshot (snapshot);
    const bool nextValid = validSnapshot (next);
    const bool layoutChanged = previousValid && nextValid
        && (snapshot.sample_rate != next.sample_rate
            || snapshot.aperture_samples != next.aperture_samples
            || snapshot.fft_size != next.fft_size
            || ! sameFloatBits (snapshot.approximate_below_hz,
                                next.approximate_below_hz)
            || snapshot.channel_mode != next.channel_mode
            || snapshot.channels != next.channels
            || ! sameFloatBits (snapshot.min_hz, next.min_hz)
            || ! sameFloatBits (snapshot.max_hz, next.max_hz));
    if (! nextValid || layoutChanged)
    {
        focusFrequencyHz = -1.0f;
        haveMark = false;
        markedDelta.fill (0.0f);
        focusTrail.reset();
    }
    snapshot = next;
    haveSnapshot = true;
    if (next.channel_mode <= KIRIN_SPECTRUM_CHANNEL_SIDE)
        channelMode = next.channel_mode;
    if (next.channels == 1u || next.channels == 2u)
        inputChannels = next.channels;
    modeActionNotice.clear();
    modeActionNoticeUntilMs = 0.0;
    if (nextValid)
    {
        const auto calmWeights = spectrum_presentation::lowFrequencyCalmWeights<
            KIRIN_SPECTRUM_BAND_COUNT> (snapshot.min_hz, snapshot.max_hz);
        displayedPre = spectrum_presentation::calmLowFrequencies (
            snapshot.pre_dbfs, calmWeights);
        displayedPost = spectrum_presentation::calmLowFrequencies (
            snapshot.post_dbfs, calmWeights);
        displayedDelta = spectrum_presentation::calmLowFrequencies (
            snapshot.display_db, calmWeights);
        readoutPre = displayedPre;
        readoutPost = displayedPost;
        readoutDelta = displayedDelta;
        pendingPre = displayedPre;
        pendingPost = displayedPost;
        pendingDelta = displayedDelta;
        if (focusTrail == nullptr)
            focusTrail = std::make_unique<spectrum_focus::FocusTrailHistory>();
        focusTrail->append (snapshot.presentation_end_samples,
                            snapshot.sample_rate,
                            displayedDelta);
    }
    else
    {
        displayedPre.fill (0.0f);
        displayedPost.fill (0.0f);
        displayedDelta.fill (0.0f);
        readoutPre.fill (0.0f);
        readoutPost.fill (0.0f);
        readoutDelta.fill (0.0f);
        pendingPre.fill (0.0f);
        pendingPost.fill (0.0f);
        pendingDelta.fill (0.0f);
    }
    pendingSnapshot = next;
    havePendingSnapshot = true;
    curveDirty = false;
    numericDirty = false;
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    lastCurvePresentationMs = nowMs;
    lastNumericPresentationMs = nowMs;
    repaint();
}

void SpectrumComponent::setBatch (const KirinSpectrumBatch& batch)
{
    if (batch.count > KIRIN_SPECTRUM_BATCH_CAPACITY)
        return;
    const bool latestValid = validSnapshot (batch.latest);
    if (latestValid != (batch.count > 0u))
        return;
    if (! latestValid)
    {
        setSnapshot (batch.latest);
        return;
    }
    for (uint32_t index = 0u; index < batch.count; ++index)
    {
        const auto& frame = batch.frames[index];
        if (! validSnapshot (frame)
            || frame.sample_rate != batch.latest.sample_rate
            || frame.aperture_samples != batch.latest.aperture_samples
            || frame.fft_size != batch.latest.fft_size
            || ! sameFloatBits (frame.approximate_below_hz,
                                batch.latest.approximate_below_hz)
            || frame.channel_mode != batch.latest.channel_mode
            || frame.channels != batch.latest.channels
            || ! sameFloatBits (frame.min_hz, batch.latest.min_hz)
            || ! sameFloatBits (frame.max_hz, batch.latest.max_hz)
            || (index > 0u && frame.presentation_end_samples
                <= batch.frames[index - 1u].presentation_end_samples))
        {
            return;
        }
    }
    if (batch.frames[batch.count - 1u].presentation_end_samples
        != batch.latest.presentation_end_samples)
    {
        return;
    }

    const bool pendingValid = havePendingSnapshot && validSnapshot (pendingSnapshot);
    const bool definitionChanged = pendingValid
        && (pendingSnapshot.sample_rate != batch.latest.sample_rate
            || pendingSnapshot.aperture_samples != batch.latest.aperture_samples
            || pendingSnapshot.fft_size != batch.latest.fft_size
            || ! sameFloatBits (pendingSnapshot.approximate_below_hz,
                                batch.latest.approximate_below_hz)
            || pendingSnapshot.channel_mode != batch.latest.channel_mode
            || pendingSnapshot.channels != batch.latest.channels
            || ! sameFloatBits (pendingSnapshot.min_hz, batch.latest.min_hz)
            || ! sameFloatBits (pendingSnapshot.max_hz, batch.latest.max_hz));
    const bool presentationMovedBackwards = pendingValid
        && batch.latest.presentation_end_samples
            < pendingSnapshot.presentation_end_samples;
    uint32_t first = 0u;
    if (! pendingValid || definitionChanged || presentationMovedBackwards)
    {
        setSnapshot (batch.frames[0]);
        first = 1u;
    }
    for (uint32_t index = first; index < batch.count; ++index)
    {
        if (batch.frames[index].presentation_end_samples
            > pendingSnapshot.presentation_end_samples)
        {
            queueSnapshot (batch.frames[index]);
        }
    }
}

void SpectrumComponent::queueSnapshot (const KirinSpectrumView& next)
{
    if (havePendingSnapshot
        && std::memcmp (&pendingSnapshot, &next, sizeof (next)) == 0)
        return;
    const bool previousValid = havePendingSnapshot && validSnapshot (pendingSnapshot);
    const bool nextValid = validSnapshot (next);
    const bool layoutChanged = previousValid && nextValid
        && (pendingSnapshot.sample_rate != next.sample_rate
            || pendingSnapshot.aperture_samples != next.aperture_samples
            || pendingSnapshot.fft_size != next.fft_size
            || ! sameFloatBits (pendingSnapshot.approximate_below_hz,
                                next.approximate_below_hz)
            || pendingSnapshot.channel_mode != next.channel_mode
            || pendingSnapshot.channels != next.channels
            || ! sameFloatBits (pendingSnapshot.min_hz, next.min_hz)
            || ! sameFloatBits (pendingSnapshot.max_hz, next.max_hz));
    if (! haveSnapshot || ! previousValid || ! nextValid || layoutChanged)
    {
        setSnapshot (next);
        return;
    }

    const auto calmWeights = spectrum_presentation::lowFrequencyCalmWeights<
        KIRIN_SPECTRUM_BAND_COUNT> (next.min_hz, next.max_hz);
    pendingPre = spectrum_presentation::calmLowFrequencies (next.pre_dbfs, calmWeights);
    pendingPost = spectrum_presentation::calmLowFrequencies (next.post_dbfs, calmWeights);
    pendingDelta = spectrum_presentation::calmLowFrequencies (next.display_db, calmWeights);
    if (focusTrail == nullptr)
        focusTrail = std::make_unique<spectrum_focus::FocusTrailHistory>();
    focusTrail->append (next.presentation_end_samples, next.sample_rate, pendingDelta);
    pendingSnapshot = next;
    havePendingSnapshot = true;
    curveDirty = true;
    numericDirty = true;
    if (next.channel_mode <= KIRIN_SPECTRUM_CHANNEL_SIDE)
        channelMode = next.channel_mode;
    if (next.channels == 1u || next.channels == 2u)
        inputChannels = next.channels;
}

void SpectrumComponent::clearSnapshot()
{
    snapshot = {};
    pendingSnapshot = {};
    displayedPre.fill (0.0f);
    displayedPost.fill (0.0f);
    displayedDelta.fill (0.0f);
    readoutPre.fill (0.0f);
    readoutPost.fill (0.0f);
    readoutDelta.fill (0.0f);
    pendingPre.fill (0.0f);
    pendingPost.fill (0.0f);
    pendingDelta.fill (0.0f);
    markedDelta.fill (0.0f);
    focusTrail.reset();
    haveSnapshot = false;
    havePendingSnapshot = false;
    curveDirty = false;
    numericDirty = false;
    haveMark = false;
    hoverNormalisedX = -1.0f;
    focusFrequencyHz = -1.0f;
    hoverNeedsRepaint = false;
    modeActionNotice.clear();
    modeActionNoticeUntilMs = 0.0;
    lastCurvePresentationMs = 0.0;
    lastNumericPresentationMs = 0.0;
    repaint();
}

void SpectrumComponent::presentationTick()
{
    presentationTickAt (juce::Time::getMillisecondCounterHiRes());
}

void SpectrumComponent::presentationTickAt (double nowMs)
{
    const bool noticeExpired = modeActionNotice.isNotEmpty()
        && nowMs >= modeActionNoticeUntilMs;
    bool needsRepaint = hoverNeedsRepaint || noticeExpired;
    hoverNeedsRepaint = false;
    if (curveDirty && havePendingSnapshot
        && nowMs - lastCurvePresentationMs >= 1'000.0
            / (double) ui_contract::spectrumCurvePresentationHz)
    {
        const double intervalMs = 1'000.0
            / (double) ui_contract::spectrumCurvePresentationHz;
        snapshot = pendingSnapshot;
        displayedPre = pendingPre;
        displayedPost = pendingPost;
        displayedDelta = pendingDelta;
        curveDirty = false;
        lastCurvePresentationMs = nowMs - lastCurvePresentationMs > 2.0 * intervalMs
                                ? nowMs : lastCurvePresentationMs + intervalMs;
        needsRepaint = true;
    }
    if (numericDirty && havePendingSnapshot
        && nowMs - lastNumericPresentationMs >= 1'000.0
            / (double) ui_contract::analysisNumericPresentationHz)
    {
        const double intervalMs = 1'000.0
            / (double) ui_contract::analysisNumericPresentationHz;
        readoutPre = pendingPre;
        readoutPost = pendingPost;
        readoutDelta = pendingDelta;
        numericDirty = false;
        lastNumericPresentationMs = nowMs - lastNumericPresentationMs > 2.0 * intervalMs
                                  ? nowMs : lastNumericPresentationMs + intervalMs;
        needsRepaint = true;
    }
    if (noticeExpired)
    {
        modeActionNotice.clear();
        modeActionNoticeUntilMs = 0.0;
    }
    if (needsRepaint)
        repaint();
}

void SpectrumComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto bounds = getLocalBounds().toFloat();
    const float scale = spectrum_geometry::visualScaleFor (bounds);
    const auto outerPlot = spectrum_geometry::plotBoundsFor (bounds);
    const auto plot = spectrum_geometry::dataPlotBoundsFor (bounds);
    const float controlsBottom = outerPlot.getY()
        + (float) (ui_contract::spectrumChannelModeTop
                 + ui_contract::spectrumChannelModeHeight) * scale;
    const auto position = event.position;
    const float next = plot.contains (position) && position.y >= controlsBottom
                         ? juce::jlimit (0.0f, 1.0f,
                                        (position.x - plot.getX()) / plot.getWidth())
                         : -1.0f;
    if (juce::approximatelyEqual (next, hoverNormalisedX))
        return;
    hoverNormalisedX = next;
    hoverNeedsRepaint = true;
}

void SpectrumComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoverNormalisedX < 0.0f)
        return;
    hoverNormalisedX = -1.0f;
    hoverNeedsRepaint = true;
}

void SpectrumComponent::mouseDown (const juce::MouseEvent& event)
{
    const auto bounds = getLocalBounds().toFloat();
    const float scale = spectrum_geometry::visualScaleFor (bounds);
    const auto outerPlot = spectrum_geometry::plotBoundsFor (bounds);
    const auto plot = spectrum_geometry::dataPlotBoundsFor (bounds);
    for (size_t index = 0; index < ui_contract::spectrumChannelModeWidths.size(); ++index)
    {
        if (! spectrum_geometry::channelModeBoundsFor (
                index, outerPlot, scale).contains (event.position))
            continue;
        const auto requestedMode = static_cast<uint8_t> (index);
        if (requestedMode == channelMode)
            return;
        const bool monoSide = requestedMode == KIRIN_SPECTRUM_CHANNEL_SIDE
                           && inputChannels == 1u;
        const bool accepted = ! monoSide && onChannelModeChange
                           && onChannelModeChange (requestedMode);
        if (! accepted)
        {
            modeActionNotice = monoSide ? "SIDE -- MONO" : "MODE --";
            modeActionNoticeUntilMs = juce::Time::getMillisecondCounterHiRes() + 1'500.0;
            repaint();
            return;
        }
        snapshot = {};
        pendingSnapshot = {};
        displayedPre.fill (0.0f);
        displayedPost.fill (0.0f);
        displayedDelta.fill (0.0f);
        readoutPre.fill (0.0f);
        readoutPost.fill (0.0f);
        readoutDelta.fill (0.0f);
        pendingPre.fill (0.0f);
        pendingPost.fill (0.0f);
        pendingDelta.fill (0.0f);
        markedDelta.fill (0.0f);
        focusTrail.reset();
        haveSnapshot = false;
        havePendingSnapshot = false;
        curveDirty = false;
        numericDirty = false;
        haveMark = false;
        hoverNormalisedX = -1.0f;
        focusFrequencyHz = -1.0f;
        channelMode = requestedMode;
        repaint();
        return;
    }

    const auto markBounds = spectrum_geometry::markBoundsFor (outerPlot, scale);
    if (focusFrequencyHz <= 0.0f && hoverNormalisedX < 0.0f
        && markBounds.contains (event.position))
    {
        if (haveMark && spectrum_geometry::markClearBoundsFor (
                markBounds, scale).contains (event.position))
        {
            markedDelta.fill (0.0f);
            haveMark = false;
        }
        else if (haveSnapshot && validSnapshot (snapshot))
        {
            markedDelta = displayedDelta;
            haveMark = true;
        }
        else
        {
            modeActionNotice = "MARK --";
            modeActionNoticeUntilMs = juce::Time::getMillisecondCounterHiRes() + 1'500.0;
        }
        repaint();
        return;
    }

    if (! haveSnapshot || ! validSnapshot (snapshot))
        return;
    const bool expanded = scale > 1.1f;
    if (focusFrequencyHz > 0.0f)
    {
        auto readout = spectrum_geometry::readoutBoundsFor (
            outerPlot, scale, expanded, true);
        if (spectrum_geometry::focusClearBoundsFor (
                readout, scale).contains (event.position))
        {
            focusFrequencyHz = -1.0f;
            hoverNormalisedX = -1.0f;
            repaint();
            return;
        }
    }
    const float controlsBottom = outerPlot.getY()
        + (float) (ui_contract::spectrumChannelModeTop
                 + ui_contract::spectrumChannelModeHeight) * scale;
    if (! plot.contains (event.position) || event.position.y < controlsBottom)
        return;
    const float normalised = spectrum_geometry::clampToBandCentreRange (
        juce::jlimit (0.0f, 1.0f,
            (event.position.x - plot.getX()) / plot.getWidth()));
    focusFrequencyHz = spectrum_geometry::frequencyForProbeNormalisedX (
        normalised, snapshot.min_hz, snapshot.max_hz);
    hoverNormalisedX = normalised;
    repaint();
}

void SpectrumComponent::paint (juce::Graphics& g)
{
    const spectrum_chrome::PaintState state {
        snapshot, displayedPre, displayedPost, displayedDelta,
        readoutPre, readoutPost, readoutDelta, markedDelta,
        focusTrail.get(), modeActionNotice, analysisOwnerNames,
        haveSnapshot, haveSnapshot && validSnapshot (snapshot),
        haveMark, hoverNormalisedX, focusFrequencyHz, channelMode, inputChannels
    };
    spectrum_chrome::paint (g, getLocalBounds().toFloat(), state);
}
}
