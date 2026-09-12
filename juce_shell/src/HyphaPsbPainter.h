#pragma once

#include <array>
#include <juce_graphics/juce_graphics.h>

#include "HyphaPresentationContext.h"

namespace hypha::psb_painter
{
constexpr std::size_t bandCount = 20;

struct State
{
    const std::array<double, bandCount>& values;
    bool available = false;
    bool delta = false;
    int hoverBand = -1;
    juce::String unavailableText = "PSB WARMING";
    presentation::Context presentation = presentation::defaultContext();
};

void paint (juce::Graphics&, juce::Rectangle<float>, const State&);
void paintSubviewToggle (juce::Graphics&, juce::Rectangle<float>, bool psbSelected,
                         presentation::Context);
juce::Rectangle<float> dataBounds (juce::Rectangle<float> componentBounds);
int bandAt (juce::Rectangle<float> componentBounds, juce::Point<float>) noexcept;
}
