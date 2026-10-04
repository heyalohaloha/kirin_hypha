#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace hypha::reference_audition
{
// Blauert の方向を決める帯（richtungsbestimmende Bänder。J. Blauert「Räumliches Hören」1974）を、A と比べる側
// （B・C・V）の同じ帯の差として比べる（2026-10-04）。
// 方向（前・上・後ろ）そのものは、狭い帯域の音を 1 つ鳴らしたときの聴取実験の結果で、ミックスの方向は測れない。
// 使うのはステレオでの読み替え（300–400 Hz と 3–4 kHz が相対的に強いと前・近い、1 kHz 付近が強いと拡散・遠い）。
// Hypha は近い・遠いとは言わず、dB の差だけを出す（R-22）。聞き慣れた音でないと効きにくい傾向がある。
//  - 範囲：300–400 Hz、1 kHz の 1/3 オクターブ（891–1122 Hz）、3–4 kHz。
//  - 値：Kirin OS の 64 帯域（中心が範囲に入る帯域）の値の平均で「1 kHz −（300–400 Hz と 3–4 kHz の平均）」。
//    曲の中の帯どうしの差なので、音量合わせの gain に左右されない。帯域の値は帯域内の最大 bin で、帯域の幅の
//    違いが値に乗る（白色雑音でも 0 にならない）ので、同じ定義で測った A と比べる側の差だけを出す。
//  - どれかの範囲に帯域が無い、値が有限でない、または −100 dBFS 以下（無音に近い）なら出さない（NaN）。
struct FrequencyZone
{
    double lowHz, highHz;
};

inline constexpr std::array<FrequencyZone, 3> blauertZones { { { 300.0, 400.0 }, { 891.0, 1122.0 }, { 3'000.0, 4'000.0 } } };
inline constexpr double blauertSilenceDb = -100.0;

inline double blauertContrastDb (const std::vector<double>& centersHz, const std::vector<float>& levelsDb) noexcept
{
    std::array<double, 3> means {};
    for (size_t zone = 0; zone < blauertZones.size(); ++zone)
    {
        double sum = 0.0;
        int count = 0;
        for (size_t band = 0; band < centersHz.size() && band < levelsDb.size(); ++band)
        {
            if (centersHz[band] < blauertZones[zone].lowHz || centersHz[band] > blauertZones[zone].highHz) continue;
            const auto level = static_cast<double> (levelsDb[band]);
            if (! std::isfinite (level) || level <= blauertSilenceDb) return std::numeric_limits<double>::quiet_NaN();
            sum += level;
            ++count;
        }
        if (count == 0) return std::numeric_limits<double>::quiet_NaN();
        means[zone] = sum / count;
    }
    return means[1] - (means[0] + means[2]) / 2.0;
}

// 比べる側 − A（dB）。帯域の並びが違う、またはどちらかが出せなければ NaN。
inline double blauertDifferenceDb (const std::vector<double>& aCentersHz, const std::vector<float>& aLevelsDb,
                                   const std::vector<double>& otherCentersHz, const std::vector<float>& otherLevelsDb) noexcept
{
    if (aCentersHz.empty() || aCentersHz.size() != otherCentersHz.size())
        return std::numeric_limits<double>::quiet_NaN();
    for (size_t band = 0; band < aCentersHz.size(); ++band)
        if (! (std::abs (aCentersHz[band] / otherCentersHz[band] - 1.0) <= 1.0e-3))
            return std::numeric_limits<double>::quiet_NaN();
    return blauertContrastDb (otherCentersHz, otherLevelsDb) - blauertContrastDb (aCentersHz, aLevelsDb);
}
}
