#pragma once

#include "../src/HyphaReferenceDisplayText.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaTextStyle.h"
#include <cstdlib>
#include <iostream>

// 2026-10-18（日本語の画面）：Kirin OS の標準の名前（CHECK セット・Check・Cue・B セットの既定の名前）は、日本語の
// 画面では Kirin OS の日本語の画面と同じ名前で出す（Check のタブが Low end / Dynamics のまま英語だった）。
// 曲名と利用者が付けた名前は訳さず、Kirin OS が日本語でも英語で出す名前（Stereo・Mono など）は英語のまま。
namespace hypha::tests
{
inline void verifyReferenceJapaneseNames()
{
    const auto check = [] (bool valid, const char* message)
    {
        if (! valid) { std::cerr << "Reference Japanese names: " << message << '\n'; std::exit (1); }
    };
    const auto utf8 = [] (const char* text) { return juce::String::fromUTF8 (text); };
    reference_ui::State state;
    state.separateComparisons = state.libraryReceived = state.osOnline = true;
    state.osAccess = os_access::State::ready;
    state.readiness = reference_ui::Readiness::ready;
    state.title = "Tone";  // 曲名（利用者の名前）
    state.presetId = "saved"; state.checkId = "low"; state.cueId = "whole";
    state.presets = { { "saved", utf8 (u8"All stages · 5 essential checks") }, { "custom", utf8 (u8"夜明けの音") } };
    // C の項目は Check と曲を分けて持つ（Check のタブは Check の名前、曲名は利用者の名前）。
    const auto target = [] (const char* id, const juce::String& check, const juce::String& song)
    { return reference_ui::SelectionOption { id, check + "  /  " + song, check, song }; };
    state.checks = { target ("low/1", "Low end", "Tone"), target ("dynamics/1", "Dynamics", "Song 1"),
                     target ("vocal/1", "Vocal balance", "Song 1"), target ("tone/1", "Tone", "Song 1"),
                     target ("stereo/1", "Stereo", "Song 1"), target ("hook/1", "Hook", "Song 1"),
                     target ("mono/1", "Mono", "Song 1"), target ("own/1", utf8 (u8"QA 自分の音色確認"), "Song 1"),
                     target ("typed/1", "Tone mix", "Song 1"), target ("ja/1", utf8 (u8"低域"), "Song 1") };
    state.cues = { { "whole", "Full track" }, { "loud", "Loudest 30 s" } };
    state.songSets = { { "first", "Set 1   1 / 2" }, { "second", utf8 (u8"セット 2") }, { "mine", "Set 2 mix" } };
    state.checkLabel = "Low end";
    state.status = "PLAY TO AUDITION";
    reference_ui::Component component;
    component.setPresentationContext (presentation::forEditor (900, 600));
    component.setSize (888, 470);
    const auto shownLabels = [] (const std::vector<reference_ui::SelectionOption>& options, bool checkPart = false)
    {
        juce::StringArray shown;
        for (const auto& option : options) shown.add (text_style::shownText (checkPart ? option.checkLabel : option.label));
        return shown;
    };
    {
        const i18n::ScopedLanguage japanese (i18n::Language::japanese);
        component.setState (state);
        const auto& shown = component.state();
        const auto checks = shownLabels (shown.checks, true);  // Check のタブ
        check (checks[0] == utf8 (u8"低域") && checks[1] == utf8 (u8"ダイナミクス")
                   && checks[2] == utf8 (u8"ボーカルのバランス") && checks[3] == utf8 (u8"音色")
                   && checks[5] == utf8 (u8"聴かせどころ") && checks[9] == utf8 (u8"低域"),
               "each Check tab reads as Kirin OS names it in Japanese");
        check (checks[4] == "Stereo" && checks[6] == "Mono", "a name Kirin OS keeps in English stays English");
        check (checks[7] == utf8 (u8"QA 自分の音色確認") && checks[8] == "Tone mix",
               "a name you gave is never translated");
        check (text_style::shownText (shown.checkLabel) == utf8 (u8"低域"), "the chosen Check reads in Japanese too");
        check (text_style::shownText (shown.checks[0].itemLabel) == "Tone"
                   && text_style::shownText (shown.checks[0].label) == utf8 (u8"低域  /  Tone"),
               "a song title next to a Check name stays as written");
        const auto cues = shownLabels (shown.cues);
        check (cues[0] == utf8 (u8"曲全体") && cues[1] == utf8 (u8"最も大きい30秒"), "the automatic Cue names read in Japanese");
        const auto presets = shownLabels (shown.presets);
        check (presets[0] == utf8 (u8"全工程｜基本5項目") && presets[1] == utf8 (u8"夜明けの音"), "CHECK SET names");
        const auto sets = shownLabels (shown.songSets);
        check (sets[0] == utf8 (u8"セット 1   1 / 2") && sets[1] == utf8 (u8"セット 2") && sets[2] == "Set 2 mix",
               "a B set's default name reads as Kirin OS names it, keeping its rank; a typed name stays");
        check (text_style::shownText (shown.title) == "Tone", "a song title is never translated");
        check (i18n::translate ("Dynamics", i18n::Language::japanese) == "Dynamics"
                   && i18n::translate ("Set 2   1 / 3", i18n::Language::japanese) == "Set 2   1 / 3",
               "outside the name fields, simple English stays English");
        check (text_style::shownText ("LOW FREQUENCY") == utf8 (u8"低域"), "the low-frequency chart heading reads in Japanese");
    }
    component.setState (state);
    const auto& english = component.state();
    check (shownLabels (english.checks, true)[0] == "Low end" && shownLabels (english.checks, true)[9] == "Low end"
               && shownLabels (english.songSets)[1] == "Set 2" && shownLabels (english.cues)[0] == "Full track"
               && text_style::shownText ("LOW FREQUENCY") == "LOW FREQUENCY",
           "English shows every standard name in English, as before");
}
}
