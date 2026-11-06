#include "HyphaObservatoryView.h"
#include <cmath>
#include <limits>

namespace hypha::observatory
{
namespace
{
double targetFor (const KirinMeterSession& meter, bool available, int channel)
{
    const auto value = available && channel < meter.channels && std::isfinite (meter.channel_vu_dbfs[channel])
        ? meter.channel_vu_dbfs[channel] : std::numeric_limits<double>::quiet_NaN();
    return std::isfinite (value) ? std::pow (10.0, value / 20.0) : 0.0;
}
}

void View::updateVuVisibility()
{
    const auto visible = hybridVuVisible() && (vuFrameTestMode || (isShowing() && getPeer() != nullptr));
    if (! vuAnimator)
        vuAnimator = std::make_unique<hybrid_vu::NeedleAnimator> (*this, [this]
        {
            if (! hybridVuVisible() || ! isShowing()) return;
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto next = hybrid_vu::vuNormalized (hybrid_vu::needleDbfs (vuAnimator->position (channel)), vuCalibration());
                if (next != vuRepaintPositions[channel])
                    for (const auto& area : hybrid_vu::needle_painter::dirtyRegions (
                             getLocalBounds(), channel, vuRepaintPositions[channel], next))
                        repaint (area);
                vuRepaintPositions[channel] = next;
            }
        }, [this] { refreshVuTargets(); });
    const bool starting = visible && ! vuAnimator->isVisible();
    if (starting)
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto amplitude = onVuLiveReading ? 0.0
                : targetFor (observatoryFrame.meter, currentFactsAvailable(), channel);
            vuRepaintPositions[channel] = hybrid_vu::vuNormalized (hybrid_vu::needleDbfs (amplitude), vuCalibration());
            vuAnimator->snap (channel, amplitude);
        }
    vuAnimator->setVisible (visible);
    if (starting && onVuLiveReading)
    {
        vuAnimator->accept (onVuLiveReading());
        for (int channel = 0; channel < 2; ++channel)
            vuRepaintPositions[channel] = hybrid_vu::vuNormalized (
                hybrid_vu::needleDbfs (vuAnimator->position (channel)), vuCalibration());
    }
}

// A live provider owns the entire needle path, including silence and interrupted callbacks.
// Never fall back to a stale public measurement when its current input is absent.
void View::refreshVuTargets()
{
    if (! vuAnimator) return;
    if (onVuLiveReading) { vuAnimator->accept (onVuLiveReading()); return; }
    // Snapshot-only render fixtures have no audio provider.
    for (int channel = 0; channel < 2; ++channel)
        vuAnimator->setTarget (channel, targetFor (observatoryFrame.meter, currentFactsAvailable(), channel));
}

void View::setVuFrameTestMode (bool enabled) { vuFrameTestMode = enabled; updateVuVisibility(); }
void View::tickVuForTest (double dt) { refreshVuTargets(); if (vuAnimator) vuAnimator->tickForTest (dt); }
double View::vuPositionForTest (int channel) const { return vuAnimator ? hybrid_vu::vuNormalized (hybrid_vu::needleDbfs (vuAnimator->position (channel)), vuCalibration()) : 0.0; }
void View::visibilityChanged() { updateVuVisibility(); }
void View::parentHierarchyChanged()
{
    if (vuAnimator && vuAnimator->isVisible()) vuAnimator->refreshHierarchy();
    updateVuVisibility();
}

void View::paintHybridVu (juce::Graphics& g)
{
    const hybrid_vu::State state {
        role, observatoryFrame.meter, watchDisplay,
        currentFactsAvailable(), cumulativeFactsAvailable(), watchDisplayAvailable,
        selectedShortTermLoudness, hostRecording, connectionText, connectionColour,
        jungleAppearance, presentationContext(), vuCalibration()
    };
    updateVuVisibility();
    // Offscreen rendering uses the published values and has no dependency on frame timing.
    if (! vuAnimator->isVisible()) { hybrid_vu::paint (g, getLocalBounds(), state); return; }
    // Only the frame callback consumes live state. Paint must use exactly the position for
    // which that callback invalidated the swept needle, even if audio published again meanwhile.
    // The cache is drawn at the pixels it lands on: the editor's magnification and a Retina or
    // high-DPI display both count, so the face stays as sharp as the needle drawn over it.
    vuLayerCache.paint (g, getLocalBounds(), state,
                        g.getInternalContext().getPhysicalPixelScaleFactor(),
                        hybrid_vu::vuNormalized (hybrid_vu::needleDbfs (vuAnimator->position (0)), vuCalibration()),
                        hybrid_vu::vuNormalized (hybrid_vu::needleDbfs (vuAnimator->position (1)), vuCalibration()));
}
}
