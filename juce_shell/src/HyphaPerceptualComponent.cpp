#include "HyphaPerceptualComponent.h"

#include "HyphaAnalysisUiText.h"
#include "HyphaPerceptualPainter.h"
#include "HyphaSpectrumGeometry.h"

#include <cmath>
#include <cstring>

namespace hypha
{
namespace
{
    bool validSnapshot (const KirinPerceptualView& view) noexcept
    {
        return view.has_data != 0
            && view.status == KIRIN_SPECTRUM_ACTIVE
            && view.sample_rate >= 8'000u
            && view.sample_rate % 10u == 0u
            && view.aperture_samples == view.sample_rate / 10u
            && view.channel_mode <= KIRIN_SPECTRUM_CHANNEL_SIDE
            && (view.channels == 1u || view.channels == 2u)
            && ! (view.channel_mode == KIRIN_SPECTRUM_CHANNEL_SIDE && view.channels != 2u)
            && std::isfinite (view.pre_sharpness) && view.pre_sharpness >= 0.0
            && std::isfinite (view.post_sharpness) && view.post_sharpness >= 0.0
            && std::isfinite (view.delta_sharpness)
            && view.state_epoch_samples % static_cast<int64_t> (view.aperture_samples) == 0
            && view.presentation_end_samples > view.state_epoch_samples;
    }

