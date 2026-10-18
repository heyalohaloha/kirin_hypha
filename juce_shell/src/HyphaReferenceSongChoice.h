#pragma once

#include "reference_audition/ReferenceRuntimeV2Model.h"

// B の曲の選択欄の文字（曲名と、その後ろの状態の語）と、音源の欠けた曲の準備の状態（2026-10-18）。
// 音源が見つからない曲は「PREPARING」と言わず「NOT FOUND」と言い、状態の行は Kirin OS の直し方（Retry）を言う。
namespace hypha::reference_ui
{
inline constexpr const char* songNotFoundSuffix = "   NOT FOUND";
inline constexpr const char* songPreparingSuffix = "   PREPARING";

inline juce::String songChoiceLabel (const juce::String& name, bool requiresPreparation, bool sourceMissing)
{
    return name + (sourceMissing ? songNotFoundSuffix : requiresPreparation ? songPreparingSuffix : "");
}

// 選択欄の文字から曲名だけ（一覧は状態を別の欄で言う）。
inline juce::String songChoiceName (const juce::String& label)
{
    for (const auto* suffix : { songNotFoundSuffix, songPreparingSuffix })
        if (label.endsWith (suffix))
            return label.dropLastCharacters (juce::String (suffix).length());
    return label;
}

// Kirin OS の B セットが音源の欠けを言う曲は、準備の記録がまだ準備済みでも見つからないとして扱う。
inline void markSourceMissing (reference_audition::RuntimeSongPreparation& preparation)
{
    preparation.reason = "source_unavailable";
    preparation.retry = "manual";
}
}
