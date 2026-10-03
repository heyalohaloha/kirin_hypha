#pragma once

#include <juce_core/juce_core.h>

#include "HyphaReferenceGuide.h"

// H6: 準備中を終わらない状態にしない（方向設計 §3.5）。待ちの種類ごとに上限を持ち（仮置き：Kirin OS の
// 応答 5 秒、音源の確認・読み込み・準備 10 秒、位置合わせは再生 30 秒ぶん、MATCH に使う A の音量は再生
// 10 秒ぶん。C は A の直近が Cue の長さ（最長 30 秒）たまるまで合わせないので 35 秒ぶん）、超えたら状態の
// 行を「できない」に変えて理由と直し方を 1 つ出す（R-28）。待ちが進めば（段階・役・Kirin OS のつながりが
// 変われば）数え直し、準備が整えばそのまま聴けるに戻る。音や選択は変えない（表示だけ）。
namespace hypha::reference_ui
{
struct PreparationBudget
{
    static constexpr double kirinOsResponseSeconds = 5.0;
    static constexpr double preparationSeconds = 10.0;
    static constexpr double alignmentPlaySeconds = 30.0;
    static constexpr double aLevelPlaySeconds = 10.0;
    static constexpr double checkALevelPlaySeconds = 35.0;
};

class PreparationWatch
{
public:
    // 見ている役の今の段階を時刻（秒）とともに入れる。上限を超えていれば「理由 / 直し方」の 1 行（英語、
    // 画面で訳す）、超えていなければ空。measuringA は MATCH のために A の音量を待っているとき。
    juce::String observe (int slot, SourceStep step, bool measuringA, bool kirinOsOnline, bool playing, double nowSeconds);

private:
    int watchedSlot = -1;
    SourceStep watchedStep = SourceStep::ready;
    bool watchedA = false, watchedOnline = false;
    double since = 0.0, played = 0.0, last = 0.0;
};
}
