#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "reference_audition/ReferenceCuePart.h"

// H12: C の画面の下段（方向設計 §4）。同じ区間・同じ音量で比べる値。
//  - 比べる gain：C が鳴っていればその gain、鳴っていなければ「A の窓 − Cue の Integrated」（選べば
//    この gain で鳴る）。Kirin OS で「元の音量」にした Check は 0（聴こえるとおりに比べる）。
//  - 4 帯域の要約：Kirin OS の Cue の Balance（gain を足す）− A の直近の窓の Balance（同じ定義）。
//    数字だけを出し、良し悪しの色は付けない。
//  - Cue の時間軸：曲の中の Cue の位置とループ、鳴っているときは C の位置の線。
namespace hypha::reference_ui
{
struct State;

double comparisonGainDb (const State&) noexcept;
// A と C の値が同じ帯域の並びで揃っているか（64 帯域、中心が 0.1% 以内）。
bool kirinComparable (const State&) noexcept;
void paintBandSummary (juce::Graphics&, juce::Rectangle<int>, const State&, presentation::Context);
void paintCueBar (juce::Graphics&, juce::Rectangle<int>, const State&, presentation::Context);
// C の画面の SPECTRUM／LOW FREQUENCY を「Cue 対 A の同じ長さの直近」で描く。描けなければ false（今までの
// 曲全体の分布の表示に戻る）。
bool paintCueSpectrum (juce::Graphics&, juce::Rectangle<float> area, const State&, double minimumHz, double maximumHz);
juce::String matchReadout (const State&);
// 見比べの凡例：「A LAST 30 S / C CUE」（A の窓が 3 秒に満たなければ A WAITING、音量をそろえられなければ
// LEVEL NOT MATCHED を足す）。
juce::String cueSpectrumLegend (const State&);
// 凡例の比べる側の名前（2026-10-04、Daisuke「A直近10秒 / Bサビ」）：「B CHORUS 1:02-1:24」「C WHOLE」など。
// range は区間の時刻を添えるか（B の画面には Cue の時間軸が無いので添える。C は時間軸が言う）。
juce::String cuePartLegend (const char* side, reference_audition::CuePart, double startSeconds, double endSeconds, bool range);
}
