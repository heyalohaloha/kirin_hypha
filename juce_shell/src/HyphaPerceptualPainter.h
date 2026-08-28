#pragma once

#include <juce_graphics/juce_graphics.h>

#include "HyphaPerceptualHistory.h"
#include "kirin_hypha_ffi.h"

namespace hypha::perceptual_painter
{
    struct PaintState
    {
        const KirinPerceptualView& snapshot;
        const perceptual_history::History& history;
        const juce::String& actionNotice;
        bool haveSnapshot;
        bool snapshotValid;
        uint8_t channelMode;
        uint8_t inputChannels;
    };

    void paint (juce::Graphics& graphics,
                juce::Rectangle<float> bounds,
                const PaintState& state);
}
