#pragma once

#include <juce_graphics/juce_graphics.h>

#include "HyphaSpectrumPainter.h"
#include "HyphaAbsoluteSpectrumHistory.h"
#include "HyphaGuideFrequencyOverlay.h"
#include "HyphaSpectrumFocusTrail.h"
#include "kirin_hypha_ffi.h"

namespace hypha::spectrum_chrome
{
    juce::String frequencyReadoutText (float hz, float approximateBelowHz);

    struct PaintState
    {
        const KirinSpectrumView& snapshot;
        const spectrum_painter::SpectrumBins& pre;
        const spectrum_painter::SpectrumBins& post;
        const spectrum_painter::SpectrumBins& delta;
        const spectrum_painter::SpectrumBins& readoutPre;
        const spectrum_painter::SpectrumBins& readoutPost;
        const spectrum_painter::SpectrumBins& readoutDelta;
        const spectrum_painter::SpectrumBins& mark;
        const spectrum_focus::FocusTrailHistory* focusTrail;
        const juce::String& actionNotice;
        const juce::String& analysisOwnerNames;
        const guide_frequency::Overlay& guideOverlay;
        const absolute_spectrum::History* absoluteHistory;
        const spectrum_painter::SpectrumBins& absolutePeakHold;
        bool absoluteObservation;
        bool haveSnapshot;
        bool snapshotValid;
        bool haveMark;
        float hoverNormalisedX;
        float focusFrequencyHz;
        uint8_t channelMode;
        uint8_t inputChannels;
    };

    void paint (juce::Graphics& graphics,
                juce::Rectangle<float> bounds,
                const PaintState& state);
}
