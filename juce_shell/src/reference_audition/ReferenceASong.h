#pragma once

#include "ReferenceKirinSpectrum.h"
#include "ReferenceLiveWindowLoudness.h"
#include "ReferenceTargetLoudnessWindow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

// C の画面の SONG の A（A FROM START）。A を DAW の時間の位置ごと（100 ms の区切り）に覚え、同じ位置をもう一度
// 鳴らしたら上書きする（ループ・シークで同じ所を何度も聴いても偏らない）。区切りの値は図の A の窓と同じ測り
// （KirinSpectrumMeter の 100 ms のフレームの 64 帯域と 4 帯域の power）に、BS.1770 の K 特性の energy を足した物。
// 音は持たない。
//  - 位置：フレームの頭の DAW の sample をフレームの長さで割って丸めた番号。DAW の頭より前（負）は持たない。
//  - 上限：DAW の頭から 30 分（18,000 区切り）。その後は持たず、鳴らしたことだけを覚える（画面が理由を言う）。
//  - 集計：値のある区切りで、帯域ごとに p10・中央値・p90（nearest-rank）、Balance は power の平均、音量は続いた
//    4 区切り（400 ms）ごとの LUFS-M をゲートつきで積算（BS.1770。値の無い区切りをまたがない）。
//  - 消すのは RESET A WINDOW と、サンプルレート・ch 数の変化だけ。C の曲を替えても残す（A は DAW の曲のまま）。
//    保存はしない（プロジェクトを開き直したら空から）。
// 観測スレッドだけで使う（Audio Thread は触らない。区切りの記憶は観測スレッドが必要な分だけ広げる）。
namespace hypha::reference_audition
{
struct ASongSummary
{
    std::shared_ptr<const KirinSpectrumWindow> spectrum;  // frames は値のある区切りの数。1 つも無ければ null
    double loudness = std::numeric_limits<double>::quiet_NaN();  // ゲートを通る 400 ms が無ければ NaN
    int slots = 0;             // 値のある区切りの数（A FROM START の長さ × 10）
    bool beyondLimit = false;  // 上限より後を鳴らした（その分は持っていない）
};

class ASongMeter
{
public:
    static constexpr int slotLimit = 18'000;  // 30 分
    static constexpr int bands = KirinSpectrumMeter::bandCount;

    // 形式が替わったら全部捨てる（区切りの長さと power の定義が変わる）。
    void configure (int sampleRate, int channelCount)
    {
        if (rate == sampleRate && channels == channelCount) return;
        clear();
        rate = sampleRate;
        channels = std::clamp (channelCount, 1, 2);
        frameLength = sampleRate / 10;
        weighting.configure (sampleRate);
    }
    void clear() noexcept
    {
        std::fill (filled.begin(), filled.end(), std::uint8_t { 0 });
        held = 0;
        beyond = false;
        expected = -1;
        frameEnergy = 0.0;
        ++changes;
    }
    // `spectrum` に同じ塊を足した直後に呼ぶ。`pendingBefore` は足す前の spectrum.pendingFrames()。
    void push (const float* pcm, int frames, std::int64_t position, int pendingBefore, const KirinSpectrumMeter& spectrum)
    {
        if (frameLength <= 0 || pcm == nullptr || frames < 1) return;
        if (position != expected) state = {};  // 途切れ（シーク・停止）：K 特性の状態を新しく
        if (pendingBefore == 0) frameEnergy = 0.0;  // 新しいフレーム（途切れ・RESET の後も）
        for (int index = 0; index < frames; ++index)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto value = weighting.filter (pcm[index * channels + channel], state[static_cast<size_t> (channel)]);
                frameEnergy += value * value;
            }
            if ((pendingBefore + index + 1) % frameLength != 0) continue;
            if (spectrum.framesMade() != made)  // 図の A と同じフレームが閉じた（同じ区切り）
            {
                made = spectrum.framesMade();
                store (position + index + 1 - frameLength, spectrum);
            }
            frameEnergy = 0.0;
        }
        expected = position + frames;
    }
    // 何か変わるたびに進む（集計し直すかを観測スレッドが見る）。
    std::uint64_t revision() const noexcept { return changes; }
    int slotsHeld() const noexcept { return held; }

    std::shared_ptr<const ASongSummary> summary() const
    {
        auto result = std::make_shared<ASongSummary>();
        result->slots = held;
        result->beyondLimit = beyond;
        if (held == 0) return result;
        auto window = std::make_shared<KirinSpectrumWindow>();
        window->centersHz = centers;
        window->frames = window->wantedFrames = held;
        window->p10Db.resize (bands);
        window->medianDb.resize (bands);
        window->p90Db.resize (bands);
        std::vector<float> values;
        values.reserve (static_cast<size_t> (held));
        for (int band = 0; band < bands; ++band)
        {
            values.clear();
            for (size_t slot = 0; slot < filled.size(); ++slot)
                if (filled[slot] != 0) values.push_back (levels[slot * bands + static_cast<size_t> (band)]);
            window->p10Db[static_cast<size_t> (band)] = rank (values, 0.1);
            window->medianDb[static_cast<size_t> (band)] = rank (values, 0.5);
            window->p90Db[static_cast<size_t> (band)] = rank (values, 0.9);
        }
        std::array<double, 4> sum {};
        std::vector<double> momentary;
        for (size_t slot = 0; slot < filled.size(); ++slot)
        {
            if (filled[slot] == 0) continue;
            for (size_t band = 0; band < 4; ++band) sum[band] += balance[slot * 4 + band];
            if (slot < 3 || filled[slot - 1] == 0 || filled[slot - 2] == 0 || filled[slot - 3] == 0) continue;
            const auto block = (energy[slot] + energy[slot - 1] + energy[slot - 2] + energy[slot - 3]) / 4.0;
            momentary.push_back (block > 0.0 ? -0.691 + 10.0 * std::log10 (block) : std::numeric_limits<double>::quiet_NaN());
        }
        for (size_t band = 0; band < 4; ++band)
            window->balanceDb[band] = 10.0 * std::log10 (std::max (sum[band] / held, 1.0e-30));
        result->spectrum = std::move (window);
        result->loudness = gatedWindowLoudness (momentary).lufs;
        return result;
    }

