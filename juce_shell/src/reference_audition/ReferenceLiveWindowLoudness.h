#pragma once

#include "kirin_hypha_ffi.h"

#include <cstddef>
#include <limits>
#include <vector>

namespace hypha::reference_audition
{
// H2: A の直近 10 秒のゲートつき音量（ITU-R BS.1770）。10 Hz のメーター履歴の LUFS-M は 400 ms の
// ブロックを 100 ms ごとに測った値で、BS.1770 のゲーティング・ブロック（75 % 重なり）そのものなので、
// 最後の 100 点を積算する：絶対ゲート −70 LUFS、相対ゲート（絶対ゲート後の平均 −10 LU）。
// energy の平均を LUFS のまま 10·log10(mean(10^(L/10))) で取る（−0.691 の補正は打ち消し合う）。
// 同じ測定区間（measurement_epoch・run_id）で続く点だけを使う。
struct LiveWindowLoudness
{
    double lufs = std::numeric_limits<double>::quiet_NaN();     // ゲートを通る点が無ければ NaN
    double maxTruePeak = std::numeric_limits<double>::quiet_NaN();
    int blocks = 0;        // 窓に入った点の数（最大 windowBlocks）
    int gatedBlocks = 0;   // ゲートを通った点の数
};

inline constexpr int liveWindowBlocks = 100;          // 10 秒

// H12: 画面が A の窓の音量を読む間隔（メーター履歴を毎フレームは読まない）。メッセージスレッドだけで使う。
struct WindowLoudnessCache
{
    int slot = 0;
    double loudness = std::numeric_limits<double>::quiet_NaN();
    double validUntilMs = 0.0;
};
inline constexpr int liveWindowMinimumGatedBlocks = 30; // 3 秒に満たない窓は使わない

// LUFS-M の並び（古い順）をゲートつきで積算する。非有限の値は無音として扱う。
LiveWindowLoudness gatedWindowLoudness (const std::vector<double>& lufsMomentary) noexcept;

// メーター履歴（古い順）の最後の windowBlocks 点を積算する。
LiveWindowLoudness liveWindowLoudness (const std::vector<KirinMeterHistoryEntry>& history,
                                       int windowBlocks = liveWindowBlocks);
}
