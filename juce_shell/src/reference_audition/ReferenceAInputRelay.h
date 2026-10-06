#pragma once

#include "ReferenceVisualObservation.h"

#include <array>
#include <cstdint>
#include <limits>

// A（DAW の入力）を Reference の観測スレッド（VisualObservation）へ渡す。Audio Thread から呼ぶ（確保・ロック・IO なし）。
// 2026-10-04：A の取り込み（capture）をやめ、その部品と受け渡しのスレッドを通さず、観測スレッドの受け口へ直接渡す。
//  - 256 フレームずつに分ける（観測スレッドの受け口の大きさ）。
//  - 再生していない・位置が無い・バイパス・書き出し・時計の元か遅延の印が替わったら、不連続の数を進める（観測スレッドが
//    A の窓を作り直す。止めて同じ位置から再生し直したときや、PDC が替わったときも）。
//  - Kirin OS のライセンスが無ければ世代 0（観測スレッドが捨てる）。VERSION BLIND とローカル Blind のあいだは渡さない（feed）。
namespace hypha::reference_audition
{
// ホストの知らせる入出力の遅延（プレゼンテーション）の印。替わったら A の時計が切れたとみなす。
struct AInputClockSignature
{
    std::uint32_t input = 0, output = 0;
    std::uint8_t presentation = 0;
    bool inputValid = false, outputValid = false;
    bool operator== (const AInputClockSignature& other) const noexcept
    {
        return input == other.input && output == other.output && presentation == other.presentation
            && inputValid == other.inputValid && outputValid == other.outputValid;
    }
    bool operator!= (const AInputClockSignature& other) const noexcept { return ! (*this == other); }
};

class AInputRelay
{
public:
    // `inputAllowed`：バイパス・書き出しでない（ライセンスは見ない）。`licensed`：Kirin OS のライセンス（無ければ渡さない）。
    void observe (VisualObservation& visual, const juce::AudioBuffer<float>& input, std::int64_t position, bool valid,
                  bool playing, bool inputAllowed, int clock, AInputClockSignature signature, bool licensed, bool feed) noexcept
    {
        if (clock != lastClock || signature != lastSignature || ! valid || ! inputAllowed || ! playing)
        {
            ++continuity;
            lastClock = clock;
            lastSignature = signature;
        }
        const int frames = input.getNumSamples(), channels = input.getNumChannels();
        constexpr auto limit = std::numeric_limits<std::int64_t>::max() / 4;
        if (! feed || ! playing || ! valid || ! inputAllowed || frames < 1 || frames > 8192 || channels < 1 || channels > 2
            || position < -limit || position > limit)
            return;
        const auto generation = licensed ? visual.inputGeneration() : 0;
        if (generation == 0) return;
        for (int offset = 0; offset < frames; offset += 256)
        {
            const int count = std::min (256, frames - offset);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < count; ++i)
                    interleaved[static_cast<size_t> (i * channels + c)] = input.getSample (c, offset + i);
            visual.enqueue (interleaved.data(), count, channels, position + offset, generation, continuity);
        }
    }

private:
    std::array<float, 512> interleaved {};
    AInputClockSignature lastSignature;
    int lastClock = 0;
    std::uint64_t continuity = 1;
};
}
