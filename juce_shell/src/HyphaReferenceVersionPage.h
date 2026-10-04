#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "reference_audition/ReferenceVisualTimeline.h"

// H13: V（VERSION）の画面のタブ。位置合わせで対応した同じ区間の A と V を、Hypha が同じ定義（Kirin OS の Cue と
// 同じ式、ReferenceKirinSpectrum）で測った値で比べる。V は鳴っているときの追従の gain で同じ音量にそろえる（鳴って
// いなければ位置合わせの gain）。タブごとの表示：スペクトル（spectrum_full）、低域（spectrum_low は 20〜250 Hz を
// 広げる）、範囲の帯（dynamics・transient・stereo など、2 つまで横に）。4 帯域の差（A を主語に言葉で）はどのタブでも
// 出す。既定の WHOLE のタブは今までのタイムラインと重ね（LOUDNESS・CREST）。
// 2026-10-04（Daisuke「Check の各項目はそれぞれ曲が違う…Version が C の項目と一緒ということに違和感がある。V の
// チェック項目はデフォルトで決定していて良い」→「音色・ダイナミクス・ステレオ・低域」）：V のタブは C の CHECK SET
// （項目ごとに参照曲が違う）と切り離した決まった項目。セクション間・Album 全体は同じ区間の A と V の比較にならない。
namespace hypha::reference_ui
{
struct VersionTab
{
    juce::String id, label;
    std::vector<juce::String> views;  // その項目で描く表示（Kirin OS の Check の view_bindings と同じ名前）
};
const std::vector<VersionTab>& versionTabs();
const VersionTab* findVersionTab (const juce::String& id);
bool sameSectionReady (const reference_audition::VisualTimeline*) noexcept;
void paintVersionSameSection (juce::Graphics&, juce::Rectangle<int>, const reference_audition::VisualTimeline*,
                              const juce::String& checkLabel, double gainDb, const std::vector<juce::String>& views,
                              bool listening, presentation::Context);
// 耳で聴き比べる Check の案内（V と C の画面。2026-10-04、図の代わりに。Tone と同じ図を出さない）。
juce::String listeningGuide (char role);
}
