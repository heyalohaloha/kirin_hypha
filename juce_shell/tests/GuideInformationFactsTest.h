#pragma once

#include "../src/HyphaGuideInformationFacts.h"
#include "../src/HyphaLanguage.h"

#include <cstdlib>
#include <iostream>

// 2026-10-18：Kirin OS の MASKING ガイドの情報 menu に、利用者が Kirin OS で選んだ周波数範囲（DAW で確認する
// 周波数範囲）を、測った帯域とは別の行で出す（図の重ね表示には出ていたが、menu の文字には無かった）。
namespace hypha::tests
{
inline void verifyGuideInformationFacts()
{
    const auto require = [] (bool valid, const char* message)
    {
        if (valid) return;
        std::cerr << "Guide information facts: " << message << '\n';
        std::exit (EXIT_FAILURE);
    };
    pre_display::DisplaySnapshot display;
    display.primary = "MASKING NEXT 0:01.0";
    display.detail = "Bass x Drums  /  20 Hz to 15.5 kHz";
    display.stateText = "PAUSED";
    pre_display::GuidePresentationSnapshot guide;
    guide.guideAvailable = true;
    guide.payloadKind = "masking";
    guide.sourcePairLabel = "Bass x Drums";
    guide.hasNextMaskingFocus = true;
    guide.nextMaskingFocus.hasBand = true;
    guide.nextMaskingFocus.lowHz = 20.0;
    guide.nextMaskingFocus.highHz = 100.0;
    auto facts = guide_information::facts (display, guide);
    require (facts.contains ("Frequency focus  20 to 100 Hz"), "the next item's chosen range is listed before it starts");
    require (facts.contains ("Sources  Bass x Drums") && facts.contains ("PAUSED"), "the other facts stay");
    guide.hasMaskingFocus = true;
    guide.maskingFocus = guide.nextMaskingFocus;
    guide.maskingFocus.lowHz = 40.0;
    guide.maskingFocus.highHz = 80.0;
    facts = guide_information::facts (display, guide);
    require (facts.contains ("Frequency focus  40 to 80 Hz") && ! facts.contains ("Frequency focus  20 to 100 Hz"),
             "while an item plays, its own chosen range is listed");
    guide.hasMaskingFocus = guide.hasNextMaskingFocus = false;
    facts = guide_information::facts (display, guide);
    require (! facts.joinIntoString ("\n").contains ("Frequency focus"), "no range is invented when none was chosen");
    require (i18n::translate ("Frequency focus  20 to 100 Hz", i18n::Language::japanese)
                 == juce::String::fromUTF8 (u8"確認する周波数範囲  20〜100 Hz"),
             "the line reads in Japanese with the range as written");
}
}
