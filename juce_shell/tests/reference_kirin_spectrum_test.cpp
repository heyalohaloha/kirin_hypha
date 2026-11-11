// A を Kirin OS の Cue の値と同じ定義で測る（ReferenceKirinSpectrum）。Kirin OS の解析
// （kirin-audio-engine の measureReferenceSource）が合成信号に出した 100 ms ごとの 64 帯域と 4 帯域
// Balance（tests/fixtures/kirin_spectrum/kirin_os_ticks.json、0.001 dB 単位）と、同じ信号を Hypha で
// 測った値が丸めの幅（0.002 dB 以内）で一致すること（48 kHz と 44.1 kHz）。
// Kirin OS は 44.1 kHz の 7 番目のフレーム（0.7 s）だけ 1 サンプル早く読む（読み込みの位置合わせの丸め）。
// そのフレームは 1 サンプル前から測った値と比べる（定義は同じで、読む位置だけが違うことを示す）。
// 窓の p10・中央値・p90 は Kirin OS の Cue の集計（nearest-rank、Balance は power の平均）と同じ並べ方。
#include "reference_runtime_test_support.h"
#include "../src/reference_audition/ReferenceKirinSpectrum.h"
#include "../src/reference_audition/ReferenceCueMatch.h"
#include "../src/reference_audition/ReferenceVisualPreferences.h"
#include "../src/reference_audition/ReferenceComparisonSettings.h"

#include <chrono>
#include <cmath>
#include <iostream>

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

// B の Balance の重ね比べ：色は出した時に決まり、ほかの線を出し入れしても変わらない。一覧に無くなった線は次の
// 出し入れで外れて色を空ける。project には ONE／ALL と線と色を書き、前の版の state は ONE のまま。
void testBalanceOverlay()
{
    namespace ref = hypha::reference_audition;
    ref::BalanceOverlay overlay;
    const std::vector<juce::String> listed { "p/c/one", "p/c/two", "p/c/three", "target-1a" };
    juce::XmlElement none ("ReferenceChoices"); overlay.write (none);
    check (none.getNumChildElements() == 0, "ONE with no lines writes nothing");
    for (const auto& id : listed) overlay.toggle (id, listed);
    check (overlay.lines.size() == 4 && overlay.find ("p/c/three")->colour == 2 && overlay.find ("target-1a")->colour == 3,
           "lines take colours in the order shown");
    overlay.toggle ("p/c/two", listed);
    check (overlay.find ("p/c/two") == nullptr && overlay.find ("p/c/one")->colour == 0 && overlay.find ("p/c/three")->colour == 2
           && overlay.find ("target-1a")->colour == 3, "hiding a line keeps every other colour");
    overlay.toggle ("p/c/two", listed);
    check (overlay.find ("p/c/two")->colour == 1 && overlay.find ("p/c/three")->colour == 2, "a line shown again takes a free colour");
    overlay.toggle ("p/c/one", { "p/c/one", "p/c/three", "target-1a" });
    check (overlay.find ("p/c/two") == nullptr && overlay.find ("p/c/one") == nullptr && overlay.lines.size() == 2,
           "a line the list no longer has is dropped at the next press");
    overlay.toggle ("p/c/one", listed);
    check (overlay.find ("p/c/one")->colour == 0, "its colour is free again");
    overlay.toggle ("bad id", listed);
    check (overlay.find ("bad id") == nullptr, "an ID outside the saved form is not shown");
    overlay.all = true;
    juce::XmlElement saved ("ReferenceChoices"); overlay.write (saved);
    check (ref::BalanceOverlay::read (saved) == overlay, "ALL, the lines and their colours round trip");
    auto* written = saved.getChildByName ("Overlay");
    for (const auto* broken : { R"(<Line id="p/c/x" colour="0"/>)", R"(<Line id="p/c/three" colour="9"/>)",
                                R"(<Line id="has space" colour="9"/>)", R"(<Line id="p/c/y" colour="99"/>)", R"(<Line colour="8"/>)" })
        written->addChildElement (juce::parseXML (broken).release());
    check (ref::BalanceOverlay::read (saved) == overlay, "a repeated colour or ID, an invalid ID or colour is skipped");
    ref::ReferenceComparisonSettings settings; settings.overlay = overlay;
    juce::XmlElement state ("KirinHyphaState"); settings.write (state);
    check (ref::ReferenceComparisonSettings::read (state).overlay == overlay, "the project keeps the overlay");
    state.getChildByName ("ReferenceChoices")->removeChildElement (state.getChildByName ("ReferenceChoices")->getChildByName ("Overlay"), true);
    const auto older = ref::ReferenceComparisonSettings::read (state).overlay;
    check (! older.all && older.lines.empty(), "a project saved before the overlay opens on ONE");
}