private:
    void store (std::int64_t start, const KirinSpectrumMeter& spectrum)
    {
        if (start + frameLength / 2 < 0) return;  // DAW の頭より前
        const auto slot = (start + frameLength / 2) / frameLength;
        if (slot >= slotLimit)
        {
            if (! beyond) { beyond = true; ++changes; }
            return;
        }
        std::array<float, bands> frameBands {};
        std::array<double, 4> frameBalance {};
        if (! spectrum.lastFrame (frameBands, frameBalance)) return;
        const auto index = static_cast<size_t> (slot);
        if (index >= filled.size())
        {
            const auto size = std::min<size_t> (slotLimit, std::max (index + 1, filled.size() * 2));
            filled.resize (size, 0);
            levels.resize (size * bands, 0.0f);
            balance.resize (size * 4, 0.0);
            energy.resize (size, 0.0);
        }
        if (centers.empty()) centers = spectrum.centersHz();
        std::copy (frameBands.begin(), frameBands.end(), levels.begin() + static_cast<std::ptrdiff_t> (index * bands));
        for (size_t band = 0; band < 4; ++band) balance[index * 4 + band] = std::pow (10.0, frameBalance[band] / 10.0);
        energy[index] = frameEnergy / frameLength;
        if (filled[index] == 0) { filled[index] = 1; ++held; }
        ++changes;
    }
    // KirinSpectrumMeter と同じ nearest-rank（並べた値の round((n − 1)·ratio) 番目）。
    static float rank (std::vector<float>& values, double ratio) noexcept
    {
        const auto index = std::min (static_cast<size_t> (std::llround (static_cast<double> (values.size() - 1) * ratio)),
                                     values.size() - 1);
        std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (index), values.end());
        return values[index];
    }

    int rate = 0, channels = 0, frameLength = 0, held = 0;
    KWeighting weighting;
    std::array<KWeighting::State, 2> state {};
    double frameEnergy = 0.0;
    std::int64_t expected = -1;
    std::uint64_t made = 0, changes = 0;
    bool beyond = false;
    std::vector<double> centers;
    std::vector<std::uint8_t> filled;  // 区切りごと：値がある
    std::vector<float> levels;         // 区切り × 64（dBFS）
    std::vector<double> balance;       // 区切り × 4（power）
    std::vector<double> energy;        // 区切りごとの K 特性の mean square（ch の和）
};
}
