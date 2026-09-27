#pragma once

#include "HyphaSpectrumChromePainter.h"

namespace hypha::spectrum_chrome
{
// The frequency probe: its line and the readout at the pointer or the locked frequency.
void paintProbe (juce::Graphics&,
                 juce::Rectangle<float> outerPlot,
                 juce::Rectangle<float> plot,
                 float scale,
                 float probeNormalisedX,
                 float minimumHz,
                 float maximumHz,
                 const PaintState&);
}