// 図の窓（3 S・10 S・30 S・CUE・ALL）：新しいフレームから数え、足りなければあるだけで集計する。
void testDisplayWindows()
{
    namespace ref = hypha::reference_audition;
    check (ref::displayWindowBlocks (ref::AWindowChoice::seconds3, 450) == 30, "3 S is 30 frames");
    check (ref::displayWindowBlocks (ref::AWindowChoice::seconds10, 450) == 100, "10 S is 100 frames");
    check (ref::displayWindowBlocks (ref::AWindowChoice::seconds30, 450) == 300, "30 S is 300 frames");
    check (ref::displayWindowBlocks (ref::AWindowChoice::cue, 450) == 450, "CUE is the role's Cue length");
    // CUE は Cue の長さのまま。MATCH の窓の下限 10 秒は図に掛けない。上限はどちらも 10 分。
    check (ref::cueLengthBlocks (0, 48'000 * 5, 48'000) == 50 && ref::cueWindowBlocks (0, 48'000 * 5, 48'000) == 100,
           "a 5 second Cue sums 5 seconds while MATCH keeps its 10 second floor");
    check (ref::cueLengthBlocks (0, 48'000 * 2, 48'000) == 20, "a short Cue is not stretched to 3 seconds");
    check (ref::cueLengthBlocks (0, 48'000 * 900, 48'000) == ref::aWindowMaximumBlocks
               && ref::cueWindowBlocks (0, 48'000 * 900, 48'000) == ref::aWindowMaximumBlocks, "both stop at 10 minutes");
    check (ref::cueLengthBlocks (5, 5, 48'000) == 100 && ref::cueLengthBlocks (0, 48'000, 0) == 100, "no Cue keeps the 10 second default");
    check (ref::displayWindowBlocks (ref::AWindowChoice::all, 450) == ref::aWindowMaximumBlocks, "ALL is what is held, up to the limit");
    check (ref::aWindowMaximumBlocks == 6'000, "the limit is 10 minutes of 100 ms frames");
    for (const int seconds : { 3, 47, 600 })
        check (ref::displayWindowBlocks (ref::AWindowChoice::custom, 450, seconds) == seconds * 10, "custom seconds use 100 ms frames");
    for (const auto* invalid : { "", "2", "601", "NaN", "-3", "3.5", "1e2", "999999999999" })
        check (ref::parseAWindowSeconds (invalid) == 0, "invalid seconds are refused without clipping");
    ref::AWindowChoices windows;
    juce::XmlElement defaults ("State"); windows.write (defaults);
    check (defaults.getChildByName ("AWindow") == nullptr, "default windows are not written");
    windows.reference = windows.check = ref::AWindowChoice::custom;
    windows.referenceSeconds = 3; windows.checkSeconds = 600;
    juce::XmlElement state ("State"); windows.write (state);
    const auto copy = ref::AWindowChoices::read (state);
    check (copy.blocksOf (3, 450) == 30 && copy.blocksOf (2, 450) == 6000, "custom windows remember each role through state");
    state.getChildByName ("AWindow")->setAttribute ("ref_seconds", "3.5");
    const auto invalidState = ref::AWindowChoices::read (state);
    check (invalidState.reference == ref::AWindowChoice::seconds10 && invalidState.blocksOf (2, 450) == 6000,
           "one invalid saved window keeps the other role");
    const auto old = ref::AWindowChoices::read (defaults);
    check (old.reference == ref::AWindowChoice::seconds10 && old.check == ref::AWindowChoice::cue, "old state keeps the old defaults");
    testBalanceOverlay();
    const int rate = 48'000, frame = rate / 10;
    constexpr double tau = 6.283185307179586476925286766559;
    KirinSpectrumMeter meter;
    check (meter.configure (rate, 2, 120), "the meter takes this format");
    std::vector<float> pcm (static_cast<size_t> (frame) * 2);
    std::int64_t sample = 0;
    const auto feed = [&] (int frames, double amplitude)
    {
        for (int index = 0; index < frames; ++index)
        {
            for (int n = 0; n < frame; ++n, ++sample)
                pcm[static_cast<size_t> (n) * 2] = pcm[static_cast<size_t> (n) * 2 + 1]
                    = static_cast<float> (amplitude * std::sin (tau * 1'000.0 * static_cast<double> (sample) / rate));
            meter.push (pcm.data(), frame);
        }
    };
    feed (50, 0.5);   // 大きい 5 秒
    feed (30, 0.05);  // 小さい 3 秒
    const auto band = static_cast<size_t> (std::lower_bound (meter.centersHz().begin(), meter.centersHz().end(), 1'000.0) - meter.centersHz().begin());
    const auto last3 = meter.window (30), last8 = meter.window (ref::aWindowMaximumBlocks);
    check (last3 != nullptr && last3->frames == 30 && last3->wantedFrames == 30, "3 S sums the last 30 frames");
    check (last8 != nullptr && last8->frames == 80, "a longer window sums what is held");
    check (std::abs (last8->medianDb[band] - last3->medianDb[band] - 20.0f) < 1.0f, "the short window leaves the loud part out");
    check (std::abs (last8->p10Db[band] - last3->medianDb[band]) < 1.0f, "the long window spans both parts");
    // Balance は窓の power の平均：大きい 50 フレーム（+20 dB）と小さい 30 フレーム。
    check (std::abs (last8->balanceDb[1] - last3->balanceDb[1] - 10.0 * std::log10 ((50.0 * 100.0 + 30.0) / 80.0)) < 0.1, "Balance averages power over the window");
    feed (60, 0.5);
    const auto held = meter.window (ref::aWindowMaximumBlocks);
    check (held != nullptr && held->frames == 120, "ALL stops at what the meter holds");
    meter.reset();
    check (meter.window (ref::aWindowMaximumBlocks) == nullptr && meter.framesHeld() == 0, "RESET leaves nothing to sum");
}

// ALL の上限：6,000 フレームを正しく集計し、それより古いフレームは入らない。時間は記録だけ（CI で揺れるので判定にしない）。
void testFullWindow()
{
    namespace ref = hypha::reference_audition;
    const int rate = 48'000, frame = rate / 10;
    constexpr double tau = 6.283185307179586476925286766559;
    KirinSpectrumMeter meter;
    check (meter.configure (rate, 2, ref::aWindowMaximumBlocks), "the meter holds 10 minutes");
    // 1 kHz は 100 ms にちょうど 100 周期なので、同じフレームを繰り返しても位相が続く。
    const auto levelFrame = [&] (double amplitude)
    {
        std::vector<float> pcm (static_cast<size_t> (frame) * 2);
        for (int n = 0; n < frame; ++n)
            pcm[static_cast<size_t> (n) * 2] = pcm[static_cast<size_t> (n) * 2 + 1]
                = static_cast<float> (amplitude * std::sin (tau * 1'000.0 * n / rate));
        return pcm;
    };
    const auto oldest = levelFrame (1.0), quiet = levelFrame (0.01), middle = levelFrame (0.1), loud = levelFrame (0.5);
    for (int index = 0; index < 100; ++index) meter.push (oldest.data(), frame);  // 上限から外れる
    for (int index = 0; index < ref::aWindowMaximumBlocks; ++index)
    {
        const auto part = index % 10;
        meter.push ((part < 2 ? quiet : part > 7 ? loud : middle).data(), frame);
    }
    check (meter.framesHeld() == ref::aWindowMaximumBlocks, "the meter stops at 6,000 frames");
    std::shared_ptr<const hypha::reference_audition::KirinSpectrumWindow> whole;
    const auto started = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < 20; ++repeat) whole = meter.window (ref::aWindowMaximumBlocks);
    const std::chrono::duration<double, std::milli> spent = std::chrono::steady_clock::now() - started;
    std::cout << "A window over 6000 frames: " << spent.count() / 20.0 << " ms per publish\n";
    check (whole != nullptr && whole->frames == ref::aWindowMaximumBlocks, "ALL sums all 6,000 frames");
    const auto band = static_cast<size_t> (std::lower_bound (meter.centersHz().begin(), meter.centersHz().end(), 1'000.0) - meter.centersHz().begin());
    // 2 割ずつの小さいフレームと大きいフレームの中に p10 と p90 が入る。
    const auto single = [&] (const std::vector<float>& pcm)
    {
        KirinSpectrumMeter one;
        check (one.configure (rate, 2, 2), "the reference meter takes this format");
        one.push (pcm.data(), frame);
        return one.window (1);
    };
    const auto low = single (quiet), mid = single (middle), high = single (loud);
    check (std::abs (whole->p10Db[band] - low->medianDb[band]) < 0.01f, "p10 of 6,000 frames is in the quiet frames");
    check (std::abs (whole->medianDb[band] - mid->medianDb[band]) < 0.01f, "the median of 6,000 frames is the middle level");
    check (std::abs (whole->p90Db[band] - high->medianDb[band]) < 0.01f, "p90 of 6,000 frames is in the loud frames");
    const auto power = [] (const std::shared_ptr<const hypha::reference_audition::KirinSpectrumWindow>& window)
    { return std::pow (10.0, window->balanceDb[1] / 10.0); };
    const auto mean = (2.0 * power (low) + 6.0 * power (mid) + 2.0 * power (high)) / 10.0;
    check (std::abs (whole->balanceDb[1] - 10.0 * std::log10 (mean)) < 0.01, "Balance averages the power of exactly the held frames");
}

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
    testDisplayWindows();
    testFullWindow();
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
