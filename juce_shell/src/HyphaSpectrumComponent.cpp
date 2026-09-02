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

    bool validSnapshot (const KirinSpectrumView& view, bool absoluteObservation) noexcept
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
        const bool validLayout = view.sample_rate >= 8000u
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
            && finiteBins (view.post_dbfs);
        if (! validLayout)
            return false;
        if (absoluteObservation)
            return view.post_has_data != 0;
        return view.has_data != 0
            && view.status == KIRIN_SPECTRUM_ACTIVE
            && finiteBins (view.pre_dbfs)
            && finiteBins (view.display_db);
    }

    bool sameFloatBits (float left, float right) noexcept
    {
        return std::memcmp (&left, &right, sizeof (float)) == 0;
    }

    bool sameLayoutDefinition (const KirinSpectrumView& left,
                               const KirinSpectrumView& right) noexcept
    {
        return left.sample_rate == right.sample_rate
            && left.aperture_samples == right.aperture_samples
            && left.fft_size == right.fft_size
            && sameFloatBits (left.approximate_below_hz, right.approximate_below_hz)
            && left.channel_mode == right.channel_mode
            && left.channels == right.channels
            && sameFloatBits (left.min_hz, right.min_hz)
            && sameFloatBits (left.max_hz, right.max_hz);
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

void SpectrumComponent::setGuideFrequencyOverlay (
    const guide_frequency::Overlay& next)
{
    if (guide_frequency::equivalent (guideOverlay, next))
        return;
    guideOverlay = next;
    repaint();
}

void SpectrumComponent::setAbsoluteObservation (bool absolute)
{
    if (absoluteObservation == absolute)
        return;
    absoluteObservation = absolute;
    clearInteractionState();
    if (haveSnapshot)
    {
        const auto retained = snapshot;
        haveSnapshot = false;
        havePendingSnapshot = false;
        setSnapshot (retained);
    }
    else
        repaint();
}

void SpectrumComponent::setSnapshot (const KirinSpectrumView& next)
{
    if (haveSnapshot && std::memcmp (&snapshot, &next, sizeof (snapshot)) == 0)
        return;
    const bool nextValid = validSnapshot (next, absoluteObservation);
    const bool layoutChanged = nextValid && haveInteractionDefinition
        && ! sameLayoutDefinition (interactionDefinition, next);
    if (layoutChanged)
        clearInteractionState();
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
        interactionDefinition = next;
        haveInteractionDefinition = true;
        const auto calmWeights = spectrum_presentation::lowFrequencyCalmWeights<
            KIRIN_SPECTRUM_BAND_COUNT> (snapshot.min_hz, snapshot.max_hz);
        if (snapshot.has_data != 0)
            displayedPre = spectrum_presentation::calmLowFrequencies (
                snapshot.pre_dbfs, calmWeights);
        else
            displayedPre.fill (0.0f);
        displayedPost = spectrum_presentation::calmLowFrequencies (
            snapshot.post_dbfs, calmWeights);
        if (snapshot.has_data != 0)
            displayedDelta = spectrum_presentation::calmLowFrequencies (
                snapshot.display_db, calmWeights);
        else
            displayedDelta.fill (0.0f);
        readoutPre = displayedPre;
        readoutPost = displayedPost;
        readoutDelta = displayedDelta;
        pendingPre = displayedPre;
        pendingPost = displayedPost;
        pendingDelta = displayedDelta;
        absoluteHistory.append (snapshot);
        if (! absoluteObservation)
        {
            if (focusTrail == nullptr)
                focusTrail = std::make_unique<spectrum_focus::FocusTrailHistory>();
            focusTrail->append (snapshot.presentation_end_samples,
                                snapshot.sample_rate,
                                displayedDelta);
        }
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
    const bool latestValid = validSnapshot (batch.latest, absoluteObservation);
    // The FFI batch may carry POST-only history while this component is presenting exact delta.
    // That history is invalid for the current target, but its latest status is still factual and
    // must replace a previously active delta instead of being rejected with the batch.
    if (! latestValid || batch.count == 0u)
    {
        setSnapshot (batch.latest);
        return;
    }
    for (uint32_t index = 0u; index < batch.count; ++index)
    {
        const auto& frame = batch.frames[index];
        if (! validSnapshot (frame, absoluteObservation)
            || ! sameLayoutDefinition (frame, batch.latest)
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

    const bool pendingValid = havePendingSnapshot
                           && validSnapshot (pendingSnapshot, absoluteObservation);
    const bool definitionChanged = pendingValid
        && ! sameLayoutDefinition (pendingSnapshot, batch.latest);
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
    const bool previousValid = havePendingSnapshot
                            && validSnapshot (pendingSnapshot, absoluteObservation);
    const bool nextValid = validSnapshot (next, absoluteObservation);
    const bool layoutChanged = previousValid && nextValid
        && ! sameLayoutDefinition (pendingSnapshot, next);
    if (! haveSnapshot || ! previousValid || ! nextValid || layoutChanged)
    {
        setSnapshot (next);
        return;
    }

    const auto calmWeights = spectrum_presentation::lowFrequencyCalmWeights<
        KIRIN_SPECTRUM_BAND_COUNT> (next.min_hz, next.max_hz);
    if (next.has_data != 0)
        pendingPre = spectrum_presentation::calmLowFrequencies (next.pre_dbfs, calmWeights);
    else
        pendingPre.fill (0.0f);
    pendingPost = spectrum_presentation::calmLowFrequencies (next.post_dbfs, calmWeights);
    if (next.has_data != 0)
        pendingDelta = spectrum_presentation::calmLowFrequencies (next.display_db, calmWeights);
    else
        pendingDelta.fill (0.0f);
    absoluteHistory.append (next);
    if (! absoluteObservation)
    {
        if (focusTrail == nullptr)
            focusTrail = std::make_unique<spectrum_focus::FocusTrailHistory>();
        focusTrail->append (next.presentation_end_samples, next.sample_rate, pendingDelta);
    }
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
    clearInteractionState();
    haveSnapshot = false;
    havePendingSnapshot = false;
    curveDirty = false;
    numericDirty = false;
    hoverNeedsRepaint = false;
    modeActionNotice.clear();
    modeActionNoticeUntilMs = 0.0;
    lastCurvePresentationMs = 0.0;
    lastNumericPresentationMs = 0.0;
    guideOverlay = {};
    absoluteHistory.clear();
    repaint();
}

void SpectrumComponent::clearInteractionState() noexcept
{
    interactionDefinition = {};
    markedDelta.fill (0.0f);
    focusTrail.reset();
    haveInteractionDefinition = false;
    haveMark = false;
    hoverNormalisedX = -1.0f;
    focusFrequencyHz = -1.0f;
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
        clearInteractionState();
        haveSnapshot = false;
        havePendingSnapshot = false;
        curveDirty = false;
        numericDirty = false;
        channelMode = requestedMode;
        repaint();
        return;
    }

    const auto markBounds = spectrum_geometry::markBoundsFor (outerPlot, scale);
    if (! absoluteObservation
        && focusFrequencyHz <= 0.0f && hoverNormalisedX < 0.0f
        && markBounds.contains (event.position))
    {
        if (haveMark && spectrum_geometry::markClearBoundsFor (
                markBounds, scale).contains (event.position))
        {
            markedDelta.fill (0.0f);
            haveMark = false;
        }
        else if (haveSnapshot && validSnapshot (snapshot, absoluteObservation))
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

    if (! haveSnapshot || ! validSnapshot (snapshot, absoluteObservation))
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
        guideOverlay,
        &absoluteHistory, absoluteHistory.peakHold(), absoluteObservation,
        haveSnapshot, haveSnapshot && validSnapshot (snapshot, absoluteObservation),
        haveMark, hoverNormalisedX, focusFrequencyHz, channelMode, inputChannels
    };
    spectrum_chrome::paint (g, getLocalBounds().toFloat(), state);
}
}
