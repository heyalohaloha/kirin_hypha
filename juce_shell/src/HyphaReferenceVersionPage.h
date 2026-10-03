#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "reference_audition/ReferenceVisualTimeline.h"

// H13: V（VERSION）の画面の Check のタブ（方向設計 §4、C と同じ CHECK SET）。位置合わせで対応した同じ区間の
// A と V を、Hypha が同じ定義（Kirin OS の Cue と同じ式、ReferenceKirinSpectrum）で測った値で比べる。
// V は鳴っているときの追従の gain で同じ音量にそろえる（鳴っていなければそろえられないと書く）。
// タブごとに、その Check の表示（view_bindings）に合わせる：低域のスペクトル（spectrum_low）だけの Check は
// 20〜250 Hz を広げて描き、スペクトル・バランスを見ない Check（音量・ダイナミクス・ステレオなど）は V では
// 比べられないと書く（C の画面で見る）。4 帯域の V−A はどのタブでも出す。
// 既定の WHOLE のタブは今までのタイムラインと重ね（LOUDNESS・CREST）。
namespace hypha::reference_ui
{
bool sameSectionReady (const reference_audition::VisualTimeline*) noexcept;
void paintVersionSameSection (juce::Graphics&, juce::Rectangle<int>, const reference_audition::VisualTimeline*,
                              const juce::String& checkLabel, double gainDb, const std::vector<juce::String>& views,
                              presentation::Context);
}
