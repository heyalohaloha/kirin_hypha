#pragma once

#include <juce_graphics/juce_graphics.h>

#include "HyphaPresentationContext.h"
#include "HyphaReferenceAComparison.h"

// 比べる側との差は A を主語に言葉で（2026-10-04、
// HyphaReferenceAComparison.h）。`otherMinusA` は比べる側 − A。
namespace hypha::reference_metric_painter
{
void paintPanel (juce::Graphics&, juce::Rectangle<float>, float alpha = 0.66f);
void paintComparisonRoots (juce::Graphics&, juce::Rectangle<float>);
// A・比べる側・差の 3 列。差の列は見出し「A VS C」（「CよりA」）、符号の無い数字、下に単位と言葉（「LU小さい」）。
void paintMetric (juce::Graphics&, juce::Rectangle<float>, const juce::String& name,
                  const juce::String& unit, reference_ui::AVersus, reference_ui::AWords,
                  presentation::Context, const juce::String& side = "B");
// 200% 以下の差だけの欄：「LUFS-I  A 1.2 LU QUIETER」（「LUFS-I  Aが1.2 LU小さい」）。
void paintCompactDelta (juce::Graphics&, juce::Rectangle<float>, const juce::String& name,
                        reference_ui::AVersus, const juce::String& unit, reference_ui::AWords, presentation::Context,
                        const juce::String& side = "B");
}
