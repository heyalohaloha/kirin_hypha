#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"

// 2026-10-04（Daisuke「この 2 個は視覚的にどういう活用をしたら良いのか分からない」→「範囲の帯で比べる」）：C の画面の
// Dynamics・Loudness・Stereo・Waveform・Transient の Check は、項目ごとに A（金）と C（水色）の帯を上下に並べる。帯は
// 区間の中の p10〜p90、縦線は中央値、右に中央値と差（C − A）。値は Kirin OS と同じ定義（ReferenceDynamicsRange.h）で、
// A は直近（C の窓の長さ）、C は Cue。音量に関わる値（LUFS-M・ピーク・RMS）は C を鳴らす gain で合わせて（聞こえる
// 大きさで）並べる。音量の動き（LUFS-S）はそれぞれの中央値のまわりの幅で比べる。差に良し悪しの色は付けない（R-22）。
namespace hypha::reference_ui
{
struct State;

bool rangeStripBinding (const juce::String& binding) noexcept;  // dynamics・loudness・stereo・waveform・transient

// C の Cue の値が無ければ false（呼ぶ側が今までどおり描く）。
bool paintCueRangeStrips (juce::Graphics&, juce::Rectangle<float> bounds, const State&, const juce::String& binding,
                          presentation::Context);
}
