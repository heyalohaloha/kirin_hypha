#pragma once

// C の画面の CUE／SONG（2026-11-12）。
//  - Cue の段の右の端に CUE／SONG。押すと比べる値だけを頼む（鳴らし方・MATCH は変えない）。目標・耳で聴き比べる
//    Check では出さない。300% と大きい画面のどちらでも、流し方とずらしの右で枠の中に収まる。
//  - SONG：図と 4 帯域は A FROM START（DAW の頭から）と C の曲全体の値。凡例は「A FROM START 2:41 / C SONG 0:00-4:55」、
//    gain は両方の曲全体の音量をそろえる量（MATCH の読みは鳴らす gain のまま）。A の窓のリストは RESET だけ。
//  - C の曲全体の値が無い（古い Kirin OS）時は CUE の値のまま描き、状態の行が「Kirin OS 1.6.1 で曲全体の値が出ます」。
//    A が DAW の 30 分より後を鳴らした時も状態の行が言う。C を鳴らしている間も後ろの区切りで言う。どれも英日。
//  - SONG は主の図がどれもスペクトルの Check だけ。範囲の帯の Check はどこも CUE のまま、SONG は押せず理由を持つ。
//  - 時間軸は SONG で曲全体を Cue の箱の金で示す。切り替えは枠の中の 2 つの区切りで、文字は 12 px 以上・4.5:1 以上。
#include "ReferenceHoverHelpTest.h"
#include "ReferenceJapaneseScreenTextTest.h"
#include "../src/HyphaReferenceAComparison.h"
#include "../src/HyphaReferenceCueSummary.h"
#include "../src/HyphaReferenceHelpText.h"
#include "../src/HyphaReferenceLegend.h"
#include "../src/HyphaReferenceTargetView.h"
#include "../src/reference_audition/ReferenceASong.h"

#include <algorithm>

