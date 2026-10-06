#pragma once

// 2026-10-04：符号つきの差では、A が多いのか少ないのかが分かりにくかった。
// 比べる側との差は A を主語にして言葉で言う（「A 3.7 LESS」、日本語は「Aが3.7少ない」）。符号は付けず、表示の桁で 0 なら
// 「A SAME AS C」。300% の C と V の 4 帯域の欄は、見出し（CよりA（dB））が主語を言い、名前と差の文を省略せずに
// 収める（両言語）。
#include "ReferenceCheckPageTest.h"
#include "../src/HyphaReferenceAComparison.h"
#include "../src/HyphaReferenceComparisonView.h"

namespace hypha::tests
{
inline void verifyReferenceAComparison()
{
    using namespace reference_guide_contract;
    using reference_ui::AWords;
    using reference_ui::compareA;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    // 差は A と比べる側の値の組で渡す（数を直接渡さない）。向きは組の中で一度だけ決まる。
    require (reference_ui::AVersus { -14.1, -13.0 }.aMinusOther() < 0.0 && reference_ui::AVersus { 2.0, 1.0 }.aMinusOther() > 0.0,
             "the pair subtracts the compared side from A");
    require (compareA ({ 1.0, 4.74 }, 1, "", AWords::amount, 'C').text == "A 3.7 LESS"
                 && compareA ({ 1.0, 4.74 }, 1, "", AWords::amount, 'C').number == "3.7"
                 && compareA ({ 5.06, 3.0 }, 1, " dB", AWords::amount, 'V').text == "A 2.1 dB MORE"
                 && compareA ({ -0.123, 0.0 }, 2, "", AWords::level, 'C').text == "A 0.12 LOWER"
                 && compareA ({ -1.0, -2.5 }, 1, " dB", AWords::level, 'B').text == "A 1.5 dB HIGHER"
                 && compareA ({ 0.44, 0.0 }, 1, " dB", AWords::size, 'C').text == "A 0.4 dB LARGER"
                 && compareA ({ 0.0, 0.6 }, 1, " LU", AWords::size, 'C').text == "A 0.6 LU SMALLER"
                 && compareA ({ -13.0, -14.1 }, 1, " LU", AWords::loudness, 'V').text == "A 1.1 LU LOUDER"
                 && compareA ({ -14.1, -13.0 }, 1, " LU", AWords::loudness, 'V').text == "A 1.1 LU QUIETER"
                 && compareA ({ 62.4, 50.0 }, 0, " pt", AWords::width, 'C').text == "A 12 pt WIDER"
                 && compareA ({ 50.0, 62.4 }, 0, " pt", AWords::width, 'C').text == "A 12 pt NARROWER",
             "the difference is A minus the compared side, said with A as the subject and no sign");
    // A が小さい組は、どの言葉の組でも「少ない・低い・小さい・狭い」側の言葉になる。
    for (const auto words : { AWords::amount, AWords::level, AWords::size, AWords::loudness, AWords::width })
    {
        const auto smaller = compareA ({ 1.0, 2.0 }, 1, "", words, 'C').word, larger = compareA ({ 2.0, 1.0 }, 1, "", words, 'C').word;
        require (juce::StringArray { "LESS", "LOWER", "SMALLER", "QUIETER", "NARROWER" }.contains (smaller)
                     && juce::StringArray { "MORE", "HIGHER", "LARGER", "LOUDER", "WIDER" }.contains (larger),
                 "a smaller A is said with the smaller word: " + smaller + " / " + larger);
    }
    require (compareA ({ 0.04, 0.0 }, 1, "", AWords::amount, 'C').text == "A SAME AS C"
                 && compareA ({ 0.0, 0.04 }, 1, "", AWords::amount, 'V').text == "A SAME AS V"
                 && compareA ({ 0.04, 0.0 }, 1, "", AWords::amount, 'C').number.isEmpty()
                 && compareA ({ 0.004, 0.0 }, 2, "", AWords::level, 'C').text == "A SAME AS C"
                 && ! compareA ({ nan, 1.0 }, 1, "", AWords::amount, 'C').shown()
                 && ! compareA ({ 1.0, nan }, 1, "", AWords::amount, 'C').shown(),
             "a difference that rounds to zero says A is the same, and a missing value says nothing");
    const auto japanese = [] (const juce::String& english) { return i18n::translate (english, i18n::Language::japanese); };
    require (japanese ("A 3.7 LESS") == juce::String (juce::CharPointer_UTF8 ("A\xe3\x81\x8c" "3.7\xe5\xb0\x91\xe3\x81\xaa\xe3\x81\x84"))
                 && japanese ("A 2.1 dB MORE") == juce::String (juce::CharPointer_UTF8 ("A\xe3\x81\x8c" "2.1 dB\xe5\xa4\x9a\xe3\x81\x84"))
                 && japanese ("A 0.12 LOWER") == juce::String (juce::CharPointer_UTF8 ("A\xe3\x81\x8c" "0.12\xe4\xbd\x8e\xe3\x81\x84"))
                 && japanese ("A 1.1 LU QUIETER") == juce::String (juce::CharPointer_UTF8 ("A\xe3\x81\x8c" "1.1 LU\xe5\xb0\x8f\xe3\x81\x95\xe3\x81\x84"))
                 && japanese ("A 12 pt WIDER") == juce::String (juce::CharPointer_UTF8 ("A\xe3\x81\x8c" "12 pt\xe5\xba\x83\xe3\x81\x84"))
                 && japanese ("A SAME AS C") == juce::String (juce::CharPointer_UTF8 ("A\xe3\x81\xaf" "C\xe3\x81\xa8\xe5\x90\x8c\xe3\x81\x98"))
                 && japanese ("A VS C (dB)") != "A VS C (dB)" && japanese ("A VS V (dB)") != "A VS V (dB)"
                 && japanese ("A 0.4 dB LARGER") != "A 0.4 dB LARGER" && japanese ("A 0.4 dB SMALLER") != "A 0.4 dB SMALLER"
                 && japanese ("A 1.5 dB HIGHER") != "A 1.5 dB HIGHER" && japanese ("A 1.1 LU LOUDER") != "A 1.1 LU LOUDER"
                 && japanese ("A 12 pt NARROWER") != "A 12 pt NARROWER",
             "the difference reads in Japanese (A\u304c3.7\u5c11\u306a\u3044 / A\u306fC\u3068\u540c\u3058)");

    // 4 帯域の欄（見出し「CよりA（dB）」が主語）：「3.7 LESS」（「3.7少ない」）、0 なら「SAME」（「差なし」）。
    using reference_ui::compareBand;
    require (compareBand ({ 0.0, 3.74 }).text == "3.7 LESS" && compareBand ({ 0.0, 3.74 }).number == "3.7"
                 && compareBand ({ 6.25, 0.0 }).text == "6.3 MORE" && compareBand ({ 0.04, 0.0 }).text == "SAME"
                 && compareBand ({ 0.0, 0.04 }).text == "SAME" && ! compareBand ({ nan, 0.0 }).shown(),
             "a band cell says the amount and the word, with A as the heading's subject");
    require (japanese ("3.7 LESS") == juce::String (juce::CharPointer_UTF8 ("3.7\xe5\xb0\x91\xe3\x81\xaa\xe3\x81\x84"))
                 && japanese ("6.3 MORE") == juce::String (juce::CharPointer_UTF8 ("6.3\xe5\xa4\x9a\xe3\x81\x84"))
                 && japanese ("SAME") == juce::String (juce::CharPointer_UTF8 ("\xe5\xb7\xae\xe3\x81\xaa\xe3\x81\x97"))
                 && japanese ("A 3.7 LESS") == juce::String (juce::CharPointer_UTF8 ("A\xe3\x81\x8c" "3.7\xe5\xb0\x91\xe3\x81\xaa\xe3\x81\x84")),
             "a band cell reads in Japanese (3.7\u5c11\u306a\u3044 / \u5dee\u306a\u3057), and the A sentence keeps its subject");

    // 指標の欄（INTEGRATED LOUDNESS・MAXIMUM TRUE PEAK、200% 以下の LUFS-I・MAX TP）の差の列の見出しと下の段。
    require (japanese ("A VS C") == juce::String (juce::CharPointer_UTF8 ("C\xe3\x82\x88\xe3\x82\x8a" "A"))
                 && japanese ("A VS C  LUFS-I") == juce::String (juce::CharPointer_UTF8 ("C\xe3\x82\x88\xe3\x82\x8a" "A LUFS-I"))
                 && japanese ("A VS C (dB)") == juce::String (juce::CharPointer_UTF8 ("C\xe3\x82\x88\xe3\x82\x8a" "A\xef\xbc\x88" "dB\xef\xbc\x89"))
                 && japanese ("LU QUIETER") == juce::String (juce::CharPointer_UTF8 ("LU\xe5\xb0\x8f\xe3\x81\x95\xe3\x81\x84"))
                 && japanese ("LU LOUDER") != "LU LOUDER" && japanese ("dB HIGHER") != "dB HIGHER" && japanese ("dB LOWER") != "dB LOWER",
             "the metric difference reads CyoriA / 1.2 / LU smaller in Japanese");

    // 300% の C の画面：4 帯域の 1 段は、名前と大きな差の文を省略せずに収める（両言語）。幅は図と同じ。
    auto state = named ("ready");
    state.separateComparisons = true;
    state.comparisonSlot = 2;
    state.comparisonMode = "loudness_match";
    state.checks = { { "chk-low/cand-1", "Low End  /  Song 1" } };
    state.checkId = "chk-low/cand-1";
    state.aKirin = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 300);
    state.cueKirin = kirinWindow (-6.0f, { -16.5, -11.5, -15.0, -28.5 }, 300);
    state.aWindowLoudness = -12.0;
    state.cueLoudness = -9.0;
    const auto context = presentation::forEditor (900, 600);
    reference_ui::Component panel;
    panel.setVisible (true);
    panel.setPresentationContext (context);
    panel.setSize (888, 470);
    panel.setState (state);
    int rowWidth = 0;
    for (auto* child : panel.getChildren())
        if (auto* view = dynamic_cast<reference_ui::ComparisonView*> (child)) rowWidth = view->getWidth();
    require (rowWidth > 600, "the C page gives the four bands the chart's width");
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        const auto where = juce::String (language == i18n::Language::japanese ? " (Japanese)" : " (English)");
        require (reference_ui::bandSummaryFits (rowWidth, context), "the four band names and their differences fit on one line at 300%" + where);
        // V の欄は 2 行（名前の下に差の文）。見出し「VよりA（dB）」の残りを等分する。
        const auto words = labelFont (context, typography::TextRole::unit, typography::Composition::information);
        const auto inner = (rowWidth - static_cast<int> (std::ceil (text_style::shownWidth (words, "A VS V (dB)"))) - 10 - 6 * 3) / 4 - 16;
        for (const auto* name : { "LOW 20-250", "LOW-MID 250-2k", "MID 2k-8k", "HIGH 8k-20k" })
            require (text_style::shownWidth (words, name) <= static_cast<float> (inner)
                         && reference_ui::aComparisonWidth (compareBand ({ 0.0, 24.5 }), context) <= static_cast<float> (inner),
                     juce::String ("a V band cell holds its name and difference at 300%: ") + name + where);
    }

    // 書き出し（見た目の確認用）：KIRIN_REFERENCE_UI_ABCV_OUTPUT=<dir> に C の役の全サイズ・両言語。
    const auto directory = juce::SystemStats::getEnvironmentVariable ("KIRIN_REFERENCE_UI_ABCV_OUTPUT", {});
    if (directory.isEmpty()) return;
    auto metrics = state;
    metrics.aIntegratedLoudness = -14.3; metrics.adjustedBIntegratedLoudness = -13.1;
    metrics.aMaximumTruePeakDbtp = -1.2; metrics.adjustedBMaximumTruePeakDbtp = -1.5;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        for (const auto& preset : observatory::sizePresets)
        {
            reference_ui::Component sized;
            sized.setVisible (true);
            sized.setPresentationContext (presentation::forEditor (preset.width, preset.height));
            sized.setSize (preset.width - 12, preset.height * 47 / 60);
            sized.setState (metrics);
            juce::Image image (juce::Image::ARGB, sized.getWidth(), sized.getHeight(), true);
            juce::Graphics graphics (image);
            graphics.fillAll (juce::Colour (0xff16110d));
            sized.paintEntireComponent (graphics, true);
            juce::FileOutputStream stream { juce::File (directory).getChildFile (
                "a_vs_c_" + juce::String (preset.width) + (language == i18n::Language::japanese ? "_ja.png" : "_en.png")) };
            if (stream.openedOk()) { stream.setPosition (0); stream.truncate(); juce::PNGImageFormat().writeImageToStream (image, stream); }
        }
    }
}
}
