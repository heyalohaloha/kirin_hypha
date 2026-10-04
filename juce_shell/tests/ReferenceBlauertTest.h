#pragma once

// 2026-10-04（Daisuke が「帯を敷き、差を1つ出す」を選んだ）：B・C・V のスペクトルの図の右上の
// 「1k vs 300-400·3-4k C-A +1.2 dB」。比べる側 − A で、日本語では「1kと300-400·3-4kの差」。差が出せなければ
// 何も出さず、3–4 kHz まで描かない図（LOW FREQUENCY）には帯も出さない。
#include "ReferenceCheckPageTest.h"
#include "../src/HyphaReferenceBlauertZones.h"

namespace hypha::tests
{
inline void verifyReferenceBlauertReadout()
{
    using namespace reference_guide_contract;
    using reference_ui::blauertReadout;
    require (blauertReadout ('C', 1.24) == juce::String (juce::CharPointer_UTF8 ("1k vs 300-400\xc2\xb7" "3-4k C-A +1.2 dB"))
                 && blauertReadout ('B', -2.36) == juce::String (juce::CharPointer_UTF8 ("1k vs 300-400\xc2\xb7" "3-4k B-A \xe2\x88\x92" "2.4 dB"))
                 && blauertReadout ('V', -0.04) == juce::String (juce::CharPointer_UTF8 ("1k vs 300-400\xc2\xb7" "3-4k V-A +0.0 dB"))
                 && blauertReadout ('C', std::numeric_limits<double>::quiet_NaN()).isEmpty(),
             "the Blauert readout says the compared side minus A, and nothing when it cannot be read");
    require (i18n::translate (blauertReadout ('C', 1.24), i18n::Language::japanese)
                 == juce::String (juce::CharPointer_UTF8 ("1k\xe3\x81\xa8" "300-400\xc2\xb7" "3-4k\xe3\x81\xae\xe5\xb7\xae C-A +1.2 dB")),
             "the Blauert readout reads in Japanese");
    require (reference_ui::blauertShown (20.0, 20'000.0) && ! reference_ui::blauertShown (20.0, 300.0)
                 && ! reference_ui::blauertShown (20.0, 250.0),
             "the bands are not drawn on a chart that stops below 3-4 kHz");
    // C の画面の値（ReferenceCheckPageTest の窓）：A と C は 64 帯域の傾きだけが違う。Blauert の差は傾きの差から
    // 決まる（1 kHz の 2 帯域の平均の位置 35.5 と、300-400 Hz・3-4 kHz の平均の位置 36.5 の差 1 帯域ぶん）。
    const auto a = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 300);
    const auto c = kirinWindow (-6.0f, { -16.5, -11.5, -15.0, -28.5 }, 300);
    const auto difference = reference_audition::blauertDifferenceDb (a->centersHz, a->medianDb, c->centersHz, c->medianDb);
    require (std::abs (difference - 6.0 / 63.0) < 1.0e-4 && blauertReadout ('C', difference).endsWith ("C-A +0.1 dB"),
             "the C page reads the difference between its two windows");
}
}
