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
        }, [this] { refreshVuTargets(); });
    if (visible && ! vuAnimator->isVisible())
        for (int channel = 0; channel < 2; ++channel)
        {
            vuRepaintPositions[channel] = targetFor (observatoryFrame.meter, currentFactsAvailable(),
                                                     channel, vuCalibration());
            vuAnimator->snap (channel, vuRepaintPositions[channel]);
        }
    vuAnimator->setVisible (visible);
}

// One needle target per channel: the audio thread's live integration while the tap has a whole
// window, otherwise the 100 ms publication (tests, offscreen paint, the first 300 ms after the
// tap was enabled). Same calibration and the same availability gate as the published value.
void View::refreshVuTargets()
{
    if (! vuAnimator) return;
    const auto& meter = observatoryFrame.meter;
    const bool available = currentFactsAvailable();
    bool usedLive = false;
    for (int channel = 0; channel < 2; ++channel)
    {
        const bool channelAvailable = available && channel < meter.channels;
        const auto liveValue = channelAvailable && onVuLiveLevel ? onVuLiveLevel (channel)
                                                                : std::numeric_limits<double>::quiet_NaN();
        if (channelAvailable && std::isfinite (liveValue))
        {
            usedLive = true;
            vuAnimator->setTarget (channel, hybrid_vu::vuNormalized (liveValue, vuCalibration()));
        }
        else vuAnimator->setTarget (channel, targetFor (meter, available, channel, vuCalibration()));
    }
    vuAnimator->setLive (usedLive);
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
    refreshVuTargets();
    // The cache is drawn at the pixels it lands on: the editor's magnification and a Retina or
    // high-DPI display both count, so the face stays as sharp as the needle drawn over it.
    vuLayerCache.paint (g, getLocalBounds(), state,
                        g.getInternalContext().getPhysicalPixelScaleFactor(),
                        static_cast<float> (vuAnimator->position (0)),
                        static_cast<float> (vuAnimator->position (1)));
}
}
