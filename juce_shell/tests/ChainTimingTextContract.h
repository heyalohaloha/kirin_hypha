#pragma once

#include "../src/HyphaChainTimingPreference.h"
#include "../src/HyphaChainTimingText.h"
#include "../src/HyphaHoverHelpPreference.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"

#include <cstdlib>
#include <iostream>

// The chain timing lines of POST's information menu: a measurement says what it is, a reading
// that was not measured gives its reason and no number, and both read in Japanese (INV-S40).
// The optional footer readout: off by default, shared by every POST, drawn only where it fits.
namespace hypha::tests
{
inline void verifyChainTimingFooterContract()
{
    const auto require = [] (bool value, const char* message)
    {
        if (! value) { std::cerr << "Chain timing footer: " << message << '\n'; std::exit (1); }
    };
    using live_compare::ChainTimingView;
    ChainTimingView measured;
    measured.state = ChainTimingView::State::measuring;
    measured.typicalMs = 0.8312; measured.peakMs = 14.93;
    measured.typicalLoad = 0.312; measured.peakLoad = 5.573;
    auto readout = chain_timing::footerReadout (measured);
    require (readout.text == "CHAIN LOAD 31% / 557%" && readout.caution,
             "a load (share of the block), never milliseconds that read as a PRE/POST offset; "
             "marked when a block took longer than its own length");
    require (! readout.text.contains ("ms"), "the footer leaves milliseconds to the information menu");
    measured.typicalLoad = 0.0004; measured.peakLoad = 0.99;
    readout = chain_timing::footerReadout (measured);
    require (readout.text == "CHAIN LOAD <1% / 99%" && ! readout.caution,
             "a tiny share is not shown as zero; a peak within the block is not marked");
    readout = chain_timing::footerReadout ({});
    require (readout.text == "CHAIN LOAD --" && ! readout.caution, "nothing measured is dashes, not a zero");
    ChainTimingView unavailable;
    unavailable.state = ChainTimingView::State::unavailable;
    require (chain_timing::footerReadout (unavailable).text == "CHAIN LOAD --", "a refused reading is dashes too");
    require (i18n::translate ("CHAIN LOAD 31% / 557%", i18n::Language::japanese) == "CHAIN LOAD 31% / 557%",
             "the readout is a label and values, the same in Japanese");

    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("kirin-hypha-chain-footer", {}, false);
    require (directory.createDirectory().wasOk(), "temporary preference directory");
    const auto file = directory.getChildFile ("ui-preferences.txt");
    {
        ChainTimingFooterPreference first (file);
        require (! first.isEnabled(), "off until the user turns it on");
        HoverHelpPreference hoverHelp (file);
        require (hoverHelp.setEnabled (false), "hover help shares the file");
        require (first.setEnabled (true), "turning it on is saved");
        ChainTimingFooterPreference otherPost (file);
        require (otherPost.isEnabled(), "every POST reads the same choice");
        HoverHelpPreference hoverHelpAgain (file);
        require (! hoverHelpAgain.isEnabled(), "and it does not overwrite hover help");
        require (otherPost.setEnabled (false), "turning it off is saved");
        first.refreshNowForTest();
        require (! first.isEnabled(), "the other POST follows within its refresh");
        const auto blocker = directory.getChildFile ("not-a-directory");
        require (blocker.replaceWithText ("blocker"), "blocking file");
        ChainTimingFooterPreference unsaved (blocker.getChildFile ("ui-preferences.txt"));
        require (! unsaved.setEnabled (true) && unsaved.isEnabled(),
                 "a choice that cannot be saved still holds for the session");
    }
    directory.deleteRecursively();

    for (const auto preset : observatory::sizePresets)
    {
        observatory::View view (observatory::Role::post);
        view.setSize (preset.width, preset.height);
        juce::Image image (juce::Image::ARGB, preset.width, preset.height, true);
        {
            juce::Graphics graphics (image);
            view.paintEntireComponent (graphics, true);
        }
        require (! view.chainReadoutShownForTest(), "nothing is drawn while it is off");
        view.setChainReadout ("CHAIN LOAD 31% / 557%", true);
        {
            juce::Graphics graphics (image);
            view.paintEntireComponent (graphics, true);
        }
        const bool folded = observatory::footerFolds (preset.density);
        // Opt-in look at the rail for review; the checks below do not depend on it.
        if (const auto preview = juce::SystemStats::getEnvironmentVariable (
                "KIRIN_HYPHA_CHAIN_FOOTER_PREVIEW_DIR", {}); preview.isNotEmpty())
            if (auto output = juce::File (preview).getChildFile ("chain-footer-" + juce::String (preset.width)
                                                                  + ".png").createOutputStream())
            {
                output->setPosition (0);
                output->truncate();
                juce::PNGImageFormat().writeImageToStream (image, *output);
            }
        require (view.chainReadoutShownForTest() == ! folded,
                 "the footer rail carries a measurement from 150%; the folded sizes use the strip");
        // The timing the user keeps in the footer stays there beside any status: a short one keeps
        // the rail, a long one moves to the strip over the body, whole.
        for (const auto* status : { "Keeping", "PRE changed: check PAIR, then MENU > LISTEN to compare again; POST plays "
                                               "meanwhile, and the PRE you chose waits until PAIR shows it again." })
        {
            view.setFeedback (status);
            {
                juce::Graphics graphics (image);
                view.paintEntireComponent (graphics, true);
            }
            require (view.chainReadoutShownForTest() == ! folded,
                     "the chain timing the user keeps stays in the footer beside a status");
        }
    }
}

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
