#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"

namespace hypha::reference_ui
{
struct State;

// B（REF）の画面の右の Balance。A・選んでいる B の曲・B SET の分布を重ねる。
void paintReferenceBalance (juce::Graphics&, juce::Rectangle<float>, const State&, presentation::Context);
// 凡例の A の場所（A の窓を選ぶボタンを重ねる）。
juce::Rectangle<int> referenceBalanceAWindowArea (juce::Rectangle<float>, const State&, presentation::Context);
}
