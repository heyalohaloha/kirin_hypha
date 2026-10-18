#pragma once

#include "HyphaLanguage.h"
#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
// Kirin OS の組み込みの名前（CHECK セット・Check）の日本語と英語（Kirin OS の ja.json と同じ）。組み込みの ID の項目
// だけに使い（kirinOsBuiltInId）、日本語で保存された名前も英語の画面では英語にそろえる。名前の後ろに曲名・順位が
// 付いた形（「名前  /  曲名」「名前   1 / 3」）にも当てる。ID・利用者が付けた名前・音源は変えない。
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
    { u8"聴かせどころ", "Hook" },
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

// 2026-10-18：標準の名前かどうかは綴りで決めない。Kirin OS は組み込みの Preset と Check に、決まった名前空間の ID
// （00000000-0000-4000-8000-＋12 桁の数字。Kirin OS の組み込みの一覧が source で固定する）を付ける。その ID の項目だけを
// 画面の言語の名前で出し、それ以外（利用者が付けた名前、標準と同じ綴りの自作の名前、印の無い Cue と B セットの名前）は
// 書かれたとおりに出す。C の項目の ID は「Check の ID / 曲の ID」、作品の Preset は「work:」が前に付く。
inline bool kirinOsBuiltInId (const juce::String& id)
{
    auto key = id.startsWith ("work:") ? id.substring (5) : id;
    key = key.upToFirstOccurrenceOf ("/", false, false);
    const juce::String space ("00000000-0000-4000-8000-");
    return key.length() == space.length() + 12 && key.startsWith (space)
        && key.substring (space.length()).containsOnly ("0123456789");
}

// 日本語の画面で Kirin OS の日本語の画面と同じ名前（ja.json）。上の表を逆に引き、Kirin OS が日本語でも英語のまま出す
// 名前は英語のまま。
inline constexpr const char* standardJapaneseOverrides[][2] {
    { "Mastering", "Mastering" }, { "Stereo", "Stereo" }, { "MIX", "MIX" }, { "Arrangement", u8"構成" },
};

inline juce::String japaneseStandardName (const juce::String& english)
{
    // そのものか、Hypha が後ろに付けた順位・曲名（2 つ以上の空白の後）。利用者が続けて打った語の前では当てない。
    const auto fits = [&english] (const juce::String& name)
    { return english == name || (english.startsWith (name) && english.substring (name.length()).startsWith ("  ")); };
    for (const auto& pair : standardJapaneseOverrides)
        if (const auto name = juce::String::fromUTF8 (pair[0]); fits (name))
            return juce::String::fromUTF8 (pair[1]) + english.substring (name.length());
    for (const auto& pair : standardDisplayNames)
        if (const auto name = juce::String::fromUTF8 (pair[1]); fits (name))
            return juce::String::fromUTF8 (pair[0]) + english.substring (name.length());
    return english;
}

// 組み込みの項目の名前は画面の言語で（英語の画面では Kirin OS の英語の名前にそろえる）、それ以外は書かれたとおり。
inline juce::String shownStandardName (const juce::String& id, const juce::String& input, i18n::Language language)
{
    if (! kirinOsBuiltInId (id)) return input;
    const auto english = standardDisplayName (input);
    return language == i18n::Language::japanese ? japaneseStandardName (english) : english;
}

inline void prepareDisplayNames (State& state, i18n::Language language = i18n::current())
{
    state.presetName = shownStandardName (state.presetId, state.presetName, language);
    state.checkLabel = shownStandardName (state.checkId, state.checkLabel, language);
    state.cueLabel = shownStandardName (state.cueId, state.cueLabel, language);
    for (auto* options : { &state.presets, &state.checks, &state.cues, &state.songSets })
        for (auto& option : *options)
        {
            option.label = shownStandardName (option.id, option.label, language);
            option.checkLabel = shownStandardName (option.id, option.checkLabel, language);
        }
}
}
