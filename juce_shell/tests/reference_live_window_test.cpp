// H2: A の直近 10 秒のゲートつき音量。ffmpeg 8.0.1 の ebur128 と同じ値になることを確かめる。
// 合成信号（48 kHz、997 Hz のサイン −20 dBFS 4 秒 → ピンクノイズ 4 秒 → 無音 2 秒）を
// `ebur128=framelog=verbose` に通した 100 ms ごとの M（LUFS-M）100 点と、その積算 I = −24.0 LUFS。
// サインの区間は相対ゲートで、無音は絶対ゲートで外れる。
#include "reference_runtime_test_support.h"
#include "../src/reference_audition/ReferenceLiveWindowLoudness.h"

#include <cmath>

void testReferenceLiveWindow();

namespace
{
const std::vector<double> ffmpegMomentary {
    -120.7, -120.7, -120.7, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1,
    -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1,
    -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -38.1, -29.2, -26.5, -24.9, -23.8, -23.7,
    -23.8, -23.7, -23.6, -23.6, -23.6, -23.6, -23.6, -23.6, -23.5, -23.5, -23.5, -23.5, -23.7, -23.7, -23.7,
    -23.7, -23.7, -23.7, -23.7, -23.7, -23.7, -23.7, -23.7, -23.7, -23.6, -23.6, -23.6, -23.6, -23.5, -23.5,
    -23.6, -23.6, -23.8, -23.8, -23.7, -25.0, -26.7, -29.7, -56.3, -165.7, -165.7, -165.7, -165.7, -165.7, -165.7,
    -165.7, -165.7, -165.7, -165.7, -165.7, -165.7, -165.7, -165.7, -165.7, -165.7 };
constexpr double ffmpegIntegrated = -24.0;

KirinMeterHistoryEntry entry (double lufs, std::uint64_t epoch = 3, std::uint64_t run = 7)
{
    KirinMeterHistoryEntry value {};
    value.measurement_epoch = epoch;
    value.run_id = run;
    value.observation_count = 1;
    value.resolution = KIRIN_METER_HISTORY_10_HZ;
    value.lufs_m = { lufs, lufs, lufs };
    value.true_peak = { -6.0, -6.0, -6.0 };
    return value;
}
}

void testReferenceLiveWindow()
{
    namespace ref = hypha::reference_audition;
    const auto window = ref::gatedWindowLoudness (ffmpegMomentary);
    require (window.blocks == 100 && window.gatedBlocks == 43, "the sine and the silence fall outside the gates");
    require (std::abs (window.lufs - ffmpegIntegrated) <= 0.1, "the 10 s window matches ffmpeg ebur128 within 0.1 LU");

    // 一定の音量はそのまま、無音だけの窓は値なし。
    require (std::abs (ref::gatedWindowLoudness (std::vector<double> (100, -14.0)).lufs + 14.0) < 1e-9, "a steady level is itself");
    require (std::isnan (ref::gatedWindowLoudness (std::vector<double> (100, -90.0)).lufs), "silence has no loudness");
    require (std::isnan (ref::gatedWindowLoudness ({}).lufs), "an empty window has no loudness");

    // メーター履歴：最後の 100 点だけを使い、測定区間が変わる前の点は使わない。
    std::vector<KirinMeterHistoryEntry> history;
    for (int index = 0; index < 50; ++index) history.push_back (entry (-6.0, 2));   // 前の測定区間
    for (const auto value : ffmpegMomentary) history.push_back (entry (value));
    const auto live = ref::liveWindowLoudness (history);
    require (live.blocks == 100 && std::abs (live.lufs - window.lufs) < 1e-9 && std::abs (live.maxTruePeak + 6.0) < 1e-12,
             "the last 100 points of this measurement");
    std::vector<KirinMeterHistoryEntry> restarted;
    for (int index = 0; index < 80; ++index) restarted.push_back (entry (-30.0, 3, 6));  // 同じ epoch の前の run
    for (int index = 0; index < 20; ++index) restarted.push_back (entry (-12.0));
    const auto fresh = ref::liveWindowLoudness (restarted);
    require (fresh.blocks == 20 && std::abs (fresh.lufs + 12.0) < 1e-9
                 && fresh.gatedBlocks < ref::liveWindowMinimumGatedBlocks,
             "a new run starts its window again, and 2 s are not enough to use");
    std::cout << "Reference live window loudness: " << window.lufs << " LUFS (ffmpeg " << ffmpegIntegrated << ") PASS\n";
}
