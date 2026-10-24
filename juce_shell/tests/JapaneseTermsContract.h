#pragma once

#include "../src/HyphaLanguage.h"
#include "../src/HyphaReferenceDisplayText.h"
#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline bool keptEnglishFactsLabel (const juce::String& text)
{
    for (const auto* allowed : { "Max age ", u8"Facts — frozen / ESC close / End LIVE", " / presentation ",
        "Whole interval ", "Exact median ", "Raw interval ", " / Whole within", "Source generation ",
        "Pair authority ", "Scroll / Up Down for further facts", "Processing change to the same hit: POST - PRE.",
        "Output hit shape and level.", "ATT / REL measure the envelope, not compressor settings.",
        "First 30 ms RMS minus body RMS.", "First 30 ms RMS level.", "Peak minus RMS in the first 30 ms.",
        "Perceptual sharpness over the first 100 ms.", "POST arrival minus PRE arrival in this band.",
        "Envelope rise time: 10 to 90 percent.", "Decay time: peak to -20 dB.", "Peak of the band RMS envelope." })
        if (text == juce::String::fromUTF8 (allowed)) return true;
    return false;
}

inline void verifyJapaneseTerms()
{
    // Technical terms keep their familiar English spelling within Japanese sentences.
    const char* const cases[][2] {
        { "POST Spectrum: current magnitude, six-second field, and rolling peak hold.", "Peak Hold" },
        { "Waiting for corresponding PRE observation", u8"同じ時刻のPREの計測値を待っています" },
        { "Record is available for mono / stereo only", "Mono" },
        { "RAW: exact POST - PRE spectral difference", u8"同じ区間" },
        { "Spectrum size", "Spectrum" },
        { "Switch WIDE / FOCUS loudness scale", "Loudness" },
        { "Listen to POST, the output of this chain", u8"チェーン" },
        { "DELAY: SAME", u8"DELAY：" },
        { "ATTACK: SAME", u8"ATTACK：" },
        { "TAIL: SAME", u8"TAIL：" },
        { "LEVEL: SAME", u8"LEVEL：" },
        { "Clear True Peak and Clip holds", "True Peak" },
        { "Sharpness Delta", "Sharpness" },
        { "HEAD: the band envelope around the onset. The lines mark where PRE and POST rise through their peak - 20 dB; DELAY is the gap between them.", "Envelope" },
        { "DELAY: POST arrival minus PRE arrival in this band, ms; arrival is where the envelope rises through peak - 20 dB.", u8"音が始まる時刻" },
        { "Level follow stopped at the safe ceiling. The current gain is kept.", "Gain" },
        { "DAW delay compensation is off", u8"遅延補正（PDC）" },
        { "Switch Momentary / Short-term loudness", "Momentary" },
        { "Switch Momentary / Short-term loudness", "Short-term" },
        { "Drum Attack", "Attack" },
        { "Set change: gap", u8"組が変わる所で線を切る" },
        { "Correlation: how alike left and right move. +1 the same, 0 unrelated, -1 opposite.", "Correlation" },
        { "Within RES", u8"RES内" },
        { "DAW timing cannot identify this loop occurrence", u8"ループの何周目か" },
        { "Crest factor (CREST): peak minus RMS in this window, the dynamics of the moment. Compare PRE and POST to see how much transients were reduced.", u8"CREST：Peak − RMS" },
        { "Integrated loudness of A's recent window and of C, and how A compares with C.", "LUFS-I" },
        { "Listen to POST, lowered by the attenuation you approved", u8"承認した分だけ下げた" },
        { "Audio block exceeded prepared size", "Buffer size" },
        { "ONSET (RISE PER HOP)", u8"ONSET（立ち上がり）" },
        { "Max age ", "Max age " },
        { "First 30 ms RMS minus body RMS.", "body RMS" },
        { "Width: side against mid, up to 150%. Shows how wide the stereo image is.", "Width" },
        { "LR specific-loudness share", u8"帯域ごとの Loudness" },
        { "Show perceptual spectral balance", u8"聴こえ方の帯域バランス（PSB）" },
        { "Perceptual sharpness over the first 100 ms.", "sharpness" },
        { "BLIND NEEDS HEADROOM", "Headroom" },
        { "Loudness range (LRA): how far the loudness moves within the song since the reset. Compare PRE and POST to see how much compression narrowed it.", "LRA" },
    };
    for (const auto& pair : cases)
    {
        const auto english = juce::String::fromUTF8 (pair[0]);
        const auto actual = i18n::translate (english, i18n::Language::japanese);
        if (! i18n::hasTranslation (english) || ! actual.contains (juce::String::fromUTF8 (pair[1]))
            || i18n::translate (english, i18n::Language::english) != english)
        {
            std::cerr << "Japanese technical term: " << english << " -> " << actual << '\n';
            std::exit (EXIT_FAILURE);
        }
    }
    for (const auto* facts : { "Max age ", "Whole interval ", "Exact median ",
                               "Raw interval ", "Source generation ", "Pair authority " })
        if (i18n::translate (facts, i18n::Language::japanese) != facts)
        {
            std::cerr << "Facts label must retain English: " << facts << '\n';
            std::exit (EXIT_FAILURE);
        }
}

inline void verifyBuiltInTermAliases()
{
    const char* const names[][3] {
        { u8"コンプレッション", "Compression", "Compression" },
        { u8"トランジェント", "Transient", "Transient" },
        { u8"ステレオと位相", u8"Stereo と位相", "Stereo and phase" },
    };
    const juce::String id ("00000000-0000-4000-8000-000000002001");
    for (const auto& row : names)
        for (const auto* alias : { row[0], row[1] })
            for (const auto& suffix : { juce::String {}, juce::String ("   1 / 3"), juce::String ("  /  Audio") })
            {
                const auto input = juce::String::fromUTF8 (alias) + suffix;
                const auto english = juce::String::fromUTF8 (row[2]) + suffix;
                const auto japanese = juce::String::fromUTF8 (row[1]) + suffix;
                if (reference_ui::standardDisplayName (input) != english
                    || reference_ui::shownStandardName (id, input, i18n::Language::english) != english
                    || reference_ui::shownStandardName (id, input, i18n::Language::japanese) != japanese
                    || reference_ui::shownStandardName ("custom", input, i18n::Language::japanese) != input
                    || reference_ui::shownStandardName ("custom", input, i18n::Language::english) != input)
                {
                    std::cerr << "Built-in name alias: " << input << '\n';
                    std::exit (EXIT_FAILURE);
                }
            }
}
}
