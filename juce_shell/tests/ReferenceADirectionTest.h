#pragma once

// 2026-10-06：差の向きと読みの基準を、実際に描く所で確かめる（文を作る関数だけを試しても、描く所で値を取り違えれば
// LOWER と HIGHER が逆になる）。
//  - A が比べる側より小さい状態を作り、差を描く所（指標の欄・Blauert の読み・4 帯域・範囲の帯・V の時間の線の
//    読み）が、どれも A が少ない・低い・小さい・狭い側の言葉だけを描く。
//  - A を 6 dB 下げているあいだ、同じ大きさの曲の MATCH の読みは、B の一覧の鳴っている行とほかの行・C・V・状態の
//    行で同じ数になる。
#include "ReferenceCheckPageTest.h"
#include "../src/HyphaReferenceComparisonView.h"
#include "../src/HyphaReferenceSongList.h"
#include "../src/HyphaReferenceStatusModel.h"

#include <map>

namespace hypha::tests
{
namespace a_direction
{
inline const juce::StringArray& largerWords()
{
    static const juce::StringArray words { "MORE", "HIGHER", "LARGER", "LOUDER", "WIDER" };
    return words;
}

inline const juce::StringArray& smallerWords()
{
    static const juce::StringArray words { "LESS", "LOWER", "SMALLER", "QUIETER", "NARROWER" };
    return words;
}

// 描いた文（英語）を英字の語に分け、向きの言葉を数える。
inline std::map<juce::String, int> directionWords (const juce::StringArray& texts)
{
    std::map<juce::String, int> counts;
    const auto count = [&counts] (const juce::String& word)
    {
        if (largerWords().contains (word) || smallerWords().contains (word)) ++counts[word];
    };
    for (const auto& text : texts)
    {
        juce::String word;
        for (int index = 0; index < text.length(); ++index)
        {
            const auto character = text[index];
            if (character >= 'A' && character <= 'Z') { word += juce::String::charToString (character); continue; }
            count (word);
            word.clear();
        }
        count (word);
    }
    return counts;
}

// 部品を 1 度描き、描いた文を描いた順に返す。
inline juce::StringArray drawnTexts (juce::Component& target)
{
    text_style::ShownTextLog log;
    juce::Image image (juce::Image::ARGB, target.getWidth(), target.getHeight(), true);
    juce::Graphics graphics (image);
    target.paintEntireComponent (graphics, true);
    return log.texts();
}

// A が小さい側の言葉だけを描き、`expected` の言葉をそれぞれ決まった回数以上描いた。
using Expected = std::vector<std::pair<juce::String, int>>;

inline void requireSmallerA (const juce::StringArray& texts, const Expected& expected, const juce::String& scene)
{
    using reference_guide_contract::require;
    const auto counts = directionWords (texts);
    bool valid = true;
    for (const auto& word : largerWords()) valid = valid && counts.count (word) == 0;
    for (const auto& [word, minimum] : expected)
    {
        const auto found = counts.find (word);
        valid = valid && found != counts.end() && found->second >= minimum;
    }
    require (valid, "A is smaller, and every difference drawn says so: " + scene + "\n  drew: " + texts.joinIntoString (" | "));
}

// A の 100 ms の bin：比べる側のどの値よりも小さい（クレスト 6.8 dB・幅 20%・相関 0.3・ピーク −20 dBFS・RMS −26 dBFS・
// LUFS-M −20・LUFS-S の動き ±0.5・立ち上がり 0・アタック 約 0 dB）。
inline KirinReferenceVisualBin smallBin (int index)
{
    KirinReferenceVisualBin bin {};
    const auto wave = std::sin (static_cast<double> (index) * 0.37);
    bin.frames = 4'800;
    bin.rms[0] = bin.rms[1] = 0.05;
    bin.peak[0] = bin.peak[1] = 0.1;
    bin.true_peak = 0.11;
    bin.mid = 10.0; bin.side = 0.4;            // √(S/M) = 0.2
    bin.cross = 0.3 * 0.05 * 0.05 * 4'800.0;   // 相関 0.3
    bin.momentary_lufs = -20.0 + 0.3 * wave;
    bin.short_lufs = -20.0 + 0.5 * wave;
    return bin;
}

// 比べる側の bin：A よりどれも大きい（V の同じ区間の比べ方。RMS は 0.1・0.2・0.3 を繰り返して立ち上がりを作る）。
inline KirinReferenceVisualBin largeBin (int index)
{
    KirinReferenceVisualBin bin {};
    const auto level = 0.1 * static_cast<double> (1 + (index / 2) % 3);
    bin.frames = 4'800;
    bin.rms[0] = bin.rms[1] = level;
    bin.peak[0] = bin.peak[1] = 0.9;
    bin.true_peak = 0.95;
    bin.mid = 10.0; bin.side = 6.4;            // √(S/M) = 0.8
    bin.cross = 0.9 * level * level * 4'800.0; // 相関 0.9
    bin.momentary_lufs = -8.0;
    bin.short_lufs = index % 2 == 0 ? -16.0 : -4.0;
    return bin;
}

// C：A の窓・C の Cue の Kirin OS の値がそろい、鳴らす gain は 0 dB。どの帯域も C が 3 dB 多く、1 kHz も C が張り出す
// （傾きが 20/63 dB ずつ急で、Blauert の差は 0.3 dB。もっと急だと 3-4 kHz が −100 dBFS を下回って読めない）。
inline reference_ui::State checkState()
{
    using reference_guide_contract::named;
    auto state = named ("ready");
    state.separateComparisons = true;
    state.comparisonSlot = 2;
    state.comparisonMode = "loudness_match";
    state.checks = { { "chk-low/cand-1", "Low End  /  Song 1" } };
    state.checkId = "chk-low/cand-1";
    state.aKirin = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 300);
    state.cueKirin = kirinWindow (-20.0f, { -17.0, -11.0, -15.0, -27.0 }, 300);
    state.aWindowLoudness = state.cueLoudness = -12.0;
    state.cueStartSeconds = 62.0; state.cueEndSeconds = 92.0; state.sourceDurationSeconds = 295.0;
    state.aIntegratedLoudness = -14.3; state.adjustedBIntegratedLoudness = -12.0;
    state.aMaximumTruePeakDbtp = -2.0; state.adjustedBMaximumTruePeakDbtp = -0.5;
    return state;
}

// C の Cue の Kirin OS の詳しい値（200 ms ごと）：A のどの値よりも大きい。
inline std::shared_ptr<reference_audition::RuntimeDetailedMeasurement> largeMeasurement()
{
    auto measurement = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    measurement->audio = { 48'000, 2, 295 * 48'000 };
    const std::int64_t hop = 9'600;
    const auto hops = 295 * 48'000 / hop;
    reference_audition::RuntimeNullableIntegerSeries crest, lufsM, lufsS, correlation, width;
    reference_audition::RuntimeMeasurementWaveform waveform { hop, { {}, {} }, { {}, {} } };
    reference_audition::RuntimeMeasurementTransient transient { hop, {} };
    for (std::int64_t index = 0; index < hops; ++index)
    {
        crest.push_back (15'000);
        lufsM.push_back (-8'000);
        lufsS.push_back (index % 2 == 0 ? -16'000 : -4'000);
        correlation.push_back (900);
        width.push_back (8'000);
        for (int c = 0; c < 2; ++c)
        {
            waveform.samplePeakMillidbfs[static_cast<size_t> (c)].push_back (-1'000);
            waveform.rmsMillidbfs[static_cast<size_t> (c)].push_back (-10'000);
        }
        transient.onsetStrengthQ15.push_back (16'384);
    }
    measurement->dynamics = reference_audition::RuntimeMeasurementTimeline { hop, { { "crest_millidb", crest } } };
    measurement->loudness = reference_audition::RuntimeMeasurementTimeline { hop, { { "lufs_m_millilu", lufsM }, { "lufs_s_millilu", lufsS } } };
    measurement->stereo = reference_audition::RuntimeMeasurementTimeline { hop, { { "correlation_milli", correlation }, { "width_basis_points", width } } };
    measurement->waveform = waveform;
    measurement->transient = transient;
    return measurement;
}

// 範囲の帯の Check ごとに、A が小さいと言う言葉（2 行ぶん）。
inline std::vector<std::pair<const char*, Expected>> stripWords()
{
    return { { "dynamics", { { "SMALLER", 2 } } }, { "loudness", { { "QUIETER", 1 }, { "SMALLER", 1 } } },
             { "stereo", { { "NARROWER", 1 }, { "LOWER", 1 } } }, { "waveform", { { "LOWER", 2 } } },
             { "transient", { { "SMALLER", 2 } } } };
}

// V：WHOLE の時間の線（A の LUFS-S もクレストも V より小さい）と、同じ区間の比べ方の値（A がどれも小さい）。
inline std::shared_ptr<reference_audition::VisualTimeline> versionTimeline (double gainDb)
{
    auto source = std::make_shared<reference_audition::RuntimeSource>();
    source->audio = { 48'000, 2, 48'000 * 200 };
    source->sourceFileSha256 = juce::String::repeatedString ("a", 64);
    source->sourceKind = "work_version"; source->sourceIdentityKey = "same-work:recording:version-a";
    auto overview = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    overview->waveform.emplace();
    overview->waveform->framesPerBin = 4'800;
    overview->waveform->samplePeakMillidbfs.resize (2);
    overview->waveform->rmsMillidbfs.resize (2);
    auto timeline = std::make_shared<reference_audition::VisualTimeline>();
    timeline->binding.source = source; timeline->binding.overview = overview; timeline->binding.key = "same-song-verified";
    timeline->binding.aligned = timeline->binding.matched = true; timeline->binding.channels = 2; timeline->binding.hostRate = 48'000;
    timeline->binding.gainDb = gainDb;
    timeline->pass = timeline->revision = 1; timeline->hop = 4'800; timeline->bins.resize (2'000);
    for (size_t index = 0; index < 2'000; ++index)
    {
        for (int c = 0; c < 2; ++c)
        {
            overview->waveform->samplePeakMillidbfs[static_cast<size_t> (c)].push_back (-6'000);
            overview->waveform->rmsMillidbfs[static_cast<size_t> (c)].push_back (-14'000);
        }
        if (index <= 700 || index >= 1'100) continue;
        auto& bin = timeline->bins[index];
        bin.pass = 1; bin.a.frames = bin.b.frames = 4'800;
        for (int c = 0; c < 2; ++c) { bin.a.peak[c] = 0.3; bin.b.peak[c] = 0.6; bin.a.rms[c] = 0.1; bin.b.rms[c] = 0.2; }
        bin.a.short_lufs = -16.0; bin.b.short_lufs = -12.0 - gainDb;  // 合わせた V は A より 4 LU 大きい
        bin.a.crest_db = 6.0; bin.b.crest_db = 9.0;
    }
    timeline->aPairKirin = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 300);
    timeline->vPairKirin = kirinWindow (-20.0f, { -17.0, -11.0, -15.0, -27.0 }, 300);
    auto a = std::make_shared<std::vector<KirinReferenceVisualBin>>(), v = std::make_shared<std::vector<KirinReferenceVisualBin>>();
    for (int index = 0; index < 240; ++index) { a->push_back (smallBin (index)); v->push_back (largeBin (index)); }
    timeline->aPairTicks = a;
    timeline->vPairTicks = v;
    timeline->pairTickChannels = 2;
    return timeline;
}
}

// A を 6 dB 下げているあいだ（承認した下げ。R-12）、同じ大きさの曲は、どこで読んでも同じ gain の数になる。読みは
// どれも displayGainDb を通る（鳴っている曲だけ下げた量を足すと、同じ大きさの曲が +0.0 と +6.0 に分かれ、押すと
// +0.0 で鳴った）。A の窓は −8 LUFS、曲はどれも −14 LUFS：下げる前の基準の gain は +6 dB、鳴る gain は 0 dB。
inline void verifyReferenceLoweredAReadings()
{
    using namespace reference_guide_contract;
    using a_direction::drawnTexts;
    const i18n::ScopedLanguage english (i18n::Language::english);
    const juce::String zero ("+0.0"), unlowered ("+6.0");
    for (const auto held : { -6.0, 0.0 })
    {
        const auto expected = held < 0.0 ? zero : unlowered;
        const auto where = juce::String (held < 0.0 ? " (A lowered 6 dB)" : " (A not lowered)");
        auto state = named ("ready");
        state.songs = { { "e1/e1/song-1", "Song 1" }, { "e2/e2/song-2", "Song 2" } };
        state.songId = "e1/e1/song-1";
        for (int song = 0; song < 2; ++song)
        {
            reference_ui::SongFact fact;
            fact.lufsI = -14.0; fact.prepared = true;
            state.songFacts.push_back (fact);
        }
        state.aWindowLoudness = -8.0;
        state.aWindowBlocks = 100;
        state.referenceReady = state.referenceArmable = true;
        state.referenceStep = reference_ui::SourceStep::ready;
        state.comparisonSlot = 3;
        state.bSelected = true;
        state.audibleComparisonSlot = 3;
        state.appliedGainDb = 6.0;  // 下げる前の A の基準
        state.heldAttenuationDb = held;
        reference_ui::Component panel;
        panel.setVisible (true);
        panel.setPresentationContext (presentation::forEditor (900, 600));
        panel.setSize (888, 470);
        panel.setState (state);
        auto* list = dynamic_cast<reference_ui::SongList*> (panel.findChildWithID ("reference-song-list"));
        require (list != nullptr && list->isVisible() && list->rows().size() == 2 && list->rows()[0].playing && ! list->rows()[1].playing,
                 "the B list shows the playing song and the other one");
        require (reference_ui::gainText (list->rows()[0].gainDb) == expected && reference_ui::gainText (list->rows()[1].gainDb) == expected,
                 "the playing song and the other song of the same loudness read the same gain" + where);
        int drawn = 0;
        for (const auto& text : drawnTexts (*list)) drawn += text == expected ? 1 : 0;
        require (drawn == 2, "the B list draws that gain on both rows" + where);
        require (reference_ui::gainReadout (state) == "B " + expected + " dB", "the status row reads the same gain for B" + where);

        auto check = state;
        check.separateComparisons = true;
        check.comparisonSlot = 2;
        check.comparisonMode = "loudness_match";
        check.cueLoudness = -14.0;
        check.bSelected = false;
        check.audibleComparisonSlot = 0;
        require (reference_ui::matchReadout (check) == "ON PLAY / C " + expected + " dB", "C reads the same gain before it plays" + where);
        check.bSelected = true;
        check.audibleComparisonSlot = 2;
        require (reference_ui::matchReadout (check) == "C " + expected + " dB" && reference_ui::gainReadout (check) == "C " + expected + " dB",
                 "C and the status row read the same gain while C plays" + where);

        auto version = check;
        version.comparisonSlot = 1;
        version.audibleComparisonSlot = 1;
        require (reference_ui::gainReadout (version) == "V " + expected + " dB", "V reads the same gain while it plays" + where);
    }
    require (reference_ui::gainText (-0.04) == "+0.0" && reference_ui::gainText (-1.56) == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "1.6"))
                 && reference_ui::gainText (6.0) == "+6.0",
             "a gain reads with a sign, and a gain that rounds to zero reads +0.0");
}

inline void verifyReferenceADirection()
{
    verifyReferenceLoweredAReadings();
    using namespace reference_guide_contract;
    using namespace a_direction;
    const i18n::ScopedLanguage english (i18n::Language::english);
    const auto panelAt = [] (reference_ui::Component& panel, int width, int height)
    {
        panel.setVisible (true);
        panel.setPresentationContext (presentation::forEditor (width, height));
        panel.setSize (width - 12, height * 47 / 60);
    };

    // C（300%）：Cue のスペクトルの Blauert の読みと 4 帯域。
    {
        auto state = checkState();
        state.viewBindings = { "spectrum_full" };
        reference_ui::Component panel;
        panelAt (panel, 900, 600);
        panel.setState (state);
        requireSmallerA (drawnTexts (panel), { { "LOWER", 1 }, { "LESS", 4 } }, "C spectrum and bands");
    }
    // C（300%）：範囲の帯（A の直近 30 秒と、C の Cue の同じ定義の値）。
    {
        auto state = checkState();
        auto timeline = std::make_shared<reference_audition::VisualTimeline>();
        timeline->binding.matchWindowBlocks = 300;
        timeline->aTickChannels = 2;
        auto ticks = std::make_shared<std::vector<KirinReferenceVisualBin>>();
        for (int index = 0; index < 300; ++index) ticks->push_back (smallBin (index));
        timeline->aTicks = ticks;
        state.visualTimeline = timeline;
        state.cueMeasurement = largeMeasurement();
        state.cuePart = reference_audition::CuePart::chorus;
        reference_ui::Component panel;
        panelAt (panel, 900, 600);
        for (const auto& [binding, expected] : stripWords())
        {
            state.viewBindings = { binding };
            panel.setState (state);
            requireSmallerA (drawnTexts (panel), expected, juce::String ("C strips ") + binding);
        }
    }
    // 指標の欄（300% の 2 つの箱）と、200% 以下の差だけの欄。
    for (const auto& [width, height] : { std::pair { 900, 600 }, std::pair { 600, 400 }, std::pair { 450, 300 } })
    {
        auto state = checkState();
        state.aKirin.reset();
        state.cueKirin.reset();
        reference_ui::Component panel;
        panelAt (panel, width, height);
        panel.setState (state);
        requireSmallerA (drawnTexts (panel), { { "QUIETER", 1 }, { "LOWER", 1 } }, "metrics at " + juce::String (width));
    }
    // B（300%）：曲の一覧の右の Balance の Blauert の読み。
    {
        auto state = named ("ready");
        state.songs = { { "e1/e1/song-1", "Song 1" } };
        state.songId = "e1/e1/song-1";
        state.aKirin = kirinWindow (0.0f, { -20.0, -14.0, -18.0, -30.0 }, 300);
        const auto large = kirinWindow (-20.0f, { -17.0, -11.0, -15.0, -27.0 }, 300);
        reference_ui::SongFact fact;
        fact.lufsI = -12.0; fact.prepared = true;
        fact.centersHz = large->centersHz;
        fact.medianDb = large->medianDb;
        state.songFacts = { fact };
        state.aWindowLoudness = -12.0;
        state.referenceReady = state.referenceArmable = true;
        state.referenceStep = reference_ui::SourceStep::ready;
        state.comparisonSlot = 3;
        reference_ui::Component panel;
        panelAt (panel, 900, 600);
        panel.setState (state);
        requireSmallerA (drawnTexts (panel), { { "LOWER", 1 } }, "B balance");
    }
    // V（300%）：WHOLE の時間の線の読み、TONE（スペクトルと 4 帯域）、同じ区間の範囲の帯。
    {
        auto state = checkState();
        state.comparisonSlot = 1;
        state.bSelected = true;
        state.audibleComparisonSlot = 1;
        state.appliedGainDb = -0.9;
        state.versions = { { "v1", "Mix v7" } };
        state.versionId = "v1";
        state.visualPreferences = std::make_shared<reference_audition::VisualPreferences>();
        state.visualTimeline = versionTimeline (-0.9);
        state.visualPositionSeconds = 98.0;
        reference_ui::Component panel;
        panelAt (panel, 900, 600);
        panel.setState (state);
        requireSmallerA (drawnTexts (panel), { { "QUIETER", 1 } }, "V timeline");
        auto* tabs = dynamic_cast<reference_ui::CheckTabs*> (panel.findChildWithID ("reference-check-tabs"));
        require (tabs != nullptr && tabs->isVisible(), "the V page has its tabs");
        tabs->onChoose ("v-tone");
        requireSmallerA (drawnTexts (panel), { { "LOWER", 1 }, { "LESS", 4 } }, "V tone");
        tabs->onChoose ("v-dynamics");
        requireSmallerA (drawnTexts (panel), { { "SMALLER", 2 } }, "V dynamics");
        tabs->onChoose ("v-stereo");
        requireSmallerA (drawnTexts (panel), { { "NARROWER", 1 }, { "LOWER", 1 } }, "V stereo");
    }
}
}
