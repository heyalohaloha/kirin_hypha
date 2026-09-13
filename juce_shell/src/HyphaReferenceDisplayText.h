#pragma once

#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
// Old saved factory copies can carry Japanese labels. Translate only known standard
// vocabulary at the presentation boundary; IDs, custom names and source receipts stay intact.
inline juce::String standardDisplayName (const juce::String& input)
{
    static constexpr const char* names[][2] {
        { "全工程｜基本3項目", "Quick Reference" },
        { "全工程｜基本5項目", "Quick Reference" },
        { "録音", "Recording" }, { "編曲", "Arrangement" },
        { "ミックス", "MIX" }, { "マスタリング", "Mastering" },
        { "低域", "Low end" }, { "ダイナミクス", "Dynamics" },
        { "ボーカルバランス", "Vocal balance" }, { "音色", "Tone" },
        { "ステレオ", "Stereo" }, { "曲全体", "Full track" }
    };
    for (const auto& pair : names)
    {
        const auto original = juce::String::fromUTF8 (pair[0]);
        if (input == original) return pair[1];
        // A Check option may include its source or a preparation suffix.
        if (input.startsWith (original + "  /  "))
            return juce::String (pair[1]) + input.substring (original.length());
    }
    return input;
}

inline void prepareDisplayNames (State& state)
{
    state.presetName = standardDisplayName (state.presetName);
    state.checkLabel = standardDisplayName (state.checkLabel);
    state.cueLabel = standardDisplayName (state.cueLabel);
    for (auto* options : { &state.presets, &state.checks, &state.cues })
        for (auto& option : *options) option.label = standardDisplayName (option.label);
}
}
