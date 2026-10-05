// 2026-10-04：Blauert の帯（ReferenceBlauertBands.h）。Kirin OS の 64 帯域（中心は 20 Hz〜20 kHz の等比）で、
// 「1 kHz 付近 −（300–400 Hz と 3–4 kHz の平均）」が範囲に入る帯域だけから出ること、音量合わせの gain に左右され
// ないこと、無音に近い・範囲が無い・帯域の並びが違うときは出さないこと。
#include "reference_runtime_test_support.h"
#include "../src/reference_audition/ReferenceBlauertBands.h"

#include <cmath>

void testReferenceBlauertBands();

namespace
{
using namespace hypha::reference_audition;

std::vector<double> kirinCenters (double maximumHz = 20'000.0)
{
    std::vector<double> centers;
    for (int band = 0; band < 64; ++band) centers.push_back (20.0 * std::pow (maximumHz / 20.0, band / 63.0));
    return centers;
}

std::vector<float> levels (const std::vector<double>& centers, double base, double frontLow, double back, double frontHigh)
{
    std::vector<float> result;
    for (const auto hz : centers)
        result.push_back (static_cast<float> (base + (hz >= 300.0 && hz <= 400.0 ? frontLow : 0.0)
                                              + (hz >= 891.0 && hz <= 1122.0 ? back : 0.0) + (hz >= 3'000.0 && hz <= 4'000.0 ? frontHigh : 0.0)));
    return result;
}

bool matches (double value, double expected) { return std::isfinite (value) && std::abs (value - expected) < 1.0e-6; }
}

void testReferenceBlauertBands()
{
    const auto centers = kirinCenters();
    int counts[3] {};
    for (const auto hz : centers)
        for (size_t zone = 0; zone < blauertZones.size(); ++zone)
            if (hz >= blauertZones[zone].lowHz && hz <= blauertZones[zone].highHz) ++counts[zone];
    require (counts[0] == 3 && counts[1] == 2 && counts[2] == 3,
             "the Kirin OS 64 bands put 3, 2 and 3 centres in 300-400 Hz, around 1 kHz and 3-4 kHz");
    require (matches (blauertContrastDb (centers, levels (centers, -40.0, 0.0, 0.0, 0.0)), 0.0), "a flat spectrum reads 0 dB");
    require (matches (blauertContrastDb (centers, levels (centers, -40.0, 0.0, 3.0, 0.0)), 3.0), "more around 1 kHz reads positive");
    require (matches (blauertContrastDb (centers, levels (centers, -40.0, 2.0, 0.0, 4.0)), -3.0),
             "more at 300-400 Hz and 3-4 kHz reads negative, by their mean");
    require (matches (blauertContrastDb (centers, levels (centers, -34.0, 2.0, 0.0, 4.0)), -3.0),
             "the same song at another level reads the same (independent of the matching gain)");
    // 範囲の外の帯域は値に入らない。
    auto outside = levels (centers, -40.0, 0.0, 3.0, 0.0);
    for (size_t band = 0; band < centers.size(); ++band)
    {
        bool inside = false;
        for (const auto& zone : blauertZones) inside = inside || (centers[band] >= zone.lowHz && centers[band] <= zone.highHz);
        if (! inside) outside[band] = -10.0f;
    }
    require (matches (blauertContrastDb (centers, outside), 3.0), "bands outside the three ranges do not count");
    // 出さないとき（NaN）。
    auto silent = levels (centers, -40.0, 0.0, 0.0, 0.0);
    for (size_t band = 0; band < centers.size(); ++band)
        if (centers[band] >= 891.0 && centers[band] <= 1'122.0) silent[band] = -120.0f;
    require (std::isnan (blauertContrastDb (centers, silent)), "a range near silence (-100 dBFS or below) is not read");
    auto broken = levels (centers, -40.0, 0.0, 0.0, 0.0);
    for (size_t band = 0; band < centers.size(); ++band)
        if (centers[band] >= 3'000.0 && centers[band] <= 4'000.0) { broken[band] = std::numeric_limits<float>::quiet_NaN(); break; }
    require (std::isnan (blauertContrastDb (centers, broken)), "a missing value in a range is not read");
    const auto low = kirinCenters (2'000.0);
    require (std::isnan (blauertContrastDb (low, levels (low, -40.0, 0.0, 0.0, 0.0))), "a spectrum without 3-4 kHz is not read");
    require (std::isnan (blauertContrastDb ({}, {})), "an empty spectrum is not read");
    // 比べるのは同じ帯域の並びで測った 2 つだけ（差の向きは画面の AVersus が決める。ReferenceBlauertTest.h）。
    require (blauertBandsAligned (centers, centers), "spectra on the same bands are compared");
    require (! blauertBandsAligned (low, centers) && ! blauertBandsAligned (centers, low) && ! blauertBandsAligned ({}, {}),
             "spectra on different bands are not compared");
    std::vector<double> shifted = centers;
    shifted[40] *= 1.01;
    require (! blauertBandsAligned (centers, shifted), "one band off by 1% is a different measurement");
}
