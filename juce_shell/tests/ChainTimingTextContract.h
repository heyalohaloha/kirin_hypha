#pragma once

#include "../src/HyphaChainTimingText.h"
#include "../src/HyphaLanguage.h"

#include <cstdlib>
#include <iostream>

// The chain timing lines of POST's information menu: a measurement says what it is, a reading
// that was not measured gives its reason and no number, and both read in Japanese (INV-S40).
namespace hypha::tests
{
inline void verifyChainTimingTextContract()
{
    const auto require = [] (bool value, const char* message)
    {
        if (! value) { std::cerr << "Chain timing text: " << message << '\n'; std::exit (1); }
    };
    const auto japanese = [] (const juce::String& english)
    { return i18n::translate (english, i18n::Language::japanese); };
    const auto utf8 = [] (const char* text) { return juce::String::fromUTF8 (text); };
    using live_compare::ChainTimingReason;
    using live_compare::ChainTimingView;

    ChainTimingView measured;
    measured.state = ChainTimingView::State::measuring;
    measured.typicalMs = 0.4189; measured.peakMs = 12.34; measured.blockMs = 10.6667;
    measured.typicalLoad = 0.03927; measured.peakLoad = 1.157;
    measured.countedShare = 1.0;
    auto lines = chain_timing::lines (measured);
    require (lines.size() == 2, "a complete measurement is two lines");
    require (lines[0] == "Elapsed 0.42 ms typical / 12.3 ms peak", "times keep about three places");
    require (lines[1] == "Share of a 10.7 ms block: 3.93% typical / 115.7% peak",
             "the share names the block it is a share of, and is not capped at 100%");
    require (japanese (lines[0]) == utf8 (u8"経過時間  通常 0.42 ms / ピーク 12.3 ms"), "Japanese time line");
    require (japanese (lines[1]) == utf8 (u8"ブロック長（10.7 ms）に対する割合  通常 3.93% / ピーク 115.7%"),
             "Japanese share line");

    measured.typicalMs = 0.0042; measured.peakMs = 0.0105;
    require (chain_timing::lines (measured)[0] == "Elapsed 0.004 ms typical / 0.011 ms peak",
             "a chain of a few microseconds does not read as zero");

    measured.countedShare = 0.62;
    measured.reason = ChainTimingReason::unevenBlocks;
    lines = chain_timing::lines (measured);
    require (lines.size() == 3
                 && lines[2] == "Counted 62.0% of blocks (PRE and POST get different block lengths)",
             "a partly counted measurement says so and why");
    require (japanese (lines[2]) == utf8 (u8"計測できたブロック 62.0%（PREとPOSTのブロック長が違います）"),
             "the reason inside the line is translated too");

    ChainTimingView unavailable;
    unavailable.state = ChainTimingView::State::unavailable;
    for (const auto reason : { ChainTimingReason::noPre, ChainTimingReason::notPlaying,
                               ChainTimingReason::preFeeding, ChainTimingReason::otherThread,
                               ChainTimingReason::unevenCalls, ChainTimingReason::unevenBlocks,
                               ChainTimingReason::orderUnproven })
    {
        unavailable.reason = reason;
        lines = chain_timing::lines (unavailable);
        require (lines.size() == 1 && lines[0].startsWith ("Not measured: ")
                     && lines[0].length() > 20 && ! lines[0].containsAnyOf ("0123456789"),
                 "no number stands in for a reading that was not measured");
        require (japanese (lines[0]).startsWith (utf8 (u8"計測できません："))
                     && ! japanese (lines[0]).containsAnyOf ("abcdefghijklmnopqrstuvwxyz"),
                 "every reason has Japanese");
    }
    unavailable.reason = ChainTimingReason::notPlaying;
    require (japanese (chain_timing::lines (unavailable)[0]) == utf8 (u8"計測できません：再生が止まっています"),
             "the reason is translated inside its sentence");

    lines = chain_timing::lines ({});
    require (lines.size() == 1 && lines[0] == "Waiting for audio callbacks"
                 && japanese (lines[0]) == utf8 (u8"音声の処理が始まるのを待っています"),
             "before any callback there is nothing to show");
    require (japanese ("PRE to POST chain") == utf8 (u8"PREからPOSTまでのチェーン")
                 && japanese ("Elapsed time between PRE and POST, not CPU usage")
                     == utf8 (u8"PREとPOSTの間の経過時間です。CPU使用率ではありません"),
             "the heading and the note that it is not CPU usage");
}
}
