#pragma once

#include "ReferenceKirinSpectrum.h"
#include "ReferenceRuntimeV2Source.h"

#include <limits>
#include <optional>

namespace hypha::reference_audition
{
// H4: Cue の音量。Kirin OS が解析のときに残した 100 ms ごとの値から計算した、Cue 区間の
// BS.1770 のゲートつき Integrated と最大 True Peak（ranges/<sha256>.json、H1 で読む）。
// C はこの値と「A の同じ長さの直近」で Match して固定する。B も鳴らすのは Cue なので、同じ値で追従する。
// 片側だけ直すと悪くなる（方向設計 §7）ので、A 側の窓と組で使う。
struct CueLevel
{
    double integratedLoudness = std::numeric_limits<double>::quiet_NaN();
    double maximumTruePeakDbtp = std::numeric_limits<double>::quiet_NaN();  // 無音だけなら NaN
    // H12: 同じ区間の見比べ。Cue の 64 帯域（p10・中央値・p90）と 4 帯域 Balance（dBFS、gain を掛ける前）。
    // A 側は KirinSpectrumMeter で同じ定義に揃えて測る。ranges がスペクトルを持たなければ空。
    std::shared_ptr<const KirinSpectrumWindow> spectrum;
};

// 選んだ曲の Cue の値を読む。library の sets.json がこの音源の ranges を持ち、その ranges がこの音源
// （sha256・サンプルレート・長さ）のもので、Cue と同じ区間の Integrated があるときだけ値を返す。
// 無ければ空（Kirin OS で測り直すまで、曲全体の値で合わせる）。
std::optional<CueLevel> readCueLevel (const juce::File& root, const RuntimeWorkspace&, const RuntimeCandidate&,
                                      const RuntimeCue&, const RuntimeSource&);

// C の A 側の窓の長さ（10 Hz のブロック数）：Cue と同じ長さ。10 秒より短い Cue は 10 秒
// （短い窓の音量は揺れる）、長い Cue はメーター履歴の 10 分まで。
int cueWindowBlocks (std::int64_t cueStartSample, std::int64_t cueEndSample, std::int64_t sampleRateHz) noexcept;
}
