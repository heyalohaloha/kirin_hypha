#include "HyphaObservatoryView.h"
#include <cmath>
#include <limits>

namespace hypha::observatory
{
namespace
{
float targetFor (const KirinMeterSession& meter, bool available, int channel, int calibration)
{
    const auto value = available && channel < meter.channels && std::isfinite (meter.channel_vu_dbfs[channel])
        ? meter.channel_vu_dbfs[channel] : std::numeric_limits<double>::quiet_NaN();
    return hybrid_vu::vuNormalized (value, calibration);
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
                const auto next = static_cast<float> (vuAnimator->position (channel));
                if (next != vuRepaintPositions[channel])
                    for (const auto& area : hybrid_vu::needle_painter::dirtyRegions (
                             getLocalBounds(), channel, vuRepaintPositions[channel], next))
                        repaint (area);
                vuRepaintPositions[channel] = next;
            }
        });
    if (visible && ! vuAnimator->isVisible())
        for (int channel = 0; channel < 2; ++channel)
        {
            vuRepaintPositions[channel] = targetFor (observatoryFrame.meter, currentFactsAvailable(),
                                                     channel, vuCalibration());
            vuAnimator->snap (channel, vuRepaintPositions[channel]);
        }
    vuAnimator->setVisible (visible);
}

void View::setVuFrameTestMode (bool enabled) { vuFrameTestMode = enabled; updateVuVisibility(); }
void View::tickVuForTest (double dt) { if (vuAnimator) vuAnimator->tickForTest (dt); }
double View::vuPositionForTest (int channel) const { return vuAnimator ? vuAnimator->position (channel) : 0.0; }
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
    for (int channel = 0; channel < 2; ++channel)
        vuAnimator->setTarget (channel, targetFor (state.meter, state.currentAvailable,
                                                   channel, state.calibrationDbfs));
    // The cache is drawn at the pixels it lands on: the editor's magnification and a Retina or
    // high-DPI display both count, so the face stays as sharp as the needle drawn over it.
    vuLayerCache.paint (g, getLocalBounds(), state,
                        g.getInternalContext().getPhysicalPixelScaleFactor(),
                        static_cast<float> (vuAnimator->position (0)),
                        static_cast<float> (vuAnimator->position (1)));
}
}
