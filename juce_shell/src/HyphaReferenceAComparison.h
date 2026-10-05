#pragma once

#include "HyphaTheme.h"

#include <limits>

// 2026-10-04：符号つきの差では、A が多いのか少ないのか、どちらの数字なのかが分かりにくかった。
// 比べる側（B・C・V）との差は、A を主語にして言葉で言う：「A 3.7 dB LESS」（日本語は「Aが3.7 dB少ない」）。
// 符号は付けない（向きは言葉が言う）。表示の桁で 0 になるときは「A SAME AS C」（「AはCと同じ」）。
namespace hypha::reference_ui
{
// 項目に合う言葉の組。
enum class AWords
{
    amount,    // 帯域の量：MORE / LESS（多い・少ない）
    level,     // 相関・ピーク・RMS・1 kHz の高さ：HIGHER / LOWER（高い・低い）
    size,      // クレスト・音量の動き・立ち上がり：LARGER / SMALLER（大きい・小さい）
    loudness,  // 音量：LOUDER / QUIETER（大きい・小さい）
    width      // 幅：WIDER / NARROWER（広い・狭い）
};

// A の値と比べる側の値の組。差の向き（A − 比べる側）はこの型の中で一度だけ決める（差の数を直接渡さない：引数を
// 1 つ取り違えるだけで LOWER と HIGHER が逆になる）。
struct AVersus
{
    double a = std::numeric_limits<double>::quiet_NaN();
    double other = std::numeric_limits<double>::quiet_NaN();
    double aMinusOther() const noexcept { return a - other; }
};

struct AComparison
{
    juce::String text;    // 英語の文（描くときに画面の言語になる）
    juce::String number;  // 文の中の数字（同じなら空）
    juce::String word;    // 向きの言葉（「LESS」など。同じなら空）
    bool shown() const noexcept { return text.isNotEmpty(); }
    bool same() const noexcept { return shown() && number.isEmpty(); }
};

// `unit` は数字の後ろ（" dB"、" LU"、" pt"、無ければ ""）。どちらかの値が無ければ空。
AComparison compareA (AVersus, int decimals, const juce::String& unit, AWords, char other);

// 4 帯域の欄（見出し「A VS C (dB)」が主語の A と単位を言う）：「3.7 LESS」（「3.7少ない」）、0 なら「SAME」（「差なし」）。
// 日本語で 1 段に「Aが…」まで収まらない（2026-10-04、300% の C で 37 px 超えた）ので、主語は見出しに 1 度だけ置く。
AComparison compareBand (AVersus);

// 数字を readout の等幅の字（`numberColour`）、言葉を unit の字（`wordsColour`）で描く（帯域の 4 つの欄）。
// 幅が足りなければ全体を unit の字で詰めずに省略する。
float aComparisonWidth (const AComparison&, const presentation::Context&);
void paintAComparison (juce::Graphics&, const AComparison&, juce::Rectangle<float>, juce::Justification,
                       const presentation::Context&, juce::Colour wordsColour, juce::Colour numberColour);
}
