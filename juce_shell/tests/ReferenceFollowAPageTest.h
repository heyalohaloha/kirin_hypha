#pragma once

// B・C の流し方（LOOP／FOLLOW A）の画面と状態の行（2026-11-11）。
//  - C の画面（300%）は Cue の段の CUE の右に LOOP と FOLLOW A、FOLLOW A のときだけ手のずらし（- / 今の量 / +）。
//    時間軸はその右に残る。押すと B・C の流し方・見ている役のずらしを頼む。
//  - 鳴っている B・C が FOLLOW A なら、状態の行は区間の代わりに「B FOLLOW A」（ずらしがあれば量も）と言う。
//  - A の位置が曲の外：「B PAST SONG END, A PLAYING / MOVE A BACK OR CHOOSE LOOP」など。待っている戻しも同じ文。
//  - 説明はどれも 300% の足元の行（部品の説明の狭い方）に両言語で入り、日本語に英語の文が残らない。
#include "ReferenceHoverHelpTest.h"
#include "ReferenceJapaneseScreenTextTest.h"
#include "../src/HyphaReferenceCueSummary.h"
#include "../src/HyphaReferenceHelpText.h"
#include "../src/HyphaReferencePendingUI.h"

namespace hypha::tests
{
inline void verifyReferenceFollowAPages()
{
    using namespace reference_guide_contract;
    using reference_ui::SourceStep;
    const auto japanese = [] (const juce::String& english) { return i18n::translate (english, i18n::Language::japanese); };
    const auto u8 = [] (const char* text) { return juce::String::fromUTF8 (text); };
    require (reference_ui::followOffsetText (0.0) == "0.00 S" && reference_ui::followOffsetText (0.004) == "0.00 S"
                 && reference_ui::followOffsetText (0.25) == "+0.25 S" && reference_ui::followOffsetText (-1.2) == "-1.20 S",
             "the offset reads in seconds with its sign");

    // 鳴っている B・C の FOLLOW A。区間とループは言わない。
    auto playing = named ("ready");
    playing.separateComparisons = playing.libraryReceived = playing.transportPlaying = playing.bSelected = true;
    playing.osAccess = os_access::State::ready;
    playing.comparisonSlot = playing.audibleComparisonSlot = 3;
    playing.tracking = reference_audition::TrackingState::following;
    playing.followA = true;
    playing.songs = { { "s1", "Song" } }; playing.songId = "s1"; playing.songFacts.resize (1);
    playing.songFacts[0].part = reference_audition::CuePart::chorus;
    playing.songFacts[0].partStartSeconds = 173.0; playing.songFacts[0].partEndSeconds = 187.0;
    auto line = reference_ui::referenceStatusLine (playing).text;
    require (line.startsWith ("B FOLLOW A  /  LEVEL FOLLOWS A (LAST 10 S)") && ! line.contains ("CHORUS") && ! line.contains ("LOOP")
                 && japanese (line).startsWith (u8 (u8"BはAに同期")),
             "B in FOLLOW A says so instead of its looped section, in both languages: " + line + " / " + japanese (line));
    playing.audibleFollowOffsetSeconds = 0.25;
    line = reference_ui::referenceStatusLine (playing).text;
    require (line.startsWith ("B FOLLOW A +0.25 S  /  ") && japanese (line).startsWith (u8 (u8"BはAに同期+0.25秒")),
             "B names its offset: " + line + " / " + japanese (line));
    playing.comparisonSlot = playing.audibleComparisonSlot = 2; playing.audibleFollowOffsetSeconds = -1.2;
    playing.tracking = reference_audition::TrackingState::fixed;
    line = reference_ui::referenceStatusLine (playing).text;
    require (line.startsWith ("C FOLLOW A -1.20 S  /  LEVEL MATCHED AND FIXED") && japanese (line).startsWith (u8 (u8"CはAに同期-1.20秒")),
             "C in FOLLOW A says so with a negative offset: " + line + " / " + japanese (line));

    // 曲の外：A が鳴っていることと、どうすれば聴けるか。押した後に待っている戻しも同じ文。
    auto outside = named ("ready");
    outside.separateComparisons = outside.libraryReceived = outside.osOnline = outside.transportPlaying = outside.aAvailable = true;
    outside.osAccess = os_access::State::ready;
    outside.songSets = { { "set", "Set   1 / 1" } };
    outside.comparisonSlot = 3; outside.followA = true;
    outside.referenceStep = SourceStep::pastSongEnd;
    line = reference_ui::referenceStatusLine (outside).text;
    require (line == "B PAST SONG END, A PLAYING / MOVE A BACK OR CHOOSE LOOP"
                 && japanese (line) == u8 (u8"Bは曲の終わりの後・Aを再生中 ／ Aを戻すかLOOPを選ぶ"),
             "past the song's end the page says A plays and how to hear B: " + line + " / " + japanese (line));
    outside.referenceStep = SourceStep::beforeSongStart;
    line = reference_ui::referenceStatusLine (outside).text;
    require (line == "B BEFORE SONG START, A PLAYING / KEEP PLAYING"
                 && japanese (line) == u8 (u8"Bは曲の頭より前・Aを再生中 ／ 再生を継続"),
             "before the song's start the page says A plays until B starts: " + line + " / " + japanese (line));
    outside.referenceStep = SourceStep::ready;
    for (const auto& [code, expected] : { std::pair { "reference_follow_past_end", "C PAST SONG END, A PLAYING / MOVE A BACK OR CHOOSE LOOP" },
                                          std::pair { "reference_follow_before_start", "C BEFORE SONG START, A PLAYING / KEEP PLAYING" } })
    {
        outside.pendingAudition = { 2, reference_audition::PendingAuditionView::Stage::checking, code };
        require (reference_ui::referenceStatusLine (outside).text == expected,
                 "a C waiting outside the song says why, not a generic wait: " + reference_ui::referenceStatusLine (outside).text);
    }

    // C の画面（300%）の Cue の段。
    auto check = named ("ready");
    check.separateComparisons = check.libraryReceived = check.osOnline = true;
    check.osAccess = os_access::State::ready;
    check.comparisonSlot = 2;
    check.checks = { { "chk-low/cand-1", "Low End  /  Song 1" } };
    check.checkId = "chk-low/cand-1";
    check.cueStartSeconds = 62.0; check.cueEndSeconds = 92.0; check.sourceDurationSeconds = 295.0; check.cueLoops = true;
    reference_ui::Component panel;
    panel.setVisible (true);
    panel.setPresentationContext (presentation::forEditor (900, 600));
    panel.setSize (888, 470);
    panel.setState (check);
    const auto button = [&] (const char* id) { return dynamic_cast<juce::TextButton*> (panel.findChildWithID (id)); };
    auto* loop = button ("reference-loop");
    auto* follow = button ("reference-follow-a");
    auto* back = button ("reference-offset-back");
    auto* offset = button ("reference-offset");
    auto* ahead = button ("reference-offset-ahead");
    require (loop && follow && back && offset && ahead, "the playback controls exist");
    require (loop->isVisible() && follow->isVisible() && loop->getToggleState() && ! follow->getToggleState()
                 && ! back->isVisible() && ! offset->isVisible() && ! ahead->isVisible()
                 && panel.getLocalBounds().contains (follow->getBounds()) && loop->getRight() < follow->getX()
                 && loop->getBottom() > panel.getHeight() - 60,
             "the C page offers LOOP (on) and FOLLOW A in its Cue row");
    // 描いた文（言語ごと）。LOOP の Cue の段は Cue の位置とループを言う。
    const auto shownTexts = [] (reference_ui::Component& page, i18n::Language language)
    {
        const i18n::ScopedLanguage scoped (language);
        juce::Image image (juce::Image::ARGB, page.getWidth(), page.getHeight(), true);
        juce::Graphics graphics (image);
        text_style::ShownTextLog log;
        page.paintEntireComponent (graphics, true);
        return log.texts();
    };
    require (shownTexts (panel, i18n::Language::english).contains ("CUE 1:02-1:32 / LOOP / 4:55")
                 && helpTextsOf (panel).count (reference_ui::help_text::cueBar) == 1,
             "in LOOP the Cue bar names the Cue and its loop");
    bool asked = false, wanted = true;
    panel.onSetFollowA = [&] (bool next) { asked = true; wanted = next; };
    follow->onClick();
    require (asked && wanted, "FOLLOW A asks for the whole song at A's position");
    asked = false;
    loop->onClick();
    require (asked && ! wanted, "LOOP asks for the repeated Cue");
    check.followA = true; check.followOffsetSeconds = 0.5;
    panel.setState (check);
    require (follow->getToggleState() && ! loop->getToggleState() && back->isVisible() && offset->isVisible() && ahead->isVisible()
                 && offset->getButtonText() == "+0.50 S" && back->getRight() < offset->getX() && offset->getRight() < ahead->getX()
                 && panel.getLocalBounds().contains (ahead->getBounds()),
             "FOLLOW A shows the song's offset beside it");
    // KIRIN_REFERENCE_UI_ABCV_OUTPUT=<dir>：FOLLOW A の C の画面を PNG に書き出す（見た目の確認用）。
    if (const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_UI_ABCV_OUTPUT", {}); directory.isNotEmpty())
    {
        juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
        juce::Graphics graphics (image);
        graphics.fillAll (juce::Colour (0xff16110d));
        panel.paintEntireComponent (graphics, true);
        juce::FileOutputStream stream { juce::File (directory).getChildFile ("abcv_c_900_follow_a.png") };
        if (stream.openedOk()) { stream.setPosition (0); stream.truncate(); juce::PNGImageFormat().writeImageToStream (image, stream); }
    }
    int slot = 0; double nudged = 0.0;
    panel.onNudgeFollow = [&] (int role, double seconds) { slot = role; nudged = seconds; };
    panel.onResetFollow = [&] (int role) { slot = -role; };
    ahead->onClick();
    require (slot == 2 && std::abs (nudged - 0.1) < 1.0e-12, "+ moves C's song 0.1 s ahead");
    back->onClick();
    require (slot == 2 && std::abs (nudged + 0.1) < 1.0e-12, "- moves C's song 0.1 s back");
    offset->onClick();
    require (slot == -2, "the offset returns to 0 when pressed");
    // 時間軸は流し方の右に残り、FOLLOW A では曲全体と「FOLLOW A +0.50 S」を言う。Cue とループは言わない（日英）。
    const auto texts = helpTextsOf (panel);
    require (texts.count (reference_ui::help_text::cueBarFollow) == 1 && texts.count (reference_ui::help_text::cueBar) == 0
                 && texts.count (reference_ui::help_text::followA) == 1 && texts.count (reference_ui::help_text::offset) == 1,
             "the Cue bar stays beside the playback controls and explains FOLLOW A");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const auto shown = shownTexts (panel, language);
        const auto joined = shown.joinIntoString ("|");
        require (joined.contains ("FOLLOW A +0.50 S") && joined.contains ("4:55") && ! joined.contains ("1:02-1:32")
                     && ! joined.contains (" LOOP") && ! joined.contains ("LOOP ") && ! joined.contains (juce::String::fromUTF8 (u8"繰り返し")),
                 "in FOLLOW A the Cue bar shows the whole song with its offset, never the Cue or LOOP: " + joined);
    }
    check.followOffsetSeconds = 0.0;
    panel.setState (check);
    require (shownTexts (panel, i18n::Language::english).contains ("FOLLOW A / 4:55"), "no offset, no amount on the Cue bar");
    // B の画面（300%）で B が FOLLOW A で鳴っている：状態の行も描いた文も LOOP と言わない（Balance の凡例は値の区間の名前）。
    {
        reference_ui::Component bPage;
        bPage.setVisible (true);
        bPage.setPresentationContext (presentation::forEditor (900, 600));
        bPage.setSize (888, 470);
        auto b = named ("ready");
        b.separateComparisons = b.libraryReceived = b.osOnline = b.transportPlaying = b.bSelected = true;
        b.osAccess = os_access::State::ready;
        b.comparisonSlot = b.audibleComparisonSlot = 3; b.followA = true; b.referenceWraps = true;
        b.tracking = reference_audition::TrackingState::following;
        b.songSets = { { "set", "Set   1 / 1" } }; b.songSetId = "set";
        b.songs = { { "s1", "Song" } }; b.songId = b.audibleSongId = "s1"; b.songFacts.resize (1);
        b.songFacts[0].part = reference_audition::CuePart::chorus;
        b.songFacts[0].partStartSeconds = 173.0; b.songFacts[0].partEndSeconds = 187.0;
        bPage.setState (b);
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const auto joined = shownTexts (bPage, language).joinIntoString ("|");
            require (! joined.contains (" LOOP") && ! joined.contains (juce::String::fromUTF8 (u8"繰り返し"))
                         && (joined.contains ("B FOLLOW A") || joined.contains (juce::String::fromUTF8 (u8"BはAに同期"))),
                     "B playing in FOLLOW A never reads as a loop on its page: " + joined);
        }
    }
    verifyReferenceHelpFits ({ reference_ui::help_text::loop, reference_ui::help_text::followA, reference_ui::help_text::earlier,
                               reference_ui::help_text::offset, reference_ui::help_text::later, reference_ui::help_text::cueBarFollow });
    for (const auto* help : { reference_ui::help_text::loop, reference_ui::help_text::followA, reference_ui::help_text::earlier,
                              reference_ui::help_text::offset, reference_ui::help_text::later, reference_ui::help_text::cueBarFollow })
        require (i18n::hasTranslation (help), "every playback help has Japanese: " + juce::String (help));
}
}
