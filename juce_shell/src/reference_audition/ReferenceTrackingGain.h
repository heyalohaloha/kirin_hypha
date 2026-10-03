#pragma once

#include "ReferenceLiveWindowLoudness.h"
#include "ReferenceRuntimeV2Measurement.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace hypha::reference_audition
{
struct VisualBinding;

// H3: B・V の追従 gain。live PRE/POST 比較の AUTO（INV-LC16）と同じ値で動かす：1 秒ごとに測り直し、
// 0.5 dB 以内は動かさず、動かすときは Audio Thread で 50 ms の直線の ramp。上限（True Peak）を超える
// gain と、利用者の MATCH の gain から ±6 dB を超える gain には動かさず、追従を止めて理由を出す
// （R-28：利用者が選んだ試聴のことは黙らない）。
// 動かすのは参照の試聴コピーだけで、A は動かさない。C は Match の後に固定（H4）で、追従しない。
inline constexpr double trackingIntervalSeconds = 1.0;
inline constexpr double trackingToleranceDb = 0.5;
inline constexpr double trackingRampSeconds = 0.05;
inline constexpr double trackingRangeDb = 6.0;

enum class TrackingAction : std::uint8_t
{
    keep,        // 0.5 dB 以内、または測れていない（窓が足りない・無音）。今の gain を保つ
    move,        // gainDb へ動かす
    stopCeiling, // gainDb にすると上限を超える。動かさず、追従を止める
    stopRange    // gainDb が MATCH の gain から ±6 dB を超える。動かさず、追従を止める
};

// H12: C の MATCH をもう一度（鳴っている C の gain を今の A の窓で決め直して固定する）の結果。
enum class RematchResult : std::uint8_t
{
    matched,           // 決め直した（gain は 50 ms の ramp で動く）
    notPlaying,        // C が鳴っていない（押すと C を鳴らす。選ぶときに合わせる）
    original,          // Kirin OS で「元の音量」にした Check（合わせない）
    peakMatch,         // True Peak で合わせる Check（選ぶときに合わせる。A を押してから C でやり直す）
    levelUnavailable,  // A の窓（Cue と同じ長さ）が足りない・Cue の値が無い
    ceilingExceeded    // 上限（True Peak）を超える。今の gain を保つ
};

struct TrackingStep
{
    TrackingAction action = TrackingAction::keep;
    double gainDb = 0.0;
};

// 選ぶときと同じ上限：gain を掛けた参照の peak は max(−1 dBTP, A の max TP, 参照の max TP) を超えない。
// 上げられる幅（0 以上）を返す。参照の peak が分からなければ 0（上げない）。
double referenceGainHeadroomDb (double sourcePeakDbtp, double aPeakDbtp) noexcept;

// 1 回の追従。requiredGainDb は今の窓から求めた gain、currentGainDb は掛けている gain、anchorGainDb は
// 利用者の MATCH の gain（NaN なら幅を見ない）。値が無い（NaN）・±100 dB を超えるときは動かさない。
TrackingStep trackingStep (double requiredGainDb, double currentGainDb, double sourcePeakDbtp, double aPeakDbtp,
                           double anchorGainDb = std::numeric_limits<double>::quiet_NaN()) noexcept;

// Audio Thread のみ。gain を rampFrames の直線で目標へ動かす。各フレームを ramp の始点からの直線上に
// 置くので丸めが積もらない。聴こえていないあいだは settle で即座に合わせる（誰にも聴こえない）。
class TrackingGainRamp
{
public:
    float next (float wanted, int rampFrames) noexcept
    {
        if (! (wanted >= target && wanted <= target))
        {
            start = current;
            target = wanted;
            position = 0;
        }
        const int frames = std::max (1, rampFrames);
        if (position < frames)
        {
            ++position;
            current = position >= frames
                ? target
                : start + (target - start) * (static_cast<float> (position) / static_cast<float> (frames));
        }
        return current;
    }

    void settle (float wanted) noexcept
    {
        start = current = target = wanted;
        position = std::numeric_limits<int>::max();
    }

private:
    float start = 1.0f, current = 1.0f, target = 1.0f;
    int position = std::numeric_limits<int>::max();
};

// V：A の直近の点と、位置合わせで対応する V の位置の Kirin OS の LUFS-M（100 ms ごと、添字 i は
// (i+1)·hop で終わる 400 ms）を対にして、両方をそれぞれゲートつきで積算する。差が V の gain。
// 対応が分からない点（DAW の位置が不明・V の外・V の値が無い）は使わない。
struct PairedWindowLoudness
{
    LiveWindowLoudness a, reference;
    int pairs = 0;
};

PairedWindowLoudness pairedWindowLoudness (const std::vector<KirinMeterHistoryEntry>& history,
                                           const VisualBinding& binding,
                                           int windowBlocks = liveWindowBlocks);
}
