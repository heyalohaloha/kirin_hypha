#pragma once

#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
// Kirin OS が日本語の画面で保存・公開した組み込みの名前（CHECK セット・Check・自動の Cue）を、Kirin OS の英語の
// 画面と同じ英語にそろえる（Hypha は名前を言語にかかわらず英語で出す。HyphaJapaneseCatalog.h の方針）。
// 2026-10-04（Daisuke「共通項目に日本語が混じっていないか」）：表を Kirin OS の訳（ja.json）で全部埋め、名前の
// 後ろに曲名・順位が付いた形（「名前  /  曲名」「名前   1 / 3」）にも当てる。ID・利用者が付けた名前・音源は変えない。
inline constexpr const char* standardDisplayNames[][2] {
    { u8"Mastering｜ステレオ・低域・全体の流れ", u8"Mastering · stereo, low end, and context" },
    { u8"Mastering｜音色・音量・ダイナミクス", u8"Mastering · tone, level, and dynamics" },
    { u8"編曲｜グルーヴ・展開・Hook", u8"Arrangement · groove, energy, and hooks" },
    { u8"録音｜部屋・ノイズ・かぶり", u8"Recording · room, noise, and bleed" },
    { u8"MIX｜パンチ・幅・奥行き", u8"MIX · punch, width, and depth" },
    { u8"MIX｜バランス・音色", u8"MIX · balance and tone" },
    { u8"録音｜音色・演奏", u8"Recording · tone and performance" },
    { u8"Album 全体との関係", "Album context" },
    { u8"編曲｜役割・密度", u8"Arrangement · roles and density" },
    { u8"曲中のエネルギー変化", "Energy curve" },
    { u8"演奏のダイナミクス", "Performance dynamics" },
    { u8"周波数帯域の使い方", "Frequency space" },
    { u8"聴かせどころの位置", "Hook placement" },
    { u8"ボーカルのバランス", "Vocal balance" },
    { u8"小音量時のバランス", "Low-volume balance" },
    { u8"セクション間の違い", "Section difference" },
    { u8"再生環境による違い", "Translation" },
    { u8"全工程｜基本5項目", u8"All stages · 5 essential checks" },
    { u8"全工程｜基本3項目", u8"All stages · 3 essential checks" },
    { u8"音程とタイミング", "Pitch and timing" },
    { u8"コンプレッション", "Compression" },
    { u8"ジャンルとの関係", "Genre context" },
    { u8"納品条件との関係", "Delivery context" },
    { u8"最も大きい30秒", "Loudest 30 s" },
    { u8"ボーカルバランス", "Vocal balance" },
    { u8"トランジェント", "Transient" },
    { u8"楽器編成と役割", "Instrumentation and roles" },
    { u8"音を減らす判断", "Subtraction" },
    { u8"キックとベース", "Kick and bass" },
    { u8"音色のバランス", "Tonal balance" },
    { u8"ステレオと位相", "Stereo and phase" },
    { u8"音源との距離", "Distance" },
    { u8"分離とかぶり", "Separation and bleed" },
    { u8"位相とマイク", "Phase and microphones" },
    { u8"ダイナミクス", "Dynamics" },
    { u8"低域の安定性", "Low-end consistency" },
    { u8"マスタリング", "Mastering" },
    { u8"部屋の響き", "Room" },
    { u8"音の重なり", "Layering" },
    { u8"エフェクト", "Effects" },
    { u8"主役の位置", "Lead position" },
    { u8"制作段階", "Production stage" },
    { u8"音の密度", "Density" },
    { u8"グルーヴ", "Groove" },
    { u8"サビ候補", "Chorus candidate" },
    { u8"ステレオ", "Stereo" },
    { u8"ミックス", "MIX" },
    { u8"ノイズ", "Noise" },
    { u8"対旋律", "Countermelody" },
    { u8"パンチ", "Punch" },
    { u8"奥行き", "Depth" },
    { u8"かぶり", "Bleed" },
    { u8"曲全体", "Full track" },
    { u8"音色", "Tone" },
    { u8"中域", "Midrange" },
    { u8"高域", "High end" },
    { u8"低域", "Low end" },
    { u8"録音", "Recording" },
    { u8"編曲", "Arrangement" },
    { u8"幅", "Width" },
};

inline juce::String standardDisplayName (const juce::String& input)
{
    // Kirin OS が新しい B セットに付ける既定の名前（「セット 2」。作ったときの言語で保存される）。
    if (const auto set = juce::String::fromUTF8 (u8"セット "); input.startsWith (set) && input.length() > set.length()
        && juce::CharacterFunctions::isDigit (input[set.length()]))
        return "Set " + input.substring (set.length());
    for (const auto& pair : standardDisplayNames)
    {
        const auto original = juce::String::fromUTF8 (pair[0]);
        // そのものか、後ろに空白で区切って何かが付いた形（曲名・準備中・順位）。名前の途中では当てない。
        const auto english = juce::String::fromUTF8 (pair[1]);  // 「·」は ASCII ではない
        if (input == original) return english;
        if (input.startsWith (original) && input.length() > original.length() && input[original.length()] == ' ')
            return english + input.substring (original.length());
    }
    return input;
}

inline void prepareDisplayNames (State& state)
{
    state.presetName = standardDisplayName (state.presetName);
    state.checkLabel = standardDisplayName (state.checkLabel);
    state.cueLabel = standardDisplayName (state.cueLabel);
    for (auto* options : { &state.presets, &state.checks, &state.cues, &state.songSets })
        for (auto& option : *options) option.label = standardDisplayName (option.label);
}
}
