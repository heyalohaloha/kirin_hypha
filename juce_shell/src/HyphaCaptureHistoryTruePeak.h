#pragma once

#include "HyphaCaptureHistoryGeometry.h"
#include "HyphaCaptureHistoryPainter.h"

// The LEVEL history TP presentation approved on 2026-09-24: one stem for each contiguous excursion
// above -1 dBTP, drawn at its measured height against the right +6..-24 dBTP axis over the lower
// part of the shared plot. Only a peak strictly above 0 dBTP glows.
namespace hypha::capture_history::true_peak
{
juce::Rectangle<float> overlayFor (juce::Rectangle<float> sharedPlot) noexcept;
float yFor (juce::Rectangle<float> overlay, double value) noexcept;
void paintEvents (juce::Graphics&, juce::Rectangle<float> sharedPlot,
                  const std::vector<KirinMeterHistoryEntry>&, const time_history::HistoryAxis&,
                  const TruePeakSummary&, double sampleRate);
void paintAxis (juce::Graphics&, const Layout&, presentation::Context);
}
