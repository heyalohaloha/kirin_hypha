#pragma once

#include "../src/HyphaComparisonPresentation.h"
#include "../src/HyphaHoverHelpPreference.h"
#include "../src/HyphaJapaneseCatalog.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaTextLookAndFeel.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaUiPreferences.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>

// The Japanese screen text (INV-S40): how the catalog is looked up, what it holds, where the
// choice is kept, and that drawing through text_style leaves English exactly as it was while
// Japanese gets a font with its glyphs.
namespace hypha::tests
{
namespace language_contract
{
inline void require (bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "Language contract failed at line " << line << ": " << expression << '\n';
    std::exit (EXIT_FAILURE);
}

#define KIRIN_LANGUAGE_REQUIRE(expression) \
    hypha::tests::language_contract::require ((expression), #expression, __LINE__)

inline juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

inline juce::String japanese (const juce::String& english)
{
    return i18n::translate (english, i18n::Language::japanese);
}

inline void verifyLookup()
{
    using i18n::Language;
    // English is the source and never changes; neither do labels, units and values in Japanese.
    KIRIN_LANGUAGE_REQUIRE (i18n::translate ("WAITING", Language::english) == "WAITING");
    KIRIN_LANGUAGE_REQUIRE (japanese ("WAITING") == utf8 (u8"待機中"));
    for (const auto* kept : { "LEVEL", "MAX TP", "-14.2", "LUFS", "POST", "2MIX", "" })
        KIRIN_LANGUAGE_REQUIRE (japanese (kept) == kept);

    // Values carried by the English stay where Japanese needs them; one that is itself catalog
    // prose is shown translated; the most specific pattern wins.
    KIRIN_LANGUAGE_REQUIRE (japanese ("WARMING 12 S") == utf8 (u8"準備中 12 S"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("20 to 250 Hz") == utf8 (u8"20〜250 Hz"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("Drum Attack. Click to switch view.")
                            == utf8 (u8"ドラムのアタック。クリックで表示を切り替えます。"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("POST. Short-term loudness over 3 seconds. Showing its maximum "
                                      "since the last Meter Session reset.")
                            == utf8 (u8"POST：3秒間のショートタームラウドネスです。最後にMeter Session"
                                     u8"をリセットしてからの最大値を表示しています。"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("All Keep: 3 ready POSTs") == utf8 (u8"All Keep：準備済みのPOST 3"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("All Keep: 1 ready POST") == utf8 (u8"All Keep：準備済みのPOST 1"));

    // Facts joined by " / " or "  ·  " translate one by one, keeping the separators.
    KIRIN_LANGUAGE_REQUIRE (japanese ("INACTIVE / POST SHARPNESS") == utf8 (u8"入力なし / POST SHARPNESS"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("BLIND STOPPED / A HELD -3.0 dB / RETURN A EXPLICITLY")
                            == utf8 (u8"Blindを中止 / Aを-3.0 dBで保持中 / Aは手動で戻す"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("LOADING B AT PLAYHEAD / KEEP PLAYING")
                            == utf8 (u8"再生位置でBを読み込み中 / 再生を継続"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("B SAMPLE RATE 44.1 TO 48.0 kHz / A REMAINS LIVE")
                            == utf8 (u8"Bのサンプルレート 44.1 → 48.0 kHz / Aは今の音のまま"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("APPROVE C 44.1 TO 48.0 kHz")
                            == utf8 (u8"Cの44.1 → 48.0 kHzを承認"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("Approve B 44.1 to 48.0 kHz for the audition copy only. "
                                      "A stays unchanged.")
                            == utf8 (u8"試聴用コピーのBを44.1 → 48.0 kHzに変換します。Aは変わりません。"));
    KIRIN_LANGUAGE_REQUIRE (japanese (utf8 (u8"Legacy guide  ·  No timed items  ·  Retained"))
                            == utf8 (u8"旧形式のGuide  ·  時刻指定なし  ·  保持"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("ANALYSIS IN USE / POST Kirin") == utf8 (u8"解析を使用中 / POST Kirin"));
    KIRIN_LANGUAGE_REQUIRE (japanese ("L / R") == "L / R");

    // A detail of several lines translates line by line.
    KIRIN_LANGUAGE_REQUIRE (
        japanese ("ANALYSIS IN USE / POST Kirin\nWaiting for Kirin OS")
        == utf8 (u8"解析を使用中 / POST Kirin\nKirin OSを待っています"));

    // The comparison states now reach the screen as the Unicode they are written in, so they meet
    // their catalog entries; they used to be read as ASCII and shown garbled.
    const auto bypassed = comparison_presentation::statusText (KIRIN_COMPARISON_STATE_REJECTED,
                                                                KIRIN_COMPARISON_REASON_PRE_BYPASSED);
    KIRIN_LANGUAGE_REQUIRE (bypassed == utf8 (u8"PRE IS OFF — ENABLE PRE TO COMPARE"));
    KIRIN_LANGUAGE_REQUIRE (japanese (bypassed)
                            == utf8 (u8"PREがオフ — PREを有効にして比較"));

    // The current language follows setCurrent; a scope puts the previous one back.
    KIRIN_LANGUAGE_REQUIRE (i18n::current() == Language::english);
    const auto before = i18n::revision();
    {
        const i18n::ScopedLanguage scoped (Language::japanese);
        KIRIN_LANGUAGE_REQUIRE (i18n::tr ("WAITING") == utf8 (u8"待機中"));
        KIRIN_LANGUAGE_REQUIRE (text_style::shownText ("BYPASSED") == utf8 (u8"バイパス中"));
    }
    KIRIN_LANGUAGE_REQUIRE (i18n::current() == Language::english);
    KIRIN_LANGUAGE_REQUIRE (i18n::revision() == before + 2u);
    KIRIN_LANGUAGE_REQUIRE (i18n::tr ("WAITING") == "WAITING");

    for (const auto* system : { "ja", "ja-JP", "ja_JP", "JA-jp" })
        KIRIN_LANGUAGE_REQUIRE (i18n::languageForSystem (system) == Language::japanese);
    for (const auto* system : { "en", "en-US", "", "zh-Hans", "jav" })
        KIRIN_LANGUAGE_REQUIRE (i18n::languageForSystem (system) == Language::english);
}

// Every entry is English on one side and Japanese on the other, keeps the English values, and is
// the only entry for its English.
inline void verifyCatalog()
{
    std::set<juce::String> seen;
    std::size_t count = 0;
    for (const auto& section : i18n::catalog::sections())
        for (std::size_t index = 0; index < section.count; ++index)
        {
            const auto english = utf8 (section.entries[index].english);
            const auto translation = utf8 (section.entries[index].japanese);
            KIRIN_LANGUAGE_REQUIRE (english.isNotEmpty() && translation.isNotEmpty());
            KIRIN_LANGUAGE_REQUIRE (seen.insert (english).second);
            KIRIN_LANGUAGE_REQUIRE (! requiresJapaneseGlyphs (english));
            KIRIN_LANGUAGE_REQUIRE (requiresJapaneseGlyphs (translation));
            for (int slot = 1; slot <= 3; ++slot)
            {
                const auto placeholder = "%" + juce::String (slot);
                KIRIN_LANGUAGE_REQUIRE (english.contains (placeholder) == translation.contains (placeholder));
            }
            KIRIN_LANGUAGE_REQUIRE (i18n::hasTranslation (english));
            ++count;
        }
    std::cout << "Japanese catalog: " << count << " entries\n";
}

// One-line text fits where its English fits. A status written in capitals is drawn in a fixed
// place; its Japanese takes at most two characters more than its English at 100%. A notice reads
// whole in the one-line strip over the body at 100% (292 px, less its padding).
inline void verifyWidths()
{
    const auto context = presentation::forEditor (300, 200);
    const auto englishFont = monoFont (context, typography::TextRole::status);
    const auto japaneseFont = nativeTextFontLike (englishFont);
    const auto character = japaneseFont.getStringWidthFloat (utf8 (u8"中"));
    auto fits = true;
    for (const auto& section : i18n::catalog::sections())
        for (std::size_t index = 0; index < section.count; ++index)
        {
            const auto english = utf8 (section.entries[index].english);
            const auto translation = utf8 (section.entries[index].japanese);
            if (translation.containsChar ('\n'))
                continue;
            const auto width = japaneseFont.getStringWidthFloat (translation);
            const auto notice = juce::String (section.name) == "notices";
            const auto capitals = ! english.containsAnyOf ("abcdefghijklmnopqrstuvwxyz");
            const auto limit = notice ? 270.0f
                             : capitals ? englishFont.getStringWidthFloat (english) + 2.0f * character + 1.0f
                                        : std::numeric_limits<float>::max();
            if (width > limit)
            {
                std::cerr << "Japanese too wide (" << width << " > " << limit << "): " << english
                          << " -> " << translation << '\n';
                fits = false;
            }
        }
    KIRIN_LANGUAGE_REQUIRE (fits);
}

inline void verifyPreference()
{
    using i18n::Language;
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("kirin-hypha-language-contract", {}, false);
    KIRIN_LANGUAGE_REQUIRE (directory.createDirectory().wasOk());
    const auto file = directory.getChildFile ("ui-preferences.txt");
    {
        // Without a choice the system language decides.
        LanguagePreference japaneseSystem (file, "ja-JP");
        KIRIN_LANGUAGE_REQUIRE (japaneseSystem.language() == Language::japanese);
        LanguagePreference englishSystem (file, "en-US");
        KIRIN_LANGUAGE_REQUIRE (englishSystem.language() == Language::english);

        // A choice made in one binary is read by the other, and wins over the system language.
        KIRIN_LANGUAGE_REQUIRE (japaneseSystem.setLanguage (Language::english));
        LanguagePreference otherBinary (file, "ja-JP");
        KIRIN_LANGUAGE_REQUIRE (otherBinary.language() == Language::english);

        // Hover help and the language share the file without overwriting each other.
        HoverHelpPreference hoverHelp (file);
        KIRIN_LANGUAGE_REQUIRE (hoverHelp.setEnabled (false));
        KIRIN_LANGUAGE_REQUIRE (otherBinary.setLanguage (Language::japanese));
        HoverHelpPreference hoverHelpAgain (file);
        KIRIN_LANGUAGE_REQUIRE (! hoverHelpAgain.isEnabled());
        LanguagePreference englishAgain (file, "en-US");
        KIRIN_LANGUAGE_REQUIRE (englishAgain.language() == Language::japanese);
        KIRIN_LANGUAGE_REQUIRE (ui_preferences::read (file, "show_hover_help") == "0");
        KIRIN_LANGUAGE_REQUIRE (ui_preferences::read (file, "language") == "ja");
        KIRIN_LANGUAGE_REQUIRE (file.loadFileAsString().startsWith ("KIRIN_HYPHA_UI_PREFERENCES_V1\n"));

        // A choice that cannot be saved still holds for the session.
        const auto blocker = directory.getChildFile ("not-a-directory");
        KIRIN_LANGUAGE_REQUIRE (blocker.replaceWithText ("blocker"));
        LanguagePreference unsaved (blocker.getChildFile ("ui-preferences.txt"), "en-US");
        KIRIN_LANGUAGE_REQUIRE (! unsaved.setLanguage (Language::japanese));
        unsaved.refreshNowForTest();
        KIRIN_LANGUAGE_REQUIRE (unsaved.language() == Language::japanese);
    }
    KIRIN_LANGUAGE_REQUIRE (directory.deleteRecursively());
}

inline juce::Image drawn (const std::function<void (juce::Graphics&)>& paint)
{
    juce::Image image (juce::Image::ARGB, 240, 40, true);
    juce::Graphics g (image);
    g.setColour (juce::Colours::white);
    paint (g);
    return image;
}

inline bool samePixels (const juce::Image& left, const juce::Image& right)
{
    for (int y = 0; y < left.getHeight(); ++y)
        for (int x = 0; x < left.getWidth(); ++x)
            if (left.getPixelAt (x, y) != right.getPixelAt (x, y))
                return false;
    return true;
}

inline void verifyDrawing()
{
    const auto context = presentation::forEditor (300, 200);
    const juce::Rectangle<int> area { 0, 0, 240, 40 };
    const auto labelled = [&] (juce::Graphics& g) {
        g.setFont (monoFont (context, typography::TextRole::status));
    };
    // English is drawn exactly as Graphics draws it.
    const auto direct = drawn ([&] (juce::Graphics& g) {
        labelled (g);
        g.drawText ("WAITING", area, juce::Justification::centredLeft, true);
    });
    const auto english = drawn ([&] (juce::Graphics& g) {
        labelled (g);
        text_style::drawText (g, "WAITING", area, juce::Justification::centredLeft);
    });
    KIRIN_LANGUAGE_REQUIRE (samePixels (direct, english));

    // Japanese is drawn in the native font at the caller's height, and the caller's font comes
    // back afterwards.
    const i18n::ScopedLanguage scoped (i18n::Language::japanese);
    const auto native = drawn ([&] (juce::Graphics& g) {
        g.setFont (nativeTextFontLike (monoFont (context, typography::TextRole::status)));
        g.drawText (utf8 (u8"待機中"), area, juce::Justification::centredLeft, true);
    });
    const auto translated = drawn ([&] (juce::Graphics& g) {
        labelled (g);
        text_style::drawText (g, "WAITING", area, juce::Justification::centredLeft);
        KIRIN_LANGUAGE_REQUIRE (g.getCurrentFont() == monoFont (context, typography::TextRole::status));
    });
    KIRIN_LANGUAGE_REQUIRE (samePixels (native, translated));
    KIRIN_LANGUAGE_REQUIRE (! samePixels (english, translated));
    KIRIN_LANGUAGE_REQUIRE (std::abs (text_style::shownWidth (monoFont (context, typography::TextRole::status),
                                                              "WAITING")
                                      - nativeTextFontLike (monoFont (context, typography::TextRole::status))
                                            .getStringWidthFloat (utf8 (u8"待機中"))) < 0.01f);
}

// Japanese breaks where Japanese may break: no line starts with closing punctuation, none ends with
// an opening bracket, and a Latin word stays whole.
inline void verifyLineBreaking()
{
    const auto font = nativeTextFontLike (labelFont (presentation::forEditor (300, 200),
                                                     typography::TextRole::body));
    const auto sentence = utf8 (u8"DAW：取り込んだ範囲をもう一度再生してください。次のソースは自動で選ばれます。");
    // Just too narrow for the closing 。: it must not stand alone at the start of a line, and
    // rather than leave "す。" behind, the paragraph breaks between its two sentences.
    const auto width = font.getStringWidthFloat (sentence.dropLastCharacters (1)) + 0.5f;
    const auto lines = text_style::japaneseLines (sentence, font, width);
    KIRIN_LANGUAGE_REQUIRE (lines.size() == 2
                            && lines[0] == utf8 (u8"DAW：取り込んだ範囲をもう一度再生してください。")
                            && lines[1] == utf8 (u8"次のソースは自動で選ばれます。"));
    for (auto narrow = 60.0f; narrow < 400.0f; narrow += 5.0f)
        for (const auto& line : text_style::japaneseLines (sentence, font, narrow))
            KIRIN_LANGUAGE_REQUIRE (! line.startsWith (utf8 (u8"。")) && ! line.startsWith (utf8 (u8"、")));

    const auto brand = utf8 (u8"「Kirin OSについて」：製品、試用、購入の情報です。");
    for (auto narrow = 40.0f; narrow < 200.0f; narrow += 7.0f)
        for (const auto& line : text_style::japaneseLines (brand, font, narrow))
        {
            KIRIN_LANGUAGE_REQUIRE (! line.endsWith (utf8 (u8"「")));
            KIRIN_LANGUAGE_REQUIRE (! line.startsWith (utf8 (u8"」")) && ! line.startsWith (utf8 (u8"、")));
            KIRIN_LANGUAGE_REQUIRE (! line.endsWith ("Kiri") && ! line.startsWith ("rin"));
        }
    // The height is those lines with a fifth of a line between them.
    for (const auto columns : { 1000, 160, 90 })
    {
        const auto count = static_cast<float> (
            text_style::japaneseLines (sentence, font, static_cast<float> (columns)).size());
        KIRIN_LANGUAGE_REQUIRE (std::abs (text_style::wrappedHeight (sentence, font, columns)
                                          - (count * font.getHeight() + (count - 1.0f) * 0.2f * font.getHeight()))
                                < 0.01f);
    }
}

// JUCE's own label and button text goes through the same translation, sized in the native font.
inline void verifyLookAndFeel()
{
    TextLookAndFeel lookAndFeel;
    juce::TextButton button ("CANCEL");
    button.setLookAndFeel (&lookAndFeel);
    const auto height = 24;
    const auto englishWidth = lookAndFeel.getTextButtonWidthToFitText (button, height);
    juce::LookAndFeel_V4 stock;
    KIRIN_LANGUAGE_REQUIRE (englishWidth == stock.getTextButtonWidthToFitText (button, height));
    {
        const i18n::ScopedLanguage scoped (i18n::Language::japanese);
        const auto font = nativeTextFontLike (lookAndFeel.getTextButtonFont (button, height));
        KIRIN_LANGUAGE_REQUIRE (lookAndFeel.getTextButtonWidthToFitText (button, height)
                                == font.getStringWidth (utf8 (u8"中止")) + height);
    }
    button.setLookAndFeel (nullptr);

    // A label keeps its English; only its drawing changes.
    juce::Label label ({}, "WAITING");
    label.setLookAndFeel (&lookAndFeel);
    label.setFont (monoFont (presentation::forEditor (300, 200), typography::TextRole::status));
    label.setSize (160, 24);
    const auto englishLabel = label.createComponentSnapshot (label.getLocalBounds(), true, 1.0f);
    {
        const i18n::ScopedLanguage scoped (i18n::Language::japanese);
        const auto japaneseLabel = label.createComponentSnapshot (label.getLocalBounds(), true, 1.0f);
        KIRIN_LANGUAGE_REQUIRE (label.getText() == "WAITING");
        KIRIN_LANGUAGE_REQUIRE (! samePixels (englishLabel, japaneseLabel));
    }
    KIRIN_LANGUAGE_REQUIRE (samePixels (englishLabel,
                                        label.createComponentSnapshot (label.getLocalBounds(), true, 1.0f)));
    label.setLookAndFeel (nullptr);

    // Wrapped Japanese keeps the label's vertical justification: set to the top, it starts there.
    juce::Label top ({}, "The comparison is closed. Live output is restored.");
    top.setLookAndFeel (&lookAndFeel);
    top.setJustificationType (juce::Justification::topLeft);
    top.setFont (labelFont (presentation::forEditor (300, 200), typography::TextRole::body));
    top.setColour (juce::Label::textColourId, juce::Colours::white);
    top.setSize (160, 120);
    {
        const i18n::ScopedLanguage scoped (i18n::Language::japanese);
        const auto image = top.createComponentSnapshot (top.getLocalBounds(), true, 1.0f);
        auto firstRow = -1;
        for (int y = 0; y < image.getHeight() && firstRow < 0; ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (image.getPixelAt (x, y).getAlpha() > 128) { firstRow = y; break; }
        KIRIN_LANGUAGE_REQUIRE (firstRow >= 0 && firstRow < 12);
    }
    top.setLookAndFeel (nullptr);
}
}

inline void verifyLanguageContract()
{
    language_contract::verifyLookup();
    language_contract::verifyCatalog();
    language_contract::verifyWidths();
    language_contract::verifyPreference();
    language_contract::verifyDrawing();
    language_contract::verifyLineBreaking();
    language_contract::verifyLookAndFeel();
}
}
