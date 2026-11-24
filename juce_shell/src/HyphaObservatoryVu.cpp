#include "HyphaObservatoryView.h"
#include "HyphaKirinPopupMenu.h"
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

bool View::metricFaceActive() const noexcept
{
    return luFace.available() && metric_needle::metric (metricSelection.face)
        && (role != Role::pre || metricSelection.face != metric_needle::Face::sharp || metricSelection.preSharpAvailable);
}

void View::setMetricFace (metric_needle::Face choice)
{
    choice = metric_needle::normaliseFace (static_cast<int> (choice));
    const bool changed = !metricSelection.configured || metricSelection.face != choice;
    metricSelection.configured = true;
    metricSelection.face = choice;
    luFace.onFaceMenu = [this] {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&kirin_popup::appearance());
        for (int value = 0; value < 5; ++value) {
            const auto face = static_cast<metric_needle::Face> (value);
            const bool allowed = face != metric_needle::Face::sharp || role != Role::pre || metricSelection.preSharpAvailable;
            if (allowed) menu.addItem (value + 1, metric_needle::label (face), true, metricSelection.face == face);
        }
        const juce::Component::SafePointer<View> safe (this);
        menu.showMenuAsync (kirin_popup::options (luFace.faceControl(), *this, getWidth()), [safe] (int result) {
            if (safe == nullptr || result < 1 || result > 5 || !safe->luFace.available()) return;
            const auto face = static_cast<metric_needle::Face> (result - 1);
            if (face == metric_needle::Face::sharp && safe->role == Role::pre && !safe->metricSelection.preSharpAvailable) return;
            safe->setMetricFace (face);
            if (safe->onMetricFaceChoice) safe->onMetricFaceChoice (face);
        });
    };
    luFace.setCommonFace (choice == metric_needle::Face::lu, metric_needle::label (choice),
                          role != Role::pre || metricSelection.preSharpAvailable);
    if (changed) { updateVuVisibility(); resized(); repaint(); }
}

void View::setPreSharpAvailable (bool available)
{
    if (metricSelection.preSharpAvailable == available) return;
    metricSelection.preSharpAvailable = available;
    if (metricSelection.configured) setMetricFace (metricSelection.face);
    updateVuVisibility(); resized(); repaint();
}

bool View::setMetricFrame (const metric_needle::Frame& frame)
{
    const auto wasReady = metricSelection.referenceReady();
    const auto wasSong = metricSelection.referenceSong();
    if (!metricSelection.frames.replace (frame)) return false;
    if (hybridVuVisible() && (wasReady != metricSelection.referenceReady() || wasSong != metricSelection.referenceSong())) resized();
    repaint();
    return true;
}

void View::updateVuVisibility()
{
    const auto visible = hybridVuVisible() && ! luFace.active() && ! metricFaceActive() && (vuFrameTestMode || (isShowing() && getPeer() != nullptr));
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
    if (metricFaceActive())
    {
        auto frame = metricSelection.frames.current();
        if (frame.face != metricSelection.face)
        {
            frame = {};
            frame.status = metric_needle::Status::measuring;
        }
        frame.face = metricSelection.face;
        frame.ownIsPre = role == Role::pre;
        if (!currentFactsAvailable()) { frame.own = metric_needle::missing; frame.pre = metric_needle::missing; }
        if (metric_face::paintMetricCandidate (g, getLocalBounds(), state, frame,
                metricSelection.candidateChassis.get(), metricSelection.candidateBackdrop))
            return;
        hybrid_vu::paintStatic (g, getLocalBounds(), state);
        metric_face::paint (g, hybrid_vu::needle_painter::faceBounds (getLocalBounds()).toNearestInt(),
                            frame, presentationContext(), nullptr, true);
        hybrid_vu::paintDynamic (g, getLocalBounds(), state);
        return;
    }
    if (luFace.active()) // the LU face reads the published loudness; the VU's moving-coil path rests
    {
        luFace.paint (g, getLocalBounds(), state, observatoryFrame.delta, deltaFactsAvailable(),
                      g.getInternalContext().getPhysicalPixelScaleFactor());
        return;
    }
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
