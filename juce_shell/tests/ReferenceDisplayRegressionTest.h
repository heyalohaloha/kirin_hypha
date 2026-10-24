#pragma once

#include "../src/HyphaReferenceDisplayText.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaReferenceVisuals.h"
#include "JapaneseTermsContract.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyReferenceDisplayRegression()
{
    verifyBuiltInTermAliases();
    const auto check = [] (bool valid, const char* message)
    {
        if (!valid) { std::cerr << "Reference display: " << message << '\n'; std::exit (1); }
    };
    reference_ui::State state;
    state.separateComparisons = state.libraryReceived = state.osOnline = true;
    state.osAccess = os_access::State::ready;
    state.readiness = reference_ui::Readiness::ready;
    state.title = "Reference song / session metadata and a deliberately long source name";
    // 組み込みの Preset と Check は Kirin OS の決まった名前空間の ID を持つ（2026-10-18：名前は綴りでなく ID で見分ける）。
    const juce::String builtInSet ("00000000-0000-4000-8000-000000000101"), builtInDynamics ("00000000-0000-4000-8000-000000002001"),
        builtInLow ("00000000-0000-4000-8000-000000002000");
    state.presetId = builtInSet; state.checkId = builtInDynamics; state.cueId = "whole";
    state.presets = { { builtInSet, juce::String::fromUTF8 ("全工程｜基本3項目") },
                      { "custom", juce::String::fromUTF8 ("夜明けの音") } };
    state.checks = { { builtInDynamics, juce::String::fromUTF8 ("ダイナミクス") },
                     { builtInLow, juce::String::fromUTF8 ("低域") } };
    state.cues = { { "whole", juce::String::fromUTF8 ("曲全体") } };
    state.checkLabel = state.checks.front().label;
    state.viewBindings = { "dynamics", "loudness" };
    state.status = "PLAY TO AUDITION";
    // 2026-10-04：Kirin OS の訳の名前は全部英語にそろえ、後ろに
    // 曲名・順位が付いた形にも当てる。名前の途中や、利用者の名前には当てない。
    for (const auto& [japanese, english] : { std::pair { u8"ボーカルのバランス  /  Song 1", "Vocal balance  /  Song 1" },
                                             std::pair { u8"全工程｜基本5項目   1 / 1", u8"All stages · 5 essential checks   1 / 1" },
                                             std::pair { u8"サビ候補", "Chorus candidate" },
                                             std::pair { u8"セット名", u8"セット名" },
                                             std::pair { u8"低域の安定性", "Low-end consistency" },
                                             std::pair { u8"低域", "Low end" },
                                             std::pair { u8"低域ノイズ", u8"低域ノイズ" },
                                             std::pair { u8"Mastering｜音色・音量・ダイナミクス", u8"Mastering · tone, level, and dynamics" } })
        check (reference_ui::standardDisplayName (juce::String::fromUTF8 (japanese)) == juce::String::fromUTF8 (english),
               "every standard name from Kirin OS displays in English, with what follows it");
    // 2026-10-18：画面全体の辞書は名前を綴りで訳さない（同じ綴りの自作の名前を訳さない）。組み込みの ID の項目だけ、
    // 名前の欄で Kirin OS の日本語名になり、順位はそのまま残る。
    for (const auto& pair : reference_ui::standardDisplayNames)
    {
        const auto english = juce::String::fromUTF8 (pair[1]);
        check (i18n::translate (english + "   1 / 3", i18n::Language::japanese) == english + "   1 / 3",
               "the screen dictionary never translates a name by its spelling");
    }
    {
        reference_ui::State ranked;
        ranked.presets = { { "00000000-0000-4000-8000-000000000107", juce::String::fromUTF8 (u8"Mastering · tone, level, and dynamics   1 / 1") },
                           { "a-copy", juce::String::fromUTF8 (u8"Mastering · tone, level, and dynamics   2 / 2") } };
        reference_ui::prepareDisplayNames (ranked, i18n::Language::japanese);
        check (ranked.presets[0].label == juce::String::fromUTF8 (u8"Mastering｜音色・音量・ダイナミクス   1 / 1")
                   && ranked.presets[1].label == juce::String::fromUTF8 (u8"Mastering · tone, level, and dynamics   2 / 2"),
               "the built-in CHECK SET reads as Kirin OS names it in Japanese; a copy without its ID stays as written");
    }
    check (i18n::translate ("Set 2   1 / 3", i18n::Language::japanese) == "Set 2   1 / 3"
               && i18n::translate ("Dynamics", i18n::Language::japanese) == "Dynamics",
           "simple English stays English (no katakana)");
    check (i18n::translate (juce::String::fromUTF8 (u8"夜明けの音   2 / 3"), i18n::Language::japanese)
               == juce::String::fromUTF8 (u8"夜明けの音   2 / 3")
               && i18n::translate ("Custom set   2 / x", i18n::Language::japanese) == "Custom set   2 / x",
           "a name you gave and a text that only looks ranked stay as written");
    reference_ui::Component component;
    for (const auto width : { 300, 375, 450, 600, 900 })
    {
        component.setPresentationContext (presentation::forEditor (width, width * 2 / 3));
        component.setSize (width - 12, width == 900 ? 470 : width * 2 / 3 - 64);
        component.setState (state);
        const auto& shown = component.state();
        check (shown.presets.front().label == juce::String::fromUTF8 (u8"All stages · 3 essential checks") && shown.checkLabel == "Dynamics",
               "built-in names display in English");
        check (shown.cues.front().label == state.cues.front().label,
               "a Cue name Kirin OS gives no built-in ID stays as written");
        check (shown.presets[1].label == state.presets[1].label && shown.title == state.title,
               "custom names and source metadata must be preserved");
        auto* preset = dynamic_cast<juce::ComboBox*> (component.findChildWithID ("reference-preset"));
        check (preset && preset->getText() == juce::String::fromUTF8 (u8"All stages · 3 essential checks"), "selector must use display labels");
        const auto font = preset->getLookAndFeel().getComboBoxFont (*preset);
        check (font.getHeight() <= preset->getHeight(), "selector font must fit its row");
        auto* b = component.findChildWithID ("reference-b");
        auto* c = component.findChildWithID ("reference-c");
        check (b && c && !b->getBounds().intersects (c->getBounds()), "A/B/C controls must stay separate");
        juce::Image image (juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
        juce::Graphics graphics (image); component.paintEntireComponent (graphics, true);
        const auto output = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_DISPLAY_OUTPUT", {});
        if (output.isNotEmpty())
        {
            const auto root = juce::File (output); check (root.createDirectory(), "render directory");
            juce::FileOutputStream stream (root.getChildFile (juce::String (width) + ".png"));
            check (juce::PNGImageFormat().writeImageToStream (image, stream), "render evidence");
        }
    }
    const auto font = nativeTextFont (presentation::forEditor (900, 600), typography::TextRole::body);
    const auto title = juce::String::repeatedString ("Long source title ", 64);
    for (const auto width : { -1.0f, 0.0f, 5.0f, 30.0f, 100.0f, 400.0f })
    {
        const auto fitted = text_style::ellipsizedText (title, font, width);
        check (font.getStringWidthFloat (fitted) <= juce::jmax (0.0f, width), "long title must fit");
    }
    check (text_style::ellipsizedText ("Mix", font, 400.0f) == "Mix", "short title must stay intact");
    check (text_style::ellipsizedText ({}, font, 400.0f).isEmpty(), "empty title must stay empty");

    // A short real measurement can contain a timeline with only null samples.
    // It must render the same unavailable state as a missing timeline.
    const auto plot = [&state] {
        juce::Image image (juce::Image::ARGB, 450, 240, true);
        juce::Graphics graphics (image);
        reference_ui::paintConfiguredReferenceViews (graphics, image.getBounds().toFloat(),
            state, presentation::forEditor (900, 600));
        return image;
    };
    const auto missing = plot();
    auto measurement = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    measurement->dynamics.emplace(); measurement->loudness.emplace();
    measurement->dynamics->series["psr_millidb"] = { std::nullopt, std::nullopt };
    measurement->loudness->series["lufs_s_millilu"] = { std::nullopt, std::nullopt };
    state.detailedMeasurement = measurement;
    const auto unavailable = plot();
    for (int y = 0; y < missing.getHeight(); ++y)
        for (int x = 0; x < missing.getWidth(); ++x)
            check (missing.getPixelAt (x, y) == unavailable.getPixelAt (x, y),
                   "null-only timeline must display NO DATA");
}
}
