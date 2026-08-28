#include "HyphaPerceptualComponent.h"

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
            && std::isfinite (view.delta_sharpness);
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
            && left.presentation_end_samples == right.presentation_end_samples;
    }
}

PerceptualComponent::PerceptualComponent()
{
    setInterceptsMouseClicks (true, false);
    setAccessible (false);
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
            || snapshot.channels != next.channels);
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
        history.append (next.presentation_end_samples, next.sample_rate,
                        next.aperture_samples, next.pre_sharpness,
                        next.post_sharpness, next.delta_sharpness);
    repaint();
}

void PerceptualComponent::clearSnapshot()
{
    snapshot = {};
    history.clear();
    haveSnapshot = false;
    modeActionNotice.clear();
    modeActionNoticeUntilMs = 0.0;
    repaint();
}

void PerceptualComponent::presentationTick()
{
    if (modeActionNotice.isEmpty()
        || juce::Time::getMillisecondCounterHiRes() < modeActionNoticeUntilMs)
        return;
    modeActionNotice.clear();
    modeActionNoticeUntilMs = 0.0;
    repaint();
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
            modeActionNotice = monoSide ? "SIDE — MONO" : "MODE —";
            modeActionNoticeUntilMs = juce::Time::getMillisecondCounterHiRes() + 1'500.0;
            repaint();
            return;
        }
        snapshot = {};
        history.clear();
        haveSnapshot = false;
        channelMode = requestedMode;
        repaint();
        return;
    }
}

void PerceptualComponent::paint (juce::Graphics& g)
{
    const perceptual_painter::PaintState state {
        snapshot, history, modeActionNotice,
        haveSnapshot, haveSnapshot && validSnapshot (snapshot),
        channelMode, inputChannels
    };
    perceptual_painter::paint (g, getLocalBounds().toFloat(), state);
}
}
