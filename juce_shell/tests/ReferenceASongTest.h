#pragma once

// C の SONG の A（A FROM START、ReferenceASong.h）。観測スレッドと同じ順で、図の A の窓のメーターと一緒に足す。
//  - 同じ DAW の位置をもう一度鳴らすと上書き（ループ・シークで偏らない）。区切りから外れた位置へのシークは近い区切りへ。
//  - 長さは値のある区切りの数。DAW の頭より前は持たない。30 分より後は持たず、鳴らしたことを覚える。
//  - 音量は BS.1770（-20 dBFS の 997 Hz は両 ch で約 -20 LUFS）。RESET（clear）と形式の変化で空。
#include "../src/reference_audition/ReferenceASong.h"

#include <cmath>
#include <vector>

// ref と require は reference_runtime_test_support.h。
namespace
{
// 観測スレッド（VisualObservation::consumeKirin）と同じ：位置が続かなければメーターを新しくし、足す前の途中の数を渡す。
struct ASongFeed
{
    ref::KirinSpectrumMeter meter;
    ref::ASongMeter song;
    std::int64_t expected = -1;
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
            const auto pending = meter.pendingFrames();
            meter.push (pcm.data(), count);
            song.push (pcm.data(), count, position, pending, meter);
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
        require (summary->slots == 100 && summary->spectrum && summary->spectrum->frames == 100 && ! summary->beyondLimit,
                 "10 s from the DAW's start hold 100 positions");
        require (std::abs (summary->loudness + 20.0) < 0.3, (juce::String ("a -20 dBFS 997 Hz sine on both channels is about -20 LUFS: ")
                     + juce::String (summary->loudness)).toRawUTF8());
        feed.play (0.0, 10.0, -24.0);
        summary = feed.song.summary();
        require (summary->slots == 100 && std::abs (summary->loudness + 24.0) < 0.3, "playing the same positions again replaces them");
        for (int lap = 0; lap < 10; ++lap) feed.play (0.0, 2.0, -20.0);  // 頭の 2 秒をループ
        summary = feed.song.summary();
        require (summary->slots == 100 && std::abs (summary->loudness - meanLoudness ({ { 20, -20.0 }, { 80, -24.0 } })) < 0.3,
                 (juce::String ("a loop counts its positions once, not once per lap: ") + juce::String (summary->loudness)).toRawUTF8());
        feed.play (2.05, 1.0, -20.0);  // 区切りから外れた位置へのシーク：近い区切りを上書き
        require (feed.song.summary()->slots == 100, "a seek between positions replaces the nearest ones");
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
