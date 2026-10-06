// 2026-10-04：範囲の帯（ReferenceDynamicsRange）。Kirin OS（kirin-audio-engine の measureReferenceSource）が合成信号に
// 出した区間ごとの値（tests/fixtures/kirin_dynamics/kirin_os_hops.json）と、同じ信号を Hypha の A の計器
// （VisualMeter の 100 ms の bin）に通して区間にまとめ直した値が一致すること。hop 100 ms（48 kHz・6 秒）と、
// 2 つの bin を 1 区間にまとめる hop 200 ms（8 kHz・210 秒の曲の先頭 8 秒）。Kirin OS の値の切り出しと p10・中央値・p90。
// Kirin OS は 100 ms の窓の頭を秒に直してから読む（symphonia の calc_timestamp：整数秒 × レート ＋ trunc(レート × 小数部)）
// ので、約 4 割の窓を 1 sample 早く読む（Kirin OS 側の別の作業）。Kirin OS の値とは同じ読み方の窓で比べ、定義そのものは
// 正しい窓（区間どおり）で同じ sample から直接数えた値と比べる。
#include "reference_runtime_test_support.h"
#include "../src/reference_audition/ReferenceDynamicsRange.h"

#include <cmath>

void testReferenceDynamicsRange();

namespace
{
using namespace hypha::reference_audition;

// fixture の説明どおりの信号（float32、interleaved の 2 ch）。生成の式は fixture を作った Node のスクリプトと同じ。
std::vector<float> syntheticSignal (int rate, int frames)
{
    constexpr double pi = 3.141592653589793;
    std::uint32_t left = 12345, right = 67890;
    const auto noise = [] (std::uint32_t& state)
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<double> (state) / 2147483648.0 - 1.0;
    };
    const auto period = std::llround (rate * 0.7), burstLength = std::llround (rate * 0.02);
    std::vector<float> pcm (static_cast<size_t> (frames) * 2);
    for (int n = 0; n < frames; ++n)
    {
        const auto t = static_cast<double> (n) / rate;
        const auto gain = 0.1 + 0.25 * (0.5 + 0.5 * std::sin (2 * pi * 0.5 * t));
        const auto burst = n % period < burstLength ? 0.3 : 0.0;
        const auto nl = noise (left), nr = noise (right);
        pcm[static_cast<size_t> (n) * 2] = static_cast<float> (gain * std::sin (2 * pi * 220 * t) + burst * std::sin (2 * pi * 1500 * t) + 0.05 * nl);
        pcm[static_cast<size_t> (n) * 2 + 1] = static_cast<float> (gain * std::sin (2 * pi * 220 * t + pi / 3) + 0.05 * nr);
    }
    return pcm;
}

// Kirin OS が 100 ms の窓を読み始める sample（read_window：秒に直して symphonia の calc_timestamp で戻す）。
std::int64_t kirinReadStart (std::int64_t offset, int rate)
{
    const auto seconds = static_cast<double> (offset) / rate;
    const auto whole = std::floor (seconds);
    return static_cast<std::int64_t> (whole) * rate + static_cast<std::int64_t> ((1.0 * rate) * (seconds - std::trunc (seconds)));
}

// A の計器と同じく 512 sample ずつ push し、100 ms ごとに bin を閉じる。kirinReads なら各 100 ms を Kirin OS と同じ位置から読む。
std::vector<KirinReferenceVisualBin> tickBins (const std::vector<float>& pcm, int rate, int ticks, bool kirinReads)
{
    auto* meter = kirin_reference_visual_create (static_cast<std::uint32_t> (rate), 2);
    require (meter != nullptr, "the A meter opens");
    std::vector<KirinReferenceVisualBin> bins;
    const auto tick = rate / 10;
    for (int index = 0; index < ticks; ++index)
    {
        const auto nominal = static_cast<std::int64_t> (index) * tick;
        const auto start = static_cast<size_t> (kirinReads ? kirinReadStart (nominal, rate) : nominal);
        for (int done = 0; done < tick;)
        {
            const auto frames = std::min (512, tick - done);
            require (kirin_reference_visual_push (meter, pcm.data() + (start + static_cast<size_t> (done)) * 2, static_cast<size_t> (frames) * 2),
                     "the A meter accepts the block");
            done += frames;
        }
        KirinReferenceVisualBin bin {};
        require (kirin_reference_visual_finish (meter, &bin), "a 100 ms bin closes");
        bins.push_back (bin);
    }
    kirin_reference_visual_drop (meter);
    return bins;
}

