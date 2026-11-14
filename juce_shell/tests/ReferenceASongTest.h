#pragma once

// C の SONG の A（A FROM START、ReferenceASong.h）。観測スレッドと同じ塊を、図の A の窓のメーターと一緒に足す。
//  - 同じ DAW の区切りをもう一度鳴らすと上書き（ループ・シークで偏らない）。区切りの間からの再生は次の境目から測るので、
//    区切りどうしは重ならず、鳴らした長さより多くは持たない。DAW の頭より前（負の位置の音）は持たない。
//  - 音量の 400 ms は同じ再生の続いた区切りだけ（別の時に鳴らした隣の区切りとはつながない）。
//  - 30 分より後は持たず、鳴らしたことを覚える。音量は BS.1770（-20 dBFS の 997 Hz は両 ch で約 -20 LUFS）。
//  - RESET（clear）と形式の変化で空（観測スレッドを通した確かめは ReferenceAWindowRuntimeTest.h）。
#include "../src/reference_audition/ReferenceASong.h"

#include <cmath>
#include <vector>

// ref と require は reference_runtime_test_support.h。
namespace
{
// 観測スレッド（VisualObservation::consumeKirin）と同じ：位置が続かなければ図の A の窓を新しくし、同じ塊を両方へ。
struct ASongFeed
{
    ref::KirinSpectrumMeter meter;  // 図の A の窓（同じ定義かを比べる）
    ref::ASongMeter song;
    std::int64_t expected = -1;
    std::uint64_t discontinuity = 1;  // 送り手の途切れの印（停止・遅延の変化で進む）
    ASongFeed() { meter.configure (48'000, 2, 6'000); song.configure (48'000, 2); }
    void play (double startSeconds, double seconds, double levelDbfs)
    {
        const auto amplitude = std::pow (10.0, levelDbfs / 20.0);
        const auto start = static_cast<std::int64_t> (std::llround (startSeconds * 48'000.0));
        const auto total = static_cast<std::int64_t> (std::llround (seconds * 48'000.0));
        std::vector<float> pcm (512);
        for (std::int64_t done = 0; done < total; done += 256)
        {
            const auto count = static_cast<int> (std::min<std::int64_t> (256, total - done));
            const auto position = start + done;
            for (int index = 0; index < count; ++index)
            {
                const auto value = static_cast<float> (amplitude * std::sin (6.283185307179586 * 997.0 * double (position + index) / 48'000.0));
                pcm[size_t (index * 2)] = pcm[size_t (index * 2 + 1)] = value;
            }
            if (position != expected) meter.reset();
            meter.push (pcm.data(), count);
            song.push (pcm.data(), count, position, discontinuity);
            expected = position + count;
        }
    }
};

double meanLoudness (std::initializer_list<std::pair<int, double>> parts)
{
    double energy = 0.0; int slots = 0;
    for (const auto& [count, level] : parts) { energy += count * std::pow (10.0, level / 10.0); slots += count; }
    return 10.0 * std::log10 (energy / slots);
}
}

inline void testReferenceASong()
{
    {
        ASongFeed feed;
        feed.play (0.0, 10.0, -20.0);
        auto summary = feed.song.summary();
        require (summary->slots == 100 && summary->spectrum && summary->spectrum->frames == 100 && ! summary->beyondLimit
                     && summary->loudnessBlocks == 97,
                 "10 s from the DAW's start hold 100 positions and 97 loudness blocks");
        require (std::abs (summary->loudness + 20.0) < 0.3, (juce::String ("a -20 dBFS 997 Hz sine on both channels is about -20 LUFS: ")
                     + juce::String (summary->loudness)).toRawUTF8());
        feed.play (0.0, 10.0, -24.0);
        summary = feed.song.summary();
        require (summary->slots == 100 && std::abs (summary->loudness + 24.0) < 0.3, "playing the same positions again replaces them");
        for (int lap = 0; lap < 10; ++lap) feed.play (0.0, 2.0, -20.0);  // 頭の 2 秒をループ
        summary = feed.song.summary();
        require (summary->slots == 100 && summary->loudnessBlocks == 94
                     && std::abs (summary->loudness - meanLoudness ({ { 20, -20.0 }, { 80, -24.0 } })) < 0.3,
                 (juce::String ("a loop counts its positions once, not once per lap, and no block joins two plays: ")
                  + juce::String (summary->loudness) + " / " + juce::String (summary->loudnessBlocks)).toRawUTF8());
        feed.play (2.05, 1.0, -20.0);  // 区切りの間へのシーク：次の境目（2.1 秒）から、最後の半端は持たない
        require (feed.song.summary()->slots == 100, "a seek between positions replaces whole positions only");
        feed.play (30.0, 1.0, -24.0);
        require (feed.song.summary()->slots == 110, "a seek to new positions adds them");
        feed.play (-1.0, 0.9, -24.0);
        require (feed.song.summary()->slots == 110, "before the DAW's start nothing is held");
        feed.play (1'800.0, 1.0, -24.0);
        summary = feed.song.summary();
        require (summary->slots == 110 && summary->beyondLimit, "past 30 minutes nothing is held, and A says so");
        feed.song.clear();
        summary = feed.song.summary();
        require (summary->slots == 0 && ! summary->spectrum && ! summary->beyondLimit && std::isnan (summary->loudness),
                 "RESET empties A FROM START");
        feed.play (0.0, 1.0, -20.0);
        require (feed.song.summary()->slots == 10, "A FROM START starts again after RESET");
        feed.song.configure (44'100, 2);
        require (feed.song.summary()->slots == 0, "a new format starts A FROM START again");
    }
    {   // 区切りの間からもう一度：1.05 秒の音で 11 の区切りにならない（同じ音を 2 つの区切りに数えない）。
        ASongFeed feed;
        feed.play (0.0, 1.0, -20.0);
        feed.play (0.05, 1.0, -20.0);
        require (feed.song.summary()->slots == 10, "a replay from between positions never holds more than was played");
    }
    {   // DAW の頭の直前の音は、頭をまたいで続けて鳴らしても入らない。
        ASongFeed feed;
        feed.play (-0.05, 0.05, -10.0);
        feed.play (0.0, 1.0, -40.0);
        const auto summary = feed.song.summary();
        require (summary->slots == 10 && std::abs (summary->loudness + 40.0) < 0.5,
                 (juce::String ("sound before the DAW's start stays out of the first position: ") + juce::String (summary->loudness)).toRawUTF8());
    }
    {   // 音量の 400 ms は同じ再生の続いた区切りだけ：0〜2 秒、10〜11 秒、2〜4 秒と鳴らすと 2 秒の継ぎ目をまたがない。
        ASongFeed feed;
        feed.play (0.0, 2.0, -20.0);
        feed.play (10.0, 1.0, -20.0);
        feed.play (2.0, 2.0, -20.0);
        const auto summary = feed.song.summary();
        require (summary->slots == 50 && summary->loudnessBlocks == 17 + 7 + 17,
                 (juce::String ("loudness blocks never join positions played at different times: ") + juce::String (summary->loudnessBlocks)).toRawUTF8());
    }
    {   // 途切れの印（停止して遅延を替え、同じ位置から再開）：途中のフレームを捨て、音量の 400 ms もつながない。
        ASongFeed feed;
        feed.play (0.0, 0.05, -20.0);   // 2,400 サンプルで止める
        ++feed.discontinuity;
        feed.play (0.05, 0.95, -20.0);  // 同じ位置から再開：半端の区切りは持たず、次の境目（0.1 秒）から
        require (feed.song.summary()->slots == 9, "an interruption at the same position drops the half-measured position");
        feed.play (1.0, 1.0, -20.0);
        ++feed.discontinuity;
        feed.play (2.0, 1.0, -20.0);
        const auto summary = feed.song.summary();
        require (summary->slots == 29 && summary->loudnessBlocks == 16 + 7,  // 0.1〜2 秒（19 区切り）と 2〜3 秒の 2 つの再生
                 (juce::String ("loudness blocks never span an interruption: ") + juce::String (summary->loudnessBlocks)).toRawUTF8());
        feed.song.interrupt();  // 観測スレッドが共有の測りを新しくした（壊れた入力・見ていない間）
        feed.play (3.0, 0.05, -20.0);
        feed.song.interrupt();
        feed.play (3.05, 0.95, -20.0);
        require (feed.song.summary()->slots == 29 + 9, "an interrupt from the observation thread also drops the half-measured position");
    }
    {   // RESET の後は本当に新しい：-1 サンプルから鳴らし直しても、前の続きと取り違えない。
        ASongFeed feed;
        feed.play (0.0, 10.0, -20.0);
        feed.song.clear();
        feed.play (-1.0 / 48'000.0, 10.05, -20.0);
        const auto summary = feed.song.summary();
        require (summary->slots == 100 && std::abs (summary->loudness + 20.0) < 0.3,
                 (juce::String ("after RESET a restart from -1 sample holds the first ten seconds at their level: ")
                  + juce::String (summary->slots) + " / " + juce::String (summary->loudness)).toRawUTF8());
    }
    {   // 同じ区切りの値は図の A の窓のフレームと同じ（帯域の中央値）。
        ASongFeed feed;
        feed.play (0.0, 3.0, -20.0);
        const auto window = feed.meter.window (30);
        const auto summary = feed.song.summary();
        require (window && summary->spectrum && summary->spectrum->centersHz == window->centersHz
                     && std::abs (summary->spectrum->medianDb[40] - window->medianDb[40]) < 1.0e-6
                     && std::abs (summary->spectrum->balanceDb[1] - window->balanceDb[1]) < 1.0e-6,
                 "A FROM START measures as A's window does");
    }
    std::cout << "Reference A from start: PASS\n";
}
