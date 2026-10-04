#pragma once

// 2026-10-04（Daisuke が「帯を敷き、差を1つ出す」を選んだ）：B・C・V のスペクトルの図の右上の
// 「1k vs 300-400·3-4k / A 1.2 dB LOWER」。300-400 Hz・3-4 kHz に対する 1 kHz の高さを A を主語に言う（同じ日に
// 「A を主語に言葉で」）。日本語では「300-400·3-4kに対する1k / Aが1.2 dB低い」。差が出せなければ何も出さず、
// 3–4 kHz まで描かない図（LOW FREQUENCY）には帯も出さない。
#include "ReferenceCheckPageTest.h"
#include "../src/HyphaReferenceBlauertZones.h"

namespace hypha::tests
{
inline void verifyReferenceBlauertReadout()
{
    using namespace reference_guide_contract;
    using reference_ui::blauertReadout;
    require (blauertReadout ('C', 1.24) == juce::String (juce::CharPointer_UTF8 ("1k vs 300-400\xc2\xb7" "3-4k / A 1.2 dB LOWER"))
                 && blauertReadout ('B', -2.36) == juce::String (juce::CharPointer_UTF8 ("1k vs 300-400\xc2\xb7" "3-4k / A 2.4 dB HIGHER"))
                 && blauertReadout ('V', -0.04) == juce::String (juce::CharPointer_UTF8 ("1k vs 300-400\xc2\xb7" "3-4k / A SAME AS V"))
                 && blauertReadout ('C', std::numeric_limits<double>::quiet_NaN()).isEmpty(),
             "the Blauert readout says how A's 1 kHz sits against the compared side, and nothing when it cannot be read");
    require (i18n::translate (blauertReadout ('C', 1.24), i18n::Language::japanese)
                 == juce::String (juce::CharPointer_UTF8 ("300-400\xc2\xb7" "3-4k\xe3\x81\xab\xe5\xaf\xbe\xe3\x81\x99\xe3\x82\x8b" "1k / A"
                                                          "\xe3\x81\x8c" "1.2 dB\xe4\xbd\x8e\xe3\x81\x84")),
             "the Blauert readout reads in Japanese");
    require (reference_ui::blauertShown (20.0, 20'000.0) && ! reference_ui::blauertShown (20.0, 300.0)
                 && ! reference_ui::blauertShown (20.0, 250.0),
             "the bands are not drawn on a chart that stops below 3-4 kHz");
    // C の画面の値（ReferenceCheckPageTest の窓）：A と C は 64 帯域の傾きだけが違う。Blauert の差は傾きの差から
    // 決まる（1 kHz の 2 帯域の平均の位置 35.5 と、300-400 Hz・3-4 kHz の平均の位置 36.5 の差 1 帯域ぶん）。
    const auto a = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 300);
    const auto c = kirinWindow (-6.0f, { -16.5, -11.5, -15.0, -28.5 }, 300);
    const auto difference = reference_audition::blauertDifferenceDb (a->centersHz, a->medianDb, c->centersHz, c->medianDb);
    require (std::abs (difference - 6.0 / 63.0) < 1.0e-4 && blauertReadout ('C', difference).endsWith ("A 0.1 dB LOWER"),
             "the C page reads the difference between its two windows");
}
}