namespace hypha::tests
{
inline void verifyReferenceSongComparePage()
{
    using namespace reference_guide_contract;
    const auto japanese = [] (const juce::String& english) { return i18n::translate (english, i18n::Language::japanese); };
    const auto u8 = [] (const char* text) { return juce::String::fromUTF8 (text); };
    const auto render = [] (reference_ui::Component& page, i18n::Language language)
    {
        const i18n::ScopedLanguage scoped (language);
        juce::Image image (juce::Image::ARGB, page.getWidth(), page.getHeight(), true);
        juce::Graphics graphics (image);
        graphics.fillAll (juce::Colour (0xff16110d));
        page.paintEntireComponent (graphics, true);
        return image;
    };
    // 範囲の中の平均の（赤 − 青）：金の地が濃いほど大きい。
    const auto warmth = [] (const juce::Image& image, juce::Rectangle<int> area)
    {
        double sum = 0.0; int count = 0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x, ++count)
            { const auto pixel = image.getPixelAt (x, y); sum += double (pixel.getRed()) - double (pixel.getBlue()); }
        return count > 0 ? sum / count : 0.0;
    };
    const auto shownTexts = [] (reference_ui::Component& page, i18n::Language language)
    {
        const i18n::ScopedLanguage scoped (language);
        juce::Image image (juce::Image::ARGB, page.getWidth(), page.getHeight(), true);
        juce::Graphics graphics (image);
        text_style::ShownTextLog log;
        page.paintEntireComponent (graphics, true);
        return log.texts().joinIntoString ("|");
    };
    auto check = named ("ready");
    check.separateComparisons = check.libraryReceived = check.osOnline = true;
    check.osAccess = os_access::State::ready;
    check.comparisonSlot = 2;
    check.comparisonMode = "loudness_match";
    check.checks = { { "chk-low/cand-1", "Low End  /  Song 1" } };
    check.checkId = "chk-low/cand-1";
    check.viewBindings = { "spectrum_full" }; check.presentationLayout = "main";
    check.cueStartSeconds = 62.0; check.cueEndSeconds = 92.0; check.sourceDurationSeconds = 295.0; check.cueLoops = true;
    check.cuePart = reference_audition::CuePart::cue;
    const auto aWindow = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 300);
    check.aKirin = aWindow;
    check.cueKirin = kirinWindow (-6.0f, { -16.5, -11.5, -15.0, -28.5 }, 300);
    check.aWindowLoudness = -12.0; check.cueLoudness = -9.0;  // 鳴らす gain は −3 dB
    reference_audition::ASongSummary aSong;
    aSong.spectrum = kirinWindow (2.0f, { -20.0, -20.0, -20.0, -20.0 }, 1'610);
    aSong.slots = 1'610; aSong.loudness = -14.0;  // 2:41
    const auto cSong = kirinWindow (-3.0f, { -23.5, -21.5, -20.7, -20.0 }, 2'950);
    const auto songLoudness = -16.0;  // 両方の曲全体をそろえる gain は +2 dB

    // CUE（既定）：今までどおり。SONG の値を渡しても選ばなければ使わない。
    auto cue = check;
    reference_ui::applySongComparison (cue, &aSong, cSong, songLoudness);
    require (! cue.song.shown && ! cue.song.cMissing && cue.aKirin == aWindow && cue.cueKirin == check.cueKirin
                 && std::abs (reference_ui::chartGainDb (cue) + 3.0) < 1.0e-9,
             "CUE compares the Cue with A's window at the gain C plays at");
    // SONG：図と 4 帯域は A FROM START と C の曲全体、音量は両方の曲全体でそろえる。MATCH の読みは鳴らす gain のまま。
    auto song = check;
    song.song.chosen = true;
    reference_ui::applySongComparison (song, &aSong, cSong, songLoudness);
    require (song.song.shown && song.aKirin == aSong.spectrum && song.cueKirin == cSong
                 && std::abs (reference_ui::chartGainDb (song) - 2.0) < 1.0e-9
                 && std::abs (reference_ui::comparisonGainDb (song) + 3.0) < 1.0e-9
                 && reference_ui::matchReadout (song) == reference_ui::matchReadout (cue),
             "SONG compares both whole songs at the same loudness and leaves MATCH alone");
    require (reference_ui::cueSpectrumLegend (song) == "A FROM START 2:41 / C SONG 0:00-4:55 / " + juce::String (reference_ui::targetRangeLegend)
                 && japanese ("A FROM START 2:41 / C SONG 0:00-4:55") == u8 (u8"A頭から2:41 ／ C曲全体 0:00-4:55"),
             "the SONG legend says how much of A it holds and C's whole span, in both languages: " + reference_ui::cueSpectrumLegend (song));
    {
        auto waiting = check;
        waiting.song.chosen = true;
        reference_audition::ASongSummary short_;
        short_.spectrum = kirinWindow (0.0f, { -20.0, -20.0, -20.0, -20.0 }, 12); short_.slots = 12;
        reference_ui::applySongComparison (waiting, &short_, cSong, songLoudness);
        require (reference_ui::cueSpectrumLegend (waiting).startsWith ("A WAITING / C SONG 0:00-4:55"),
                 "under 3 s of A from the start, A waits");
        reference_ui::applySongComparison (waiting, nullptr, cSong, songLoudness);
        require (waiting.song.shown && waiting.aKirin == nullptr && reference_ui::cueSpectrumLegend (waiting).startsWith ("A WAITING"),
                 "before anything is held, A waits");
    }

    // 画面（300% と大きい画面）：Cue の段の右の端に CUE／SONG。流し方とずらしの右、枠の中。
    for (const auto& [editorWidth, editorHeight, width, height] : { std::tuple { 900, 600, 888, 470 }, std::tuple { 1350, 900, 1332, 705 } })
    {
        reference_ui::Component panel;
        panel.setVisible (true);
        panel.setPresentationContext (presentation::forEditor (editorWidth, editorHeight));
        panel.setSize (width, height);
        panel.setState (check);  // LOOP
        auto* switcher = panel.findChildWithID ("reference-compare");
        auto* cueHalf = dynamic_cast<juce::Button*> (switcher != nullptr ? switcher->findChildWithID ("reference-compare-cue") : nullptr);
        auto* songHalf = dynamic_cast<juce::Button*> (switcher != nullptr ? switcher->findChildWithID ("reference-compare-song") : nullptr);
        auto* ahead = panel.findChildWithID ("reference-offset-ahead");
        require (switcher != nullptr && cueHalf != nullptr && songHalf != nullptr && ahead != nullptr, "the CUE / SONG switch exists");
        auto* follow = panel.findChildWithID ("reference-follow-a");
        require (switcher->isVisible() && panel.getLocalBounds().contains (switcher->getBounds()) && follow != nullptr
                     && follow->getRight() + 8 <= switcher->getX() && switcher->getBottom() > panel.getHeight() - 60
                     && cueHalf->getToggleState() && ! songHalf->getToggleState() && cueHalf->getButtonText() == "CUE 1:02-1:32"
                     && switcher->getWidth() >= reference_ui::SongSwitch::widthFor ("CUE 1:02-1:32", presentation::forEditor (editorWidth, editorHeight)),
                 "in LOOP the switch sits at the right of the Cue row, past the playback controls, and reads CUE with the Cue's times at "
                     + juce::String (width));
        require (shownTexts (panel, i18n::Language::english).contains ("CUE 1:02-1:32 / LOOP / 4:55"),
                 "the Cue bar keeps its words beside the switch in LOOP");
        // FOLLOW A（ずらしの段が一番広い形）：曲全体を鳴らすので CUE に時刻を添えない。時間軸の札は切れない。
        auto followed = check;
        followed.followA = true; followed.followOffsetSeconds = -0.5;
        panel.setState (followed);
        require (switcher->isVisible() && ahead->isVisible() && ahead->getRight() + 8 <= switcher->getX()
                     && panel.getLocalBounds().contains (switcher->getBounds()) && cueHalf->getButtonText() == "CUE",
                 "in FOLLOW A the switch sits past the offset and reads CUE alone at " + juce::String (width));
        bool asked = false, wanted = false;
        panel.onSetCheckSong = [&] (bool next) { asked = true; wanted = next; };
        songHalf->onClick();
        require (asked && wanted, "SONG asks for the whole-song values");
        asked = false;
        cueHalf->onClick();
        require (asked && ! wanted, "CUE asks for the Cue values");
        const auto english = shownTexts (panel, i18n::Language::english);
        require (english.contains ("FOLLOW A -0.50 S / 4:55") && ! english.contains ("1:02-1:32"),
                 "in FOLLOW A the Cue bar keeps its words beside the switch: " + english);
        // 枠で囲んだ 2 つの区切り：文字は 12 px 以上、どの地とも 4.5:1 以上（透明度で薄めない）。選んだ側は明るい地。
        const auto context = presentation::forEditor (editorWidth, editorHeight);
        using Switch = reference_ui::SongSwitch;
        require (Switch::font (context).getHeight() >= 12.0f
                     && reference_ui::contrastRatio (Switch::textColour (true), Switch::selectedFill()) >= 4.5f
                     && reference_ui::contrastRatio (Switch::textColour (false), Switch::frameFill()) >= 4.5f
                     && reference_ui::contrastRatio (Switch::textColour (false), Switch::unavailableFill()) >= 4.5f
                     && Switch::textColour (true).isOpaque() && Switch::textColour (false).isOpaque(),
                 "the switch reads at 12 px or more with 4.5:1 on every ground at " + juce::String (width));
        panel.setState (cue);
        const auto cueImage = render (panel, i18n::Language::english);
        // SONG で描く：凡例・4 帯域の数・説明。A の窓のリストは RESET だけ。
        auto shown = song;
        panel.setState (shown);
        require (songHalf->getToggleState() && ! cueHalf->getToggleState() && songHalf->isEnabled(), "SONG is lit when chosen");
        const auto songImage = render (panel, i18n::Language::english);
        const auto songLeft = songHalf->getBounds().translated (switcher->getX(), switcher->getY()).getTopLeft() + juce::Point<int> (3, 4);
        require (songImage.getPixelAt (songLeft.x, songLeft.y).getPerceivedBrightness()
                     > cueImage.getPixelAt (songLeft.x, songLeft.y).getPerceivedBrightness() + 0.05f,
                 "the chosen side of the switch is filled");
        const auto bar = juce::Rectangle<int>::leftTopRightBottom (follow->getRight() + 12, switcher->getY() + 2,
                                                                   switcher->getX() - 12, switcher->getBottom() - 2);
        require (warmth (songImage, bar) > warmth (cueImage, bar) + 8.0,
                 "in SONG the whole time bar is shaded in the Cue box's gold: " + juce::String (warmth (songImage, bar))
                     + " against " + juce::String (warmth (cueImage, bar)));
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const auto joined = shownTexts (panel, language);
            const auto aText = language == i18n::Language::english ? juce::String ("A FROM START 2:41") : u8 (u8"A頭から2:41");
            const auto cText = language == i18n::Language::english ? juce::String ("C SONG 0:00-4:55") : u8 (u8"C曲全体 0:00-4:55");
            require (joined.contains (aText) && joined.contains (cText) && ! joined.contains ("A LAST"),
                     "the SONG chart names A from the start and C's whole song: " + joined);
            for (size_t band = 0; band < 4; ++band)
            {
                const auto expected = reference_ui::compareBand ({ aSong.spectrum->balanceDb[band], cSong->balanceDb[band] + 2.0 });
                require (expected.number.isEmpty() || joined.contains (expected.number),
                         "each band compares the whole songs at the same loudness: " + expected.text + " in " + joined);
            }
        }
        // KIRIN_REFERENCE_UI_ABCV_OUTPUT=<dir>：SONG の C の画面を両方の言語で PNG に書き出す（見た目の確認用）。
        if (const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_UI_ABCV_OUTPUT", {}); directory.isNotEmpty())
            for (const auto& [language, name] : { std::pair { i18n::Language::english, "en" }, std::pair { i18n::Language::japanese, "ja" } })
            {
                const auto image = render (panel, language);
                juce::FileOutputStream stream { juce::File (directory).getChildFile ("abcv_c_" + juce::String (width) + "_song_" + name + ".png") };
                if (stream.openedOk()) { stream.setPosition (0); stream.truncate(); juce::PNGImageFormat().writeImageToStream (image, stream); }
            }
        const auto helps = helpTextsOf (panel);
        require (helps.count (reference_ui::help_text::songSpectrum) == 1 && helps.count (reference_ui::help_text::songBands) == 1
                     && helps.count (reference_ui::help_text::cueBarSong) == 1
                     && helps.count (reference_ui::help_text::cueSpectrum) == 0 && helps.count (reference_ui::help_text::cueBands) == 0,
                 "SONG's chart, bands and time bar explain what they compare");
        auto* control = dynamic_cast<reference_ui::AWindowControl*> (panel.findChildWithID ("reference-a-window-0"));
        require (control != nullptr && control->isVisible() && control->makeMenu().getNumItems() == 2,  // 見出しと RESET
                 "in SONG the A list only resets A (A FROM START too)");
        panel.setState (cue);
        require (control->makeMenu().getNumItems() > 2, "in CUE the A list chooses the window again");
        // 範囲の帯の Check（主の図が Cue の区間と A の直近で測る）：どこも CUE のまま。SONG は押せず理由を持ち、覚えた SONG は残す。
        auto ranged = check;
        ranged.song.chosen = true; ranged.viewBindings = { "dynamics", "loudness" };
        reference_ui::applySongComparison (ranged, &aSong, cSong, songLoudness);
        panel.setState (ranged);
        require (! ranged.song.shown && ranged.song.chosen && switcher->isVisible() && cueHalf->getToggleState()
                     && ! songHalf->getToggleState() && ! songHalf->isEnabled()
                     && hypha::unavailable::reasonAt (songHalf) == reference_ui::songUnsupportedText,
                 "a range-strip Check keeps CUE, SONG cannot be pressed and says why");
        // 目標と耳で聴き比べる Check は CUE／SONG を持たない。
        auto listening = check;
        listening.listeningChecks = { "chk-low" };
        panel.setState (listening);
        require (! switcher->isVisible(), "a Check heard by ear has nothing to switch");
    }

    // SONG で比べられるのは主の図がどれもスペクトルの Check だけ（ほかは図と 4 帯域のどちらも CUE のまま）。
    for (const auto& views : { std::vector<juce::String> { "dynamics" }, std::vector<juce::String> { "loudness" },
                               std::vector<juce::String> { "stereo" }, std::vector<juce::String> { "waveform" },
                               std::vector<juce::String> { "transient" }, std::vector<juce::String> { "spectrum_full", "dynamics" },
                               std::vector<juce::String> { "spectrum_low" }, std::vector<juce::String> { "spectrum_full", "spectrum_low" } })
    {
        auto viewed = check;
        viewed.song.chosen = true; viewed.viewBindings = views;
        reference_ui::applySongComparison (viewed, &aSong, cSong, songLoudness);
        const bool spectrumOnly = std::all_of (views.begin(), views.end(), [] (const juce::String& view) { return view.startsWith ("spectrum_"); });
        require (viewed.song.shown == spectrumOnly && reference_ui::songSupported (viewed) == spectrumOnly
                     && (spectrumOnly ? viewed.cueKirin == cSong : viewed.cueKirin == check.cueKirin && viewed.aKirin == aWindow),
                 "SONG only where every main chart can show the whole song: " + views.front());
    }
    require (japanese (reference_ui::songUnsupportedText) == u8 (u8"SONGはSpectrumのCheckで使えます。このCheckはCueで比べます。"),
             "the reason SONG cannot be pressed reads in Japanese");

    // 目標を選んだ C は SONG を使わない（選択は覚えたまま）。
    {
        auto target = song;
        reference_audition::MeasuredTarget measured;
        measured.id = "t1"; measured.name = "Target"; measured.integrated = -14.0; measured.channels = 2;
        measured.spectrum = *kirinWindow (0.0f, { -20.0, -20.0, -20.0, -20.0 }, 100);
        reference_audition::TargetSnapshot targets;
        targets.collection.items = { measured }; targets.collection.selected = "t1";
        reference_ui::applyMeasuredTarget (target, targets, true);
        require (target.targetSelected && target.song.chosen && ! target.song.shown
                     && reference_ui::cueSpectrumLegend (target).contains ("C TARGET"),
                 "a target is compared as it was, SONG is kept for later");
    }

    // 使えない時の理由（英日）：C の曲全体の値が無い（CUE のまま描く）、A が 30 分より後。
    {
        auto missing = check;
        missing.song.chosen = true;
        missing.checkStep = reference_ui::SourceStep::ready;
        reference_ui::applySongComparison (missing, &aSong, nullptr, std::numeric_limits<double>::quiet_NaN());
        const auto line = reference_ui::referenceStatusLine (missing).text;
        require (missing.song.cMissing && ! missing.song.shown && missing.cueKirin == check.cueKirin
                     && line == "WHOLE-SONG VALUES COME WITH KIRIN OS 1.6.1"
                     && japanese (line) == u8 (u8"Kirin OS 1.6.1で曲全体の値が出ます")
                     && reference_ui::cueSpectrumLegend (missing).contains ("C CUE"),
                 "without C's whole-song values the page keeps the Cue and says why: " + line + " / " + japanese (line));
        auto beyond = check;
        beyond.song.chosen = true;
        beyond.checkStep = reference_ui::SourceStep::ready;
        auto late = aSong;
        late.beyondLimit = true;
        reference_ui::applySongComparison (beyond, &late, cSong, songLoudness);
        const auto limit = reference_ui::referenceStatusLine (beyond).text;
        require (limit == "A FROM START HOLDS THE DAW'S FIRST 30 MIN" && japanese (limit) == u8 (u8"A頭からはDAWの最初の30分まで"),
                 "A past 30 minutes says what A FROM START holds: " + limit + " / " + japanese (limit));
        // C を鳴らしている間も、後ろの区切りで同じ理由を言う。
        for (auto* playing : { &missing, &beyond })
        {
            playing->bSelected = playing->transportPlaying = playing->audibleHeard = true;
            playing->audibleComparisonSlot = 2;
            playing->tracking = reference_audition::TrackingState::fixed;
            const auto said = reference_ui::referenceStatusLine (*playing).text;
            const auto reason = playing == &missing ? juce::String ("WHOLE-SONG VALUES COME WITH KIRIN OS 1.6.1")
                                                    : juce::String ("A FROM START HOLDS THE DAW'S FIRST 30 MIN");
            const auto reasonJa = playing == &missing ? u8 (u8"Kirin OS 1.6.1で曲全体の値が出ます") : u8 (u8"A頭からはDAWの最初の30分まで");
            require (said.startsWith ("C CUE 1:02-1:32 LOOP") && said.contains ("  /  " + reason) && japanese (said).contains (reasonJa),
                     "while C plays the page still says why SONG is not whole: " + said + " / " + japanese (said));
        }
    }
    verifyReferenceHelpFits ({ reference_ui::help_text::songCompare, reference_ui::help_text::songSpectrum,
                               reference_ui::help_text::songBands, reference_ui::help_text::cueBarSong, reference_ui::songUnsupportedText });
    for (const auto* help : { reference_ui::help_text::songCompare, reference_ui::help_text::songSpectrum, reference_ui::help_text::songBands,
                              reference_ui::help_text::cueBarSong, reference_ui::songUnsupportedText })
        require (i18n::hasTranslation (help), "every SONG help has Japanese: " + juce::String (help));
    require (japanese ("A FROM START") == u8 (u8"A頭から") && japanese ("C SONG") == u8 (u8"C曲全体"),
             "the A list's heading and a bare C SONG read in Japanese");
}
}
