// H12: A を Kirin OS の Cue の値と同じ定義で測る（ReferenceKirinSpectrum）。Kirin OS の解析
// （kirin-audio-engine の measureReferenceSource）が合成信号に出した 100 ms ごとの 64 帯域と 4 帯域
// Balance（tests/fixtures/kirin_spectrum/kirin_os_ticks.json、0.001 dB 単位）と、同じ信号を Hypha で
// 測った値が丸めの幅（0.002 dB 以内）で一致すること（48 kHz と 44.1 kHz）。
// Kirin OS は 44.1 kHz の 7 番目のフレーム（0.7 s）だけ 1 サンプル早く読む（読み込みの位置合わせの丸め）。
// そのフレームは 1 サンプル前から測った値と比べる（定義は同じで、読む位置だけが違うことを示す）。
// 窓の p10・中央値・p90 は Kirin OS の Cue の集計（nearest-rank、Balance は power の平均）と同じ並べ方。
#include "reference_runtime_test_support.h"
#include "../src/reference_audition/ReferenceKirinSpectrum.h"

#include <cmath>

void testReferenceKirinSpectrum();

namespace
{
using hypha::reference_audition::KirinSpectrumMeter;

// fixture の説明どおりの信号（float32、interleaved の 2 ch）。
std::vector<float> syntheticSignal (int rate, int frames)
{
    constexpr double tau = 6.283185307179586476925286766559;
    std::uint32_t left = 12345, right = 67890;
    const auto noise = [] (std::uint32_t& state)
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<double> (state) / 2147483648.0 - 1.0;
    };
    std::vector<float> pcm (static_cast<size_t> (frames) * 2);
    for (int n = 0; n < frames; ++n)
    {
        const auto t = static_cast<double> (n) / rate;
        pcm[static_cast<size_t> (n) * 2] = static_cast<float> (0.5 * std::sin (tau * 1000.0 * t)
                                                               + 0.2 * std::sin (tau * 63.0 * t) + 0.05 * noise (left));
        pcm[static_cast<size_t> (n) * 2 + 1] = static_cast<float> (0.4 * std::sin (tau * 5000.0 * t) + 0.05 * noise (right));
    }
    return pcm;
}

void check (bool condition, const juce::String& message) { require (condition, message.toRawUTF8()); }

std::vector<double> numbers (const juce::var& array)
{
    std::vector<double> result;
    if (const auto* values = array.getArray())
        for (const auto& value : *values) result.push_back (static_cast<double> (value));
    return result;
}
}

void testReferenceKirinSpectrum()
{
    const auto fixture = juce::JSON::parse (juce::File (KIRIN_REFERENCE_FIXTURE_DIR)
                                                .getChildFile ("kirin_spectrum/kirin_os_ticks.json"));
    const auto* signals = fixture["signals"].getArray();
    require (signals != nullptr && signals->size() == 2, "the Kirin OS cross-check fixture is readable");
    for (const auto& signal : *signals)
    {
        const int rate = static_cast<int> (signal["sample_rate"]);
        const int ticks = static_cast<int> (signal["ticks"]);
        const auto spectrum = numbers (signal["spectrum_millidb"]);
        const auto balance = numbers (signal["balance_millidb"]);
        require (static_cast<int> (spectrum.size()) == ticks * 64 && static_cast<int> (balance.size()) == ticks * 4,
                 "the fixture holds 64 bands and 4 Balance bands per tick");
        KirinSpectrumMeter meter;
        check (meter.configure (rate, 2, 100), "the meter accepts " + juce::String (rate) + " Hz stereo");
        const auto tick = rate / 10;
        const auto pcm = syntheticSignal (rate, tick * ticks);
        double worstBand = 0.0, worstBalance = 0.0;
        std::vector<std::array<float, 64>> heard;
        std::vector<std::array<double, 4>> heardBalance;
        for (int index = 0; index < ticks; ++index)
        {
            // 100 ms を 256 フレームずつ（観測の Block と同じ大きさ）で入れる。
            for (int offset = 0; offset < tick; offset += 256)
                meter.push (pcm.data() + static_cast<size_t> (index * tick + offset) * 2, std::min (256, tick - offset));
            std::array<float, 64> bands {};
            std::array<double, 4> levels {};
            require (meter.lastFrame (bands, levels) && meter.framesHeld() == index + 1, "every 100 ms adds one frame");
            heard.push_back (bands);
            heardBalance.push_back (levels);
            if (rate == 44'100 && index == 7)
            {
                KirinSpectrumMeter early;  // Kirin OS が読んだ位置（1 サンプル前）から
                early.configure (rate, 2, 1);
                early.push (pcm.data() + static_cast<size_t> (index * tick - 1) * 2, tick);
                early.lastFrame (bands, levels);
            }
            for (size_t band = 0; band < 64; ++band)
                worstBand = std::max (worstBand, std::abs (bands[band] - spectrum[static_cast<size_t> (index) * 64 + band] / 1000.0));
            for (size_t band = 0; band < 4; ++band)
                worstBalance = std::max (worstBalance, std::abs (levels[band] - balance[static_cast<size_t> (index) * 4 + band] / 1000.0));
        }
        check (worstBand < 0.002 && worstBalance < 0.002,
                 "A is measured as Kirin OS measures a Cue at " + juce::String (rate) + " Hz (worst band "
                     + juce::String (worstBand, 4) + " dB, Balance " + juce::String (worstBalance, 4) + " dB)");

        // 窓：Kirin OS の Cue の集計と同じ並べ方（nearest-rank、Balance は power の平均）。
        const auto window = meter.window (ticks);
        require (window != nullptr && window->frames == ticks && window->centersHz.size() == 64
                     && std::abs (window->centersHz.front() - 20.0) < 1.0e-9 && std::abs (window->centersHz.back() - 20'000.0) < 1.0e-6,
                 "the window covers every frame on the Kirin OS band centres");
        for (size_t band = 0; band < 64; ++band)
        {
            std::vector<double> values;
            for (const auto& frame : heard) values.push_back (frame[band]);
            std::sort (values.begin(), values.end());
            const auto at = [&values] (double ratio) { return values[static_cast<size_t> (std::llround ((values.size() - 1) * ratio))]; };
            require (std::abs (window->p10Db[band] - at (0.1)) < 1.0e-4 && std::abs (window->medianDb[band] - at (0.5)) < 1.0e-4
                         && std::abs (window->p90Db[band] - at (0.9)) < 1.0e-4,
                     "the window's p10, median and p90 follow the Kirin OS Cue percentiles");
        }
        for (size_t band = 0; band < 4; ++band)
        {
            double power = 0.0;
            for (const auto& frame : heardBalance) power += std::pow (10.0, frame[band] / 10.0);
            require (std::abs (window->balanceDb[band] - 10.0 * std::log10 (power / ticks)) < 1.0e-6,
                     "the window's Balance is the power mean of its frames");
        }
        // 途切れ（シーク）で窓を捨て、短い窓は直近のフレームだけを使う。
        require (meter.window (3)->frames == 3 && meter.window (500)->frames == ticks, "a window never exceeds what was heard");
        meter.reset();
        require (meter.framesHeld() == 0 && meter.window (10) == nullptr, "a discontinuity starts a new window");
    }
    KirinSpectrumMeter unsupported;
    require (! unsupported.configure (32'000, 2, 100) && ! unsupported.configure (48'000, 3, 100),
             "rates below 40 kHz and more than two channels are not measured");
}
