#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "reference_audition/ReferenceCuePart.h"

#include <memory>

namespace hypha::reference_audition { struct ASongSummary; struct KirinSpectrumWindow; }

// C の画面の下段。同じ区間・同じ音量で比べる値。
//  - 比べる gain：C が鳴っていればその gain、鳴っていなければ「A の窓 − Cue の Integrated」（選べば
//    この gain で鳴る）。Kirin OS で「元の音量」にした Check は 0（聴こえるとおりに比べる）。
//  - 4 帯域の要約：A の直近の窓の Balance − Kirin OS の Cue の Balance（gain を足す。同じ定義）を、見出し
//    「CよりA（dB）」を主語に言葉で（「3.7少ない」。HyphaReferenceAComparison.h）。良し悪しの色は付けない。
//  - Cue の時間軸：曲の中の Cue の位置とループ、鳴っているときは C の位置の線。
namespace hypha::reference_ui
{
struct State;

double comparisonGainDb (const State&) noexcept;
// 図と 4 帯域が C に足す gain。CUE は comparisonGainDb（鳴らす・鳴らしている gain）。SONG は両方の曲全体の音量を
// そろえる gain（A FROM START の LUFS-I −（C の曲全体の LUFS-I + 鳴らす音が大きく測れる量））で、MATCH の gain ではない。
double chartGainDb (const State&) noexcept;
// SONG を選んでいれば、C の画面の比べる値を A FROM START（a）と C の曲全体（c と その LUFS-I）にする（aKirin・cueKirin を
// 替える）。C の曲全体の値が無ければ CUE の値のまま、state.song.cMissing（状態の行が理由を言う）。
void applySongComparison (State&, const reference_audition::ASongSummary* a,
                          std::shared_ptr<const reference_audition::KirinSpectrumWindow> c, double cLoudness);
// 「A FROM START 2:41」。`slots` は値のある 100 ms の区切りの数。
juce::String songALegend (int slots);
// A と C の値が同じ帯域の並びで揃っているか（64 帯域、中心が 0.1% 以内）。
bool kirinComparable (const State&) noexcept;
void paintBandSummary (juce::Graphics&, juce::Rectangle<int>, const State&, presentation::Context);
// 4 帯域の 1 段が `width` に名前と差の文を省略せずに収まるか（試験が 300% の C の画面の幅で確かめる）。
bool bandSummaryFits (int width, const presentation::Context&);
void paintCueBar (juce::Graphics&, juce::Rectangle<int>, const State&, presentation::Context);
// C の画面の SPECTRUM／LOW FREQUENCY を「Cue 対 A の同じ長さの直近」で描く。描けなければ false（今までの
// 曲全体の分布の表示に戻る）。
bool paintCueSpectrum (juce::Graphics&, juce::Rectangle<float> area, const State&, double minimumHz, double maximumHz,
                       presentation::Context);
juce::String matchReadout (const State&);
// A の窓に実際に入っている長さ：「A LAST 4 S」、1 分からは「A LAST 4:05」。`frames` は 100 ms のフレーム数。
juce::String aWindowLegend (int frames);
// 見比べの凡例：「A LAST 30 S / C CUE / TARGET RANGE」（A の窓が 3 秒に満たなければ A WAITING、音量をそろえられなければ
// LEVEL NOT MATCHED を足す）。
juce::String cueSpectrumLegend (const State&, bool targetRange = true);
// その図に描く凡例。図が低くて凡例が図を潰すときは目標の範囲の札を外す（帯は図に残る）。
juce::String cueChartLegend (juce::Rectangle<float> chart, const juce::String& heading, const State&, presentation::Context);
// 凡例の比べる側の名前（2026-10-04。「A直近10秒 / Bサビ」の形）：「B CHORUS 1:02-1:24」「C WHOLE」など。
// range は区間の時刻を添えるか（B の画面には Cue の時間軸が無いので添える。C は時間軸が言う）。
juce::String cuePartLegend (const char* side, reference_audition::CuePart, double startSeconds, double endSeconds, bool range);
// 鳴っている B・C が曲のどこを鳴らしているか（2026-11-11。B・C は A の位置を追わず Kirin OS の決めた区間を繰り返す）：
// 「B CHORUS 2:53-3:07 LOOP」。区間の名前も時刻も分からなければ空（状態の行は役の名前で言う）。V は A の位置を追うので空。
// FOLLOW A では区間ではなく「B FOLLOW A」（ずらしがあれば「B FOLLOW A +0.25 S」）。
juce::String playingSection (const State&);
// FOLLOW A の手のずらし（秒）：「+0.25 S」「-1.20 S」「0.00 S」。
juce::String followOffsetText (double seconds);
}
