#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "reference_audition/ReferenceVisualTimeline.h"

// H13: V（VERSION）の画面の Check のタブ（方向設計 §4、C と同じ CHECK SET）。位置合わせで対応した同じ区間の
// A と V を、Hypha が同じ定義（Kirin OS の Cue と同じ式、ReferenceKirinSpectrum）で測った値で比べる。
// V は鳴っているときの追従の gain で同じ音量にそろえる（鳴っていなければそろえられないと書く）。
// 既定の WHOLE のタブは今までのタイムラインと重ね（LOUDNESS・CREST）。
namespace hypha::reference_ui
{
bool sameSectionReady (const reference_audition::VisualTimeline*) noexcept;
void paintVersionSameSection (juce::Graphics&, juce::Rectangle<int>, const reference_audition::VisualTimeline*,
                              const juce::String& checkLabel, double gainDb, presentation::Context);
}
