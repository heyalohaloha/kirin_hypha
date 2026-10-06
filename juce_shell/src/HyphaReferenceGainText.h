#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

// 2026-10-06：gain の読みの書き方を一か所に（B の一覧の MATCH・C の MATCH・状態の行）。「+6.0」「−1.6」、表示の桁で
// 0 なら「+0.0」（一覧と状態の行だけ ASCII の「-」で、−0.04 は場所によって「−0.0」になっていた）。読む値の基準は
// displayGainDb（HyphaReferenceComponent.h）。
namespace hypha::reference_ui
{
inline juce::String gainText (double gainDb)
{
    const auto rounded = std::round (gainDb * 10.0) / 10.0;
    return (rounded < 0.0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String ("+"))
         + juce::String (std::abs (rounded), 1);
}
}
