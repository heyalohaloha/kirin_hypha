#pragma once

#include "HyphaCaptureHistoryGeometry.h"
#include "HyphaCaptureHistoryPainter.h"

// The LEVEL history TP presentation: one stem for each contiguous excursion above -1 dBTP, drawn
// at its measured height over the lower part of the shared plot. Every stem starts above -1 dBTP,
// so the right axis covers only -1..+3 dBTP (2026-10-06; it was +6..-24, whose lower 23 dB never
// held a stem). The stem stands on -1, a line marks 0, and a peak above +3 rests on the top with a
// cap. Only a peak strictly above 0 dBTP glows. TP keeps the cyan of the VU TP rail.
namespace hypha::capture_history::true_peak
{
juce::Rectangle<float> overlayFor (juce::Rectangle<float> sharedPlot) noexcept;
inline constexpr double axisBottom = -1.0;
inline constexpr double axisTop = 3.0;
float yFor (juce::Rectangle<float> overlay, double value) noexcept;
void paintEvents (juce::Graphics&, juce::Rectangle<float> sharedPlot,
                  const std::vector<KirinMeterHistoryEntry>&, const time_history::HistoryAxis&,
                  const TruePeakSummary&, double sampleRate);
void paintAxis (juce::Graphics&, const Layout&, presentation::Context);
}