void requireText (bool condition, const juce::String& message) { require (condition, message.toRawUTF8()); }

double number (const juce::var& value) { return value.isVoid() ? std::numeric_limits<double>::quiet_NaN() : static_cast<double> (value); }

bool same (double ours, const juce::var& theirs, double tolerance)
{
    const auto expected = number (theirs);
    if (! std::isfinite (expected)) return ! std::isfinite (ours);
    return std::isfinite (ours) && std::abs (ours - expected) <= tolerance;
}
}

void testReferenceDynamicsRange()
{
    const auto fixture = juce::JSON::parse (juce::File (KIRIN_REFERENCE_FIXTURE_DIR).getChildFile ("kirin_dynamics/kirin_os_hops.json"));
    const auto* signals = fixture["signals"].getArray();
    require (signals != nullptr && signals->size() == 2, "the Kirin OS dynamics fixture is readable");
    for (const auto& signal : *signals)
    {
        const int rate = static_cast<int> (signal["sample_rate"]);
        const int hops = static_cast<int> (signal["hops"]);
        const auto hopSamples = static_cast<std::int64_t> (static_cast<double> (signal["hop_samples"]));
        const int binsPerHop = static_cast<int> (hopSamples / (rate / 10));
        require (binsPerHop >= 1 && hopSamples == binsPerHop * (rate / 10), "the Kirin OS hop is whole 100 ms ticks");
        const auto pcm = syntheticSignal (rate, hops * static_cast<int> (hopSamples));
        const auto bins = tickBins (pcm, rate, hops * binsPerHop, true);
        const auto ours = aggregateHops (bins.data(), bins.size(), binsPerHop, 2);
        require (static_cast<int> (ours.size()) == hops && ours.hopSamples == hopSamples, "every Kirin OS hop is rebuilt from whole bins");
        const auto check = [&] (DynamicsFact fact, const char* key, double tolerance, int from = 0)
        {
            const auto* theirs = signal[key].getArray();
            requireText (theirs != nullptr && static_cast<int> (theirs->size()) == hops, juce::String ("fixture ") + key);
            for (int hop = from; hop < hops; ++hop)
                requireText (same (ours[fact][static_cast<size_t> (hop)], (*theirs)[hop], tolerance),
                         juce::String (key) + " hop " + juce::String (hop) + " at " + juce::String (rate) + " Hz: "
                             + juce::String (ours[fact][static_cast<size_t> (hop)], 6) + " vs " + (*theirs)[hop].toString());
        };
        check (DynamicsFact::crest, "crest_db", 1.0e-6);
        check (DynamicsFact::lufsM, "lufs_m", 1.0e-6);
        check (DynamicsFact::lufsS, "lufs_s", 1.0e-6);
        check (DynamicsFact::correlation, "correlation", 1.0e-9);
        check (DynamicsFact::width, "width", 1.0e-6);
        // 定義：正しい窓（区間どおり）でも、同じ sample から区間ごとに直接数えた値（Kirin OS の式そのまま）と一致する。
        {
            const auto exactBins = tickBins (pcm, rate, hops * binsPerHop, false);
            const auto exact = aggregateHops (exactBins.data(), exactBins.size(), binsPerHop, 2);
            double previous = 0.0;
            for (int hop = 0; hop < hops; ++hop)
            {
                double cross = 0.0, e0 = 0.0, e1 = 0.0, mid = 0.0, side = 0.0, peak = 0.0;
                for (std::int64_t n = hop * hopSamples; n < (hop + 1) * hopSamples; ++n)
                {
                    const double l = pcm[static_cast<size_t> (n) * 2], r = pcm[static_cast<size_t> (n) * 2 + 1];
                    cross += l * r; e0 += l * l; e1 += r * r;
                    mid += (l + r) * 0.5 * ((l + r) * 0.5); side += (l - r) * 0.5 * ((l - r) * 0.5);
                    peak = std::max ({ peak, std::abs (l), std::abs (r) });
                }
                const auto frames = static_cast<double> (hopSamples);
                const auto rms = std::sqrt ((e0 + e1) / (frames * 2.0));
                const auto at = static_cast<size_t> (hop);
                requireText (std::abs (exact[DynamicsFact::correlation][at] - cross / std::sqrt (e0 * e1)) < 1.0e-9
                                 && std::abs (exact[DynamicsFact::width][at] - std::sqrt (side / frames) / (std::sqrt (mid / frames) + 1e-10) * 100.0) < 1.0e-6
                                 && std::abs (exact[DynamicsFact::peak][at] - 20.0 * std::log10 (peak)) < 1.0e-9
                                 && std::abs (exact[DynamicsFact::rms][at] - 20.0 * std::log10 (rms)) < 1.0e-9
                                 && (hop == 0 || std::abs (exact[DynamicsFact::onset][at] - std::max (0.0, rms - previous) / rms) < 1.0e-9)
                                 && (std::isfinite (exact[DynamicsFact::lufsM][at])
                                         ? std::abs (exact[DynamicsFact::attack][at] - (20.0 * std::log10 (peak) - exact[DynamicsFact::lufsM][at])) < 1.0e-9
                                         : std::isnan (exact[DynamicsFact::attack][at])),
                             "the definition holds on the same samples, hop " + juce::String (hop));
                previous = rms;
            }
        }
        // ピーク・RMS：Kirin OS は ch ごとの millidB。大きい ch と、電力の平均にして比べる。
        const auto* peaks = signal["peak_millidb"].getArray();
        const auto* rms = signal["rms_millidb"].getArray();
        require (peaks != nullptr && rms != nullptr && peaks->size() == 2 && rms->size() == 2, "fixture peaks and RMS");
        for (int hop = 0; hop < hops; ++hop)
        {
            const auto peak = std::max (number ((*peaks)[0][hop]), number ((*peaks)[1][hop])) / 1000.0;
            const auto power = (std::pow (10.0, number ((*rms)[0][hop]) / 10'000.0) + std::pow (10.0, number ((*rms)[1][hop]) / 10'000.0)) / 2.0;
            requireText (std::abs (ours[DynamicsFact::peak][static_cast<size_t> (hop)] - peak) < 0.0006
                         && std::abs (ours[DynamicsFact::rms][static_cast<size_t> (hop)] - 10.0 * std::log10 (power)) < 0.0012,
                     "peak and RMS hop " + juce::String (hop));
        }
        // 立ち上がり：Kirin OS は曲の頭の前を 0 として最初の区間を 32767 にする。A の窓は前を知らないので最初は出さない。
        const auto* onset = signal["onset_q15"].getArray();
        require (onset != nullptr && ! std::isfinite (ours[DynamicsFact::onset][0]), "the first onset in a window is unknown");
        for (int hop = 1; hop < hops; ++hop)
            requireText (std::abs (ours[DynamicsFact::onset][static_cast<size_t> (hop)] * 32'767.0 - number ((*onset)[hop])) <= 0.5 + 1.0e-6,
                     "onset hop " + juce::String (hop));

        // C・V の側：Kirin OS が Hypha に渡す形（milli の整数）に丸めた値から、[start, end) に丸ごと入る区間を切り出す。
        RuntimeDetailedMeasurement measurement;
        const auto series = [&] (const char* key, double scale)
        {
            RuntimeNullableIntegerSeries values;
            for (const auto& value : *signal[key].getArray())
                values.push_back (value.isVoid() ? std::optional<std::int64_t> {} : std::optional<std::int64_t> (std::llround (number (value) * scale)));
            return values;
        };
        measurement.dynamics = RuntimeMeasurementTimeline { hopSamples, { { "crest_millidb", series ("crest_db", 1000.0) } } };
        measurement.loudness = RuntimeMeasurementTimeline { hopSamples, { { "lufs_m_millilu", series ("lufs_m", 1000.0) },
                                                                          { "lufs_s_millilu", series ("lufs_s", 1000.0) } } };
        measurement.stereo = RuntimeMeasurementTimeline { hopSamples, { { "correlation_milli", series ("correlation", 1000.0) },
                                                                        { "width_basis_points", series ("width", 100.0) } } };
        const auto slice = kirinHops (measurement, hopSamples * 10 + hopSamples / 2, hopSamples * 20);
        require (slice.size() == 9 && slice.hopSamples == hopSamples, "a Cue keeps the hops wholly inside it");
        require (same (slice[DynamicsFact::crest][0], (*signal["crest_db"].getArray())[11], 0.0006)
                     && same (slice[DynamicsFact::width][8], (*signal["width"].getArray())[19], 0.006)
                     && same (slice[DynamicsFact::lufsM][3], (*signal["lufs_m"].getArray())[14], 0.0006),
                 "the slice starts at the first whole hop and reads Kirin OS's units");
        require (std::isnan (slice[DynamicsFact::onset][0]) && std::isnan (slice[DynamicsFact::peak][0])
                     && std::isnan (slice[DynamicsFact::attack][0]),
                 "facts Kirin OS did not send stay unknown");
        // アタック（ピーク − LUFS-M）：Kirin OS が波形の値を送れば、同じ区間のピークと LUFS-M から作る。
        RuntimeMeasurementWaveform waveform;
        waveform.framesPerBin = hopSamples;
        for (int c = 0; c < 2; ++c)
        {
            waveform.samplePeakMillidbfs.emplace_back();
            waveform.rmsMillidbfs.emplace_back();
            for (int hop = 0; hop < hops; ++hop)
            {
                waveform.samplePeakMillidbfs.back().push_back (std::llround (number ((*peaks)[c][hop])));
                waveform.rmsMillidbfs.back().push_back (std::llround (number ((*rms)[c][hop])));
            }
        }
        measurement.waveform = waveform;
        const auto withPeaks = kirinHops (measurement, hopSamples * 10 + hopSamples / 2, hopSamples * 20);
        for (std::size_t hop = 0; hop < withPeaks.size(); ++hop)
            requireText (std::isfinite (withPeaks[DynamicsFact::peak][hop]) && std::isfinite (withPeaks[DynamicsFact::lufsM][hop])
                             && std::abs (withPeaks[DynamicsFact::attack][hop]
                                          - (withPeaks[DynamicsFact::peak][hop] - withPeaks[DynamicsFact::lufsM][hop])) < 1.0e-9,
                         "the attack is the same hop's peak minus LUFS-M, hop " + juce::String ((int) hop));
        require (kirinHops (measurement, hopSamples / 4, hopSamples / 2).size() == 1, "a Cue shorter than a hop uses the hop it overlaps");
    }
    // A の 100 ms の bin を貯める部品：半端な大きさで push しても 100 ms ごとに閉じ、途切れで捨て、60 秒までを保つ。
    {
        DynamicsTicks ticks;
        const auto pcm = syntheticSignal (8'000, 8'000 * 62);
        int frame = 0;
        while (frame < 800 * 25)
        {
            const auto count = std::min (333, 800 * 25 - frame);
            ticks.push (pcm.data() + static_cast<size_t> (frame) * 2, count, 8'000, 2);
            frame += count;
        }
        require (ticks.bins() && ticks.bins()->size() == 25 && ticks.channels() == 2
                     && std::all_of (ticks.bins()->begin(), ticks.bins()->end(), [] (const auto& bin) { return bin.frames == 800; }),
                 "A's bins close every 100 ms whatever the block size");
        ticks.reset();
        require (ticks.bins() == nullptr, "a discontinuity drops A's bins");
        for (frame = 0; frame < 8'000 * 61; frame += 400) ticks.push (pcm.data() + static_cast<size_t> (frame) * 2, 400, 8'000, 2);
        require (ticks.bins()->size() == DynamicsTicks::capacity, "A keeps its last 60 seconds");
        ticks.push (pcm.data(), 400, 48'000, 2);
        require (ticks.bins() == nullptr, "a new rate starts a new meter");
    }
    // p10・中央値・p90 は NaN を除いた nearest-rank。
    const auto range = rangeOf ({ 5.0, std::numeric_limits<double>::quiet_NaN(), 1.0, 9.0, 3.0, 7.0, 2.0, 8.0, 4.0, 6.0, 10.0 });
    require (range.count == 10 && range.p10 == 2.0 && range.median == 6.0 && range.p90 == 9.0, "ranges are nearest-rank without NaN");
    require (! rangeOf ({ std::numeric_limits<double>::quiet_NaN() }).valid(), "nothing measured is no range");
    // mono：相関と幅は無い。
    std::vector<KirinReferenceVisualBin> mono (4);
    for (auto& bin : mono) { bin.frames = 4800; bin.rms[0] = 0.1; bin.peak[0] = 0.3; bin.true_peak = 0.31; }
    const auto monoHops = aggregateHops (mono.data(), mono.size(), 2, 1);
    require (monoHops.size() == 2 && std::isnan (monoHops[DynamicsFact::width][0]) && std::isnan (monoHops[DynamicsFact::correlation][0])
                 && std::abs (monoHops[DynamicsFact::crest][0] - 20.0 * std::log10 (0.31 / 0.1)) < 1.0e-9,
             "mono has crest but no width or correlation");
}
