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
        return view.has_data != 0
            && view.status == KIRIN_SPECTRUM_ACTIVE
            && view.sample_rate >= 8000u
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

void SpectrumComponent::setSnapshot (const KirinSpectrumView& next)
{
    if (haveSnapshot && std::memcmp (&snapshot, &next, sizeof (snapshot)) == 0)
        return;
    const bool previousValid = haveSnapshot && validSnapshot (snapshot);
    const bool nextValid = validSnapshot (next);
    const bool layoutChanged = previousValid && nextValid
        && (snapshot.sample_rate != next.sample_rate
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

void SpectrumComponent::queueSnapshot (const KirinSpectrumView& next)
{
    if (havePendingSnapshot
        && std::memcmp (&pendingSnapshot, &next, sizeof (next)) == 0)
        return;
    const bool previousValid = havePendingSnapshot && validSnapshot (pendingSnapshot);
    const bool nextValid = validSnapshot (next);
    const bool layoutChanged = previousValid && nextValid
        && (pendingSnapshot.sample_rate != next.sample_rate
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
            modeActionNotice = monoSide ? "SIDE — MONO" : "MODE —";
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
            modeActionNotice = "MARK —";
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
    const float normalised = juce::jlimit (0.0f, 1.0f,
        (event.position.x - plot.getX()) / plot.getWidth());
    focusFrequencyHz = spectrum_geometry::frequencyForNormalisedX (
        normalised, snapshot.min_hz, snapshot.max_hz);
    hoverNormalisedX = normalised;
    repaint();
}

void SpectrumComponent::paint (juce::Graphics& g)
{
    const spectrum_chrome::PaintState state {
        snapshot, displayedPre, displayedPost, displayedDelta,
        readoutPre, readoutPost, readoutDelta, markedDelta,
        focusTrail.get(), modeActionNotice,
        haveSnapshot, haveSnapshot && validSnapshot (snapshot),
        haveMark, hoverNormalisedX, focusFrequencyHz, channelMode, inputChannels
    };
    spectrum_chrome::paint (g, getLocalBounds().toFloat(), state);
}
}
