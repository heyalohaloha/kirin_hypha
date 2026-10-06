#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"

// 2026-10-04（2 つの値を並べるだけでは、どう使えばよいかが分からなかった）：C の画面の
// Dynamics・Loudness・Stereo・Waveform・Transient の Check は、項目ごとに A（金）と C（水色）の帯を上下に並べる。帯は
// 区間の中の p10〜p90、縦線は中央値、右に中央値、A の行に差（A を主語に言葉で。HyphaReferenceAComparison.h）。
// 値は Kirin OS と同じ定義（ReferenceDynamicsRange.h）で、A は直近（C の窓の長さ）、C は Cue。音量に関わる値（LUFS-M・ピーク・RMS）は C を鳴らす gain で合わせて（聞こえる
// 大きさで）並べる。音量の動き（LUFS-S）はそれぞれの中央値のまわりの幅で比べる。差に良し悪しの色は付けない（R-22）。
namespace hypha::reference_audition { struct VisualTimeline; }

namespace hypha::reference_ui
{
struct State;

bool rangeStripBinding (const juce::String& binding) noexcept;  // dynamics・loudness・stereo・waveform・transient

// C の Cue の値が無ければ false（呼ぶ側が今までどおり描く）。frame は REF の主役の窓（塗った面の上に枠の内側の影を戻す）。
bool paintCueRangeStrips (juce::Graphics&, juce::Rectangle<float> bounds, const State&, const juce::String& binding,
                          presentation::Context, juce::Rectangle<float> frame);

// V の画面の Check のタブ：位置合わせで対応した同じ区間の A と V（Hypha が同じフレームで測る）。V は同じ曲なので、上に
// 時間の線（A と V）を重ね、下に範囲の帯。area は図の中（見出しは V の画面が描く）。A と V が 3 秒に満たなければ false。
bool paintVersionRangeStrips (juce::Graphics&, juce::Rectangle<float> area, const reference_audition::VisualTimeline*,
                              const juce::String& binding, double gainDb, presentation::Context);
}