    bool sameSnapshot (const KirinPerceptualView& left,
                       const KirinPerceptualView& right) noexcept
    {
        return left.status == right.status
            && left.has_data == right.has_data
            && left.channel_mode == right.channel_mode
            && left.channels == right.channels
            && left.sample_rate == right.sample_rate
            && left.aperture_samples == right.aperture_samples
            && std::memcmp (&left.pre_sharpness, &right.pre_sharpness, sizeof (double)) == 0
            && std::memcmp (&left.post_sharpness, &right.post_sharpness, sizeof (double)) == 0
            && std::memcmp (&left.delta_sharpness, &right.delta_sharpness, sizeof (double)) == 0
            && left.presentation_end_samples == right.presentation_end_samples
            && left.state_epoch_samples == right.state_epoch_samples;
    }
}

PerceptualComponent::PerceptualComponent()
{
    setInterceptsMouseClicks (true, false);
    setAccessible (false);
}

void PerceptualComponent::setAnalysisOwnerNames (const juce::String& names)
{
    if (analysisOwnerNames == names)
        return;
    analysisOwnerNames = names;
    repaint();
}

void PerceptualComponent::setSnapshot (const KirinPerceptualView& next)
{
    if (haveSnapshot && sameSnapshot (snapshot, next))
        return;
    const bool previousValid = haveSnapshot && validSnapshot (snapshot);
    const bool nextValid = validSnapshot (next);
    const bool layoutChanged = previousValid && nextValid
        && (snapshot.sample_rate != next.sample_rate
            || snapshot.aperture_samples != next.aperture_samples
            || snapshot.channel_mode != next.channel_mode
            || snapshot.channels != next.channels
            || snapshot.state_epoch_samples != next.state_epoch_samples);
    if (! nextValid || layoutChanged)
        history.clear();
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
        const auto appended = history.append (
            next.presentation_end_samples, next.sample_rate,
            next.aperture_samples, next.pre_sharpness,
            next.post_sharpness, next.delta_sharpness);
        if (appended == perceptual_history::AppendResult::gapAppended)
        {
            history.clear();
            history.append (next.presentation_end_samples, next.sample_rate,
                            next.aperture_samples, next.pre_sharpness,
                            next.post_sharpness, next.delta_sharpness);
        }
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

void PerceptualComponent::setBatch (const KirinPerceptualBatch& batch)
{
    if (batch.count > KIRIN_PERCEPTUAL_BATCH_CAPACITY)
        return;
    const auto& next = batch.latest;
    const bool nextValid = validSnapshot (next);
    if (nextValid != (batch.count > 0u))
        return;
    for (uint32_t index = 0u; index < batch.count; ++index)
    {
        const auto& frame = batch.frames[index];
        if (! validSnapshot (frame)
            || frame.sample_rate != next.sample_rate
            || frame.aperture_samples != next.aperture_samples
            || frame.channel_mode != next.channel_mode
            || frame.channels != next.channels
            || frame.state_epoch_samples != next.state_epoch_samples
            || (index > 0u && frame.presentation_end_samples
                <= batch.frames[index - 1u].presentation_end_samples))
            return;
    }
    if (nextValid
        && batch.frames[batch.count - 1u].presentation_end_samples
            != next.presentation_end_samples)
        return;

    const bool previousValid = havePendingSnapshot && validSnapshot (pendingSnapshot);
    const bool layoutChanged = previousValid && nextValid
        && (pendingSnapshot.sample_rate != next.sample_rate
            || pendingSnapshot.aperture_samples != next.aperture_samples
            || pendingSnapshot.channel_mode != next.channel_mode
            || pendingSnapshot.channels != next.channels
            || pendingSnapshot.state_epoch_samples != next.state_epoch_samples);
    if (! nextValid || layoutChanged)
        history.clear();

    bool appendedAny = false;
    if (nextValid)
    {
        for (uint32_t index = 0u; index < batch.count; ++index)
        {
            const auto& frame = batch.frames[index];
            if (! history.empty()
                && frame.presentation_end_samples <= history.newestEndpoint())
                continue;
            const auto appended = history.append (
                frame.presentation_end_samples, frame.sample_rate,
                frame.aperture_samples, frame.pre_sharpness,
                frame.post_sharpness, frame.delta_sharpness);
            if (appended == perceptual_history::AppendResult::gapAppended)
            {
                // A true overrun is recorded internally by the endpoint jump, but the user gets
                // one clean new run instead of a screen full of disconnected fragments.
                history.clear();
                history.append (frame.presentation_end_samples, frame.sample_rate,
                                frame.aperture_samples, frame.pre_sharpness,
                                frame.post_sharpness, frame.delta_sharpness);
            }
            appendedAny = true;
        }
    }

    const bool firstPresentation = ! haveSnapshot || ! previousValid || layoutChanged;
    pendingSnapshot = next;
    havePendingSnapshot = true;
    haveSnapshot = true;
    if (next.channel_mode <= KIRIN_SPECTRUM_CHANNEL_SIDE)
        channelMode = next.channel_mode;
    if (next.channels == 1u || next.channels == 2u)
        inputChannels = next.channels;
    modeActionNotice.clear();
    modeActionNoticeUntilMs = 0.0;
    if (firstPresentation || ! nextValid)
    {
        snapshot = next;
        curveDirty = false;
        numericDirty = false;
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        lastCurvePresentationMs = nowMs;
        lastNumericPresentationMs = nowMs;
        ++curvePresentationCount;
        ++numericPresentationCount;
        repaint();
        return;
    }
    curveDirty = curveDirty || appendedAny;
    numericDirty = numericDirty || ! sameSnapshot (snapshot, next);
}

void PerceptualComponent::clearSnapshot()
{
    snapshot = {};
    pendingSnapshot = {};
    history.clear();
    haveSnapshot = false;
    havePendingSnapshot = false;
    curveDirty = false;
    numericDirty = false;
    modeActionNotice.clear();
    modeActionNoticeUntilMs = 0.0;
    lastCurvePresentationMs = 0.0;
    lastNumericPresentationMs = 0.0;
    repaint();
}

void PerceptualComponent::presentationTick()
{
    presentationTickAt (juce::Time::getMillisecondCounterHiRes());
}

void PerceptualComponent::presentationTickAt (double nowMs)
{
    bool needsRepaint = false;
    const double curveIntervalMs = 1'000.0
        / (double) ui_contract::perceptualCurvePresentationHz;
    const double numericIntervalMs = 1'000.0
        / (double) ui_contract::analysisNumericPresentationHz;
    if (curveDirty && nowMs - lastCurvePresentationMs >= curveIntervalMs)
    {
        curveDirty = false;
        lastCurvePresentationMs = nowMs - lastCurvePresentationMs > 2.0 * curveIntervalMs
                                ? nowMs : lastCurvePresentationMs + curveIntervalMs;
        ++curvePresentationCount;
        needsRepaint = true;
    }
    if (numericDirty && havePendingSnapshot
        && nowMs - lastNumericPresentationMs >= numericIntervalMs)
    {
        snapshot = pendingSnapshot;
        numericDirty = false;
        lastNumericPresentationMs = nowMs - lastNumericPresentationMs > 2.0 * numericIntervalMs
                                  ? nowMs : lastNumericPresentationMs + numericIntervalMs;
        ++numericPresentationCount;
        needsRepaint = true;
    }
    if (modeActionNotice.isNotEmpty() && nowMs >= modeActionNoticeUntilMs)
    {
        modeActionNotice.clear();
        modeActionNoticeUntilMs = 0.0;
        needsRepaint = true;
    }
    if (needsRepaint)
        repaint();
}

void PerceptualComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto bounds = getLocalBounds().toFloat();
    const float scale = spectrum_geometry::visualScaleFor (bounds);
    const auto outer = spectrum_geometry::plotBoundsFor (bounds);
    juce::String tip = analysis_ui::sharpnessDeltaTooltip();
    for (size_t index = 0; index < ui_contract::spectrumChannelModeWidths.size(); ++index)
        if (spectrum_geometry::channelModeBoundsFor (index, outer, scale)
                .contains (event.position))
            tip = analysis_ui::channelModeTooltip (static_cast<uint8_t> (index));
    if (tip != getTooltip())
        setTooltip (tip);
}

void PerceptualComponent::mouseExit (const juce::MouseEvent&)
{
    if (getTooltip().isNotEmpty())
        setTooltip ({});
}

void PerceptualComponent::mouseDown (const juce::MouseEvent& event)
{
    const auto bounds = getLocalBounds().toFloat();
    const float scale = spectrum_geometry::visualScaleFor (bounds);
    const auto outerPlot = spectrum_geometry::plotBoundsFor (bounds);
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
        history.clear();
        haveSnapshot = false;
        havePendingSnapshot = false;
        curveDirty = false;
        numericDirty = false;
        channelMode = requestedMode;
        repaint();
        return;
    }
}

void PerceptualComponent::paint (juce::Graphics& g)
{
    const perceptual_painter::PaintState state {
        snapshot, history, modeActionNotice, analysisOwnerNames,
        haveSnapshot, haveSnapshot && validSnapshot (snapshot),
        channelMode, inputChannels
    };
    perceptual_painter::paint (g, getLocalBounds().toFloat(), state);
}
}
