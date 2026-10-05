#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace hypha::reference_audition
{
// 上限超えの承認の結果。断ったときは理由を言う（R-28：押した利用者に「下げた」と思わせない）。
enum class LowerAApproval { lowered, postInUse, refused };

// 2026-10-03（R-12）：B・C・V の MATCH が上限（True Peak）を超えるとき、利用者が承認すれば
// A（POST の出力全体）を差だけ下げて合わせる。参照は元の音量のまま（上げない）。承認した量は試聴の後も、
// 利用者が RETURN で戻すまで保ち、急に上げない（live PRE/POST 比較の POST の減衰と同じ）。オフライン書き出し・
// bypass には掛けない。測定と Record は下げる前の A で取る（Reference の段は測定の後にある）。
// Audio Thread は atomic を読んで直線の ramp で掛けるだけ（確保・ロック・I/O なし）。
class HeldAttenuation
{
public:
    static constexpr double lowerSeconds = 0.05;  // 下げるとき（承認・bypass の後に掛け直すとき）
    static constexpr double raiseSeconds = 0.5;   // 上げるとき（RETURN）

    // メッセージスレッド。承認：今より深いときだけ深くする（浅くするのは RETURN だけ）。待っている RETURN は取り消す。
    void hold (double attenuationDb) noexcept
    {
        if (! std::isfinite (attenuationDb) || attenuationDb >= 0.0) return;
        returnWanted.store (false, std::memory_order_release);
        const auto wanted = static_cast<float> (std::pow (10.0, std::max (-60.0, attenuationDb) / 20.0));
        if (wanted < target.load (std::memory_order_acquire)) target.store (wanted, std::memory_order_release);
    }
    void release() noexcept { returnWanted.store (false, std::memory_order_release); target.store (1.0f, std::memory_order_release); }
    // メッセージスレッド。RETURN：鳴っている役が出力を返してから上げる（役の gain は下げた A に合わせてあり、先に上げると
    // 役が自分の音量より大きく鳴る）。上げ始めは Audio Thread が、役の出力が無いブロックで決める（apply）。
    void requestReturn() noexcept
    {
        returnFrom.store (target.load (std::memory_order_acquire), std::memory_order_release);
        returnWanted.store (true, std::memory_order_release);
    }
    // メッセージスレッド。鳴らせなかった承認を取り消すときだけ、承認の前の量に戻す（下げたままにしない）。
    void restore (double attenuationDb) noexcept
    {
        target.store (std::isfinite (attenuationDb) && attenuationDb < 0.0
                          ? static_cast<float> (std::pow (10.0, std::max (-60.0, attenuationDb) / 20.0)) : 1.0f,
                      std::memory_order_release);
    }
    // 利用者が見る下げ幅（RETURN を押したら 0）。
    double targetDb() const noexcept
    { return returnWanted.load (std::memory_order_acquire) ? 0.0 : toDb (target.load (std::memory_order_acquire)); }
    bool held() const noexcept
    { return ! returnWanted.load (std::memory_order_acquire) && target.load (std::memory_order_acquire) < 1.0f; }
    // RETURN の後、通常の音量へ戻り終わるまで（役が出力を返すのを待つあいだと、0.5 秒の上げ）。
    bool returning() const noexcept
    {
        return returnWanted.load (std::memory_order_acquire)
            || (target.load (std::memory_order_acquire) >= 1.0f && actual.load (std::memory_order_acquire) < 1.0f);
    }
    // 掛けている量が目標に着いた（下げ終わる前・RETURN の待ちのあいだに新しい役を鳴らさない。鳴らすと一瞬大きく聴こえる）。
    bool settled() const noexcept
    {
        return ! returnWanted.load (std::memory_order_acquire)
            && std::abs (actual.load (std::memory_order_acquire) - target.load (std::memory_order_acquire)) < 1.0e-5f;
    }

    void prepare (double sampleRate) noexcept
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 48'000.0;
        lowerStep.store (static_cast<float> (1.0 / (lowerSeconds * rate)), std::memory_order_release);
        raiseStep.store (static_cast<float> (1.0 / (raiseSeconds * rate)), std::memory_order_release);
    }

    // Audio Thread。usable でない（オフライン書き出し・bypass）ブロックには掛けない。戻ったら下げ直す（ramp）。
    // rolesSilent：このブロックの始めに、出力を持つ役（B・C・V・Blind）が無い。待っている RETURN はそこで上げ始める
    // （その間に承認で深くしていたら、上げない）。
    void apply (juce::AudioBuffer<float>& buffer, bool usable, bool rolesSilent) noexcept
    {
        if (rolesSilent && returnWanted.load (std::memory_order_acquire))
        {
            auto from = returnFrom.load (std::memory_order_acquire);
            target.compare_exchange_strong (from, 1.0f, std::memory_order_acq_rel);
            returnWanted.store (false, std::memory_order_release);
        }
        const auto wanted = target.load (std::memory_order_acquire);
        if (! usable) { rtLevel = 1.0f; actual.store (1.0f, std::memory_order_release); return; }
        if (rtLevel == 1.0f && wanted == 1.0f) { actual.store (1.0f, std::memory_order_release); return; }
        const auto down = lowerStep.load (std::memory_order_acquire), up = raiseStep.load (std::memory_order_acquire);
        const int frames = buffer.getNumSamples(), channels = buffer.getNumChannels();
        for (int frame = 0; frame < frames; ++frame)
        {
            rtLevel = rtLevel > wanted ? std::max (wanted, rtLevel - down) : std::min (wanted, rtLevel + up);
            for (int channel = 0; channel < channels; ++channel)
                buffer.getWritePointer (channel)[frame] *= rtLevel;
        }
        actual.store (rtLevel, std::memory_order_release);
    }

private:
    static double toDb (float linear) noexcept { return linear >= 1.0f ? 0.0 : 20.0 * std::log10 (std::max (1.0e-6f, linear)); }

    std::atomic<float> target { 1.0f };
    std::atomic<float> actual { 1.0f };
    std::atomic<float> returnFrom { 1.0f };      // RETURN を押したときの target（承認で変わっていたら上げない）
    std::atomic<bool> returnWanted { false };    // RETURN を押した。役が出力を返したら上げる
    std::atomic<float> lowerStep { 1.0f / 2'400.0f }, raiseStep { 1.0f / 24'000.0f };
    float rtLevel = 1.0f;  // Audio Thread のみ
};
}
