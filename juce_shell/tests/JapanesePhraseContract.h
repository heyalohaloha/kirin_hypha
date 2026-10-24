#pragma once

#include "../src/HyphaTextStyle.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyJapanesePhrases()
{
    const auto font = nativeTextFontLike (labelFont (presentation::forEditor (300, 200),
                                                    typography::TextRole::body));
    const char* const phrases[][4] {
        { u8"PREとPOSTのタイミングを確かめられません。", u8"PREと", u8"タイミングを", u8"確かめられません。" },
        { u8"音源の登録情報がファイルと合いません。", u8"音源の", u8"登録情報が", u8"ファイルと" },
        { u8"Kirin OSで Reference のセットを選んでください。", "Kirin OS", u8"Reference のセット", u8"選んでください。" },
        { u8"CHECK セットと Reference の範囲を表示します。", u8"CHECK セット", u8"Reference の範囲", u8"表示します。" },
        { u8"SOURCE 1とSOURCE 2を聴いてから答えを表示してください。", "SOURCE 1", "SOURCE 2", u8"表示してください。" },
        { u8"音源を確認しました。そのまま比較できます。", u8"確認しました。", u8"そのまま", u8"比較できます。" },
        { u8"PREを選び直してください。音源を読み込み、設定を切り替えます。", u8"選び直してください。", u8"読み込み、", u8"切り替えます。" },
        { u8"同じボタンを押し直すと、取り込みを取り消せます。", u8"押し直すと、", u8"取り込みを", u8"取り消せます。" },
        { u8"Sample PeakとPeak Holdを同じ区間で表示します。", "Sample Peak", "Peak Hold", u8"表示します。" },
        { u8"Momentary LoudnessとShort-term Loudnessを比べます。", "Momentary Loudness", "Short-term Loudness", u8"比べます。" },
    };
    for (const auto& row : phrases)
    {
        const auto text = juce::String::fromUTF8 (row[0]);
        auto minimum = 0.0f;
        for (int index = 1; index < 4; ++index)
            minimum = std::max (minimum, font.getStringWidthFloat (juce::String::fromUTF8 (row[index])));
        for (auto width = minimum + 15.0f; width < font.getStringWidthFloat (text); width += 5.0f)
        {
            const auto lines = text_style::japaneseLines (text, font, width);
            const auto joined = lines.joinIntoString ("\n");
            for (int index = 1; index < 4; ++index)
                if (! joined.contains (juce::String::fromUTF8 (row[index])))
                {
                    std::cerr << "Japanese phrase split at " << width << ": " << joined << '\n';
                    std::exit (EXIT_FAILURE);
                }
            if (joined.removeCharacters (" \n") != text.removeCharacters (" ")) std::exit (EXIT_FAILURE);
            for (const auto& line : lines)
                if (font.getStringWidthFloat (line) > width + 0.5f) std::exit (EXIT_FAILURE);
        }
    }
}
}
