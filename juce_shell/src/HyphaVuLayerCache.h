#pragma once

#include "HyphaHybridVuPainter.h"
#include "HyphaObservationEquality.h"
#include "HyphaVuNeedlePainter.h"
#include <cmath>

namespace hypha::hybrid_vu
{
// Static face and a publication-rate composite are retained separately. Frame paints only
// copy the composite and draw the two needles.
class LayerCache
{
public:
    std::size_t staticBuildsForTest() const noexcept { return staticBuilds; }
    void paint (juce::Graphics& g, juce::Rectangle<int> bounds, const State& state,
                float scale, float left, float right)
    {
        if (bounds.isEmpty()) return;
        // Physical pixels per point. The cap bounds the three retained images; past it (a large
        // magnification on a Retina display) the face is scaled up rather than redrawn.
        scale = std::isfinite (scale) ? juce::jlimit (1.0f, 4.0f, scale) : 1.0f;
        const juce::Point<int> requestedSize (bounds.getWidth(), bounds.getHeight());
        const bool staticChanged = ! ready || size != requestedSize || pixelScale != scale
            || role != state.role || presentation != state.presentation || jungle != state.jungle
            || calibration != state.calibrationDbfs;
        const bool captionChanged = staticChanged || channels != state.meter.channels;
        const bool dynamicChanged = captionChanged || ! observation_equality::same (meter, state.meter)
            || ! observation_equality::same (watch, state.watch)
            || currentAvailable != state.currentAvailable || cumulativeAvailable != state.cumulativeAvailable
            || watchAvailable != state.watchAvailable || shortTerm != state.shortTermLoudness
            || recording != state.recording || connectionText != state.connectionText
            || connectionColour != state.connectionColour;
        const juce::Rectangle<int> local (0, 0, bounds.getWidth(), bounds.getHeight());
        if (staticChanged)
        {
            ++staticBuilds;
            size = requestedSize; pixelScale = scale; role = state.role;
            presentation = state.presentation; jungle = state.jungle;
            calibration = state.calibrationDbfs;
            staticImage = makeImage (size, scale);
            juce::Graphics layer (staticImage);
            layer.addTransform (juce::AffineTransform::scale (scale));
            paintStatic (layer, local, state);
            dialBackground = makeImage (size, scale);
            juce::Graphics dialBack (dialBackground);
            dialBack.addTransform (juce::AffineTransform::scale (scale));
            paintDialLayer (dialBack, local, state, false);
        }
        if (captionChanged)
        {
            channels = state.meter.channels;
            dialCaption = makeImage (size, scale);
            juce::Graphics caption (dialCaption);
            caption.addTransform (juce::AffineTransform::scale (scale));
            paintDialLayer (caption, local, state, true);
        }
        if (dynamicChanged)
        {
            meter = state.meter; watch = state.watch;
            currentAvailable = state.currentAvailable; cumulativeAvailable = state.cumulativeAvailable;
            watchAvailable = state.watchAvailable; shortTerm = state.shortTermLoudness;
            recording = state.recording; connectionText = state.connectionText;
            connectionColour = state.connectionColour;
            composite = staticImage.createCopy();
            {
                juce::Graphics layer (composite);
                layer.addTransform (juce::AffineTransform::scale (scale));
                paintDynamic (layer, local, state);
            }
            juce::Graphics pixels (composite);
            pixels.drawImageAt (dialBackground, 0, 0);
        }
        ready = true;
        g.drawImage (composite, bounds.toFloat(), juce::RectanglePlacement::stretchToFit);
        needle_painter::paintPair (g, bounds, left, right);
        g.drawImage (dialCaption, bounds.toFloat(), juce::RectanglePlacement::stretchToFit);
    }

private:
    static juce::Image makeImage (juce::Point<int> size, float scale)
    {
        return { juce::Image::ARGB, juce::jmax (1, juce::roundToInt (size.x * scale)),
                 juce::jmax (1, juce::roundToInt (size.y * scale)), true };
    }
    juce::Image staticImage, dialBackground, dialCaption, composite;
    juce::Point<int> size;
    float pixelScale = 0.0f;
    observatory::Role role = observatory::Role::pre;
    presentation::Context presentation;
    std::size_t staticBuilds = 0;
    bool ready = false, jungle = false, currentAvailable = false, cumulativeAvailable = false;
    bool watchAvailable = false, shortTerm = false, recording = false;
    int calibration = 0, channels = 0;
    KirinMeterSession meter {};
    KirinWatchDisplay watch {};
    juce::String connectionText;
    juce::Colour connectionColour;
};
}
