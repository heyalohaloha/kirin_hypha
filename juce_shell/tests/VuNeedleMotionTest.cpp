#include "../src/HyphaVuNeedleMotion.h"
#include "../src/HyphaVuLiveTap.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
using namespace hypha::hybrid_vu;
void require (bool value, const char* message)
{
    if (! value) { std::cerr << message << '\n'; std::exit (1); }
}
// Independent continuous-time oracle; the implementation uses the prepare-time state matrix.
double response (double seconds)
{
    const auto z = NeedleStep::zeta, w = NeedleStep::omega;
    const auto b = std::sqrt (1.0 - z * z);
    return 1.0 - std::exp (-z * w * seconds)
        * (std::cos (w * b * seconds) + z / b * std::sin (w * b * seconds));
}
void verifyRate (int rate)
{
    LiveTap tap;
    tap.prepare (rate, 2048);
    const auto initial = tap.publications();
    float sample = static_cast<float> (2.0 / 3.14159265358979323846);
    tap.push (&sample, 1, 1);
    require (tap.publications() == initial && ! std::isfinite (tap.read()[0]), "disabled tap does no work");
    tap.setEnabled (true);
    double half = 0, crossing = 0, maximum = 0;
    const double input = static_cast<double> (sample) * 1.5707963267948966;
    for (int frame = 1; frame <= rate * 2; ++frame)
    {
        tap.push (&sample, 1, 1);
        const auto reading = tap.reading();
        require (reading.coherent && reading.channels == 1 && reading.feeding, "a block publishes a coherent state");
        const auto y = reading.needles[0].value / input;
        const double t = static_cast<double> (frame) / rate;
        require (std::abs (y - response (t)) < 1.0e-7, "sample response matches independent continuous oracle at every rate");
        if (half == 0 && y >= 0.5) half = t;
        if (crossing == 0 && y >= 0.99) crossing = t;
        maximum = std::max (maximum, y);
    }
    require (std::abs (half - 0.1121089769) <= 1.1 / rate, "half crossing is 112.109 ms");
    require (std::abs (crossing - 0.3) <= 1.1 / rate, "first 99% crossing is 300 ms");
    require (std::abs (maximum - 1.0125) < 1.0e-6, "overswing is 1.25%, not clamped away");
    require (std::abs (tap.reading().needles[0].value - input) < 1.0e-8, "DC gain is exactly unity");
    sample = 0.0f;
    double fallHalf = 0, fall99 = 0, minimum = 1;
    for (int frame = 1; frame <= rate; ++frame)
    {
        tap.push (&sample, 1, 1);
        const double t = static_cast<double> (frame) / rate;
        const auto y = tap.reading().needles[0].value / input;
        require (std::abs (y - (1.0 - response (t))) < 1.0e-7, "return is the same physical response as attack");
        if (fallHalf == 0 && y <= 0.5) fallHalf = t;
        if (fall99 == 0 && y <= 0.01) fall99 = t;
        minimum = std::min (minimum, y);
    }
    require (std::abs (fallHalf - half) <= 1.1 / rate && std::abs (fall99 - crossing) <= 1.1 / rate,
             "rise and return have matching crossings");
    require (std::abs (minimum + 0.0125) < 1.0e-6, "return underswing is preserved internally");
    std::cout << rate << " Hz: half=" << half * 1000 << " ms, 99%=" << crossing * 1000
              << " ms, overswing=" << (maximum - 1) * 100 << "%, return half=" << fallHalf * 1000 << " ms\n";
    for (int frame = 0; frame < rate * 10; ++frame) tap.push (&sample, 1, 1);
    require (tap.reading().needles[0].value == 0.0 && tap.reading().needles[0].velocity == 0.0,
             "zero tail flushes without subnormal state");
}
void verifySineAndBlocks()
{
    for (int rate : { 44100, 48000, 88200, 96000, 176400, 192000 })
    {
        LiveTap tap; tap.prepare (rate, 2048); tap.setEnabled (true);
        double oldSum = 0;
        const int frames = rate * 2, window = static_cast<int> (std::lround (rate * .3));
        std::vector<float> input (static_cast<size_t> (frames) * 2);
        for (int frame = 0; frame < frames; ++frame)
        {
            const auto v = static_cast<float> (.1 * std::sin (2 * 3.14159265358979323846 * 1000 * frame / rate));
            input[(size_t) frame * 2] = v; input[(size_t) frame * 2 + 1] = v * .5f;
            if (frame >= frames - window) oldSum += std::abs (v);
        }
        int at = 0, block = 0;
        while (at < frames)
        {
            const auto count = std::min (block++ % 2 == 0 ? 2048 : 63, frames - at);
            tap.push (input.data() + at * 2, count, 2); at += count;
        }
        const auto legacy = 20 * std::log10 (oldSum / window * 1.5707963267948966);
        require (std::abs (tap.level (0) - legacy) < 0.0002, "steady sine matches the old full-wave mean calibration");
        require (std::abs (tap.level (1) - tap.level (0) + 6.020599913) < 1.0e-7, "channels stay independent");
        LiveTap singles; singles.prepare (rate, 1); singles.setEnabled (true);
        for (int frame = 0; frame < frames; ++frame) singles.push (input.data() + frame * 2, 1, 2);
        require (singles.reading().needles[0].value == tap.reading().needles[0].value,
                 "callback size cannot change a bit of the response");
        std::cout << rate << " Hz steady: old=" << legacy << " new=" << tap.level (0) << " dBFS\n";
    }
}
void verifyInvalidBlocks()
{
    for (const auto invalid : { std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity() })
        for (int offset : { 0, 511 })
        {
            LiveTap tap; tap.prepare (48000, 512); tap.setEnabled (true);
            std::vector<float> samples (1024, .1f);
            for (int block = 0; block < 200; ++block) tap.push (samples.data(), 512, 2);
            const auto settled = tap.reading();
            NeedleMotionPair ui; ui.setVisible (true);
            for (int block = 0; block < 48; ++block)
            {
                samples[(size_t) offset * 2] = invalid;
                tap.push (samples.data(), 512, 2);
                ui.accept (tap.reading()); ui.tick (1.0 / 60);
            }
            require (std::abs (ui.position (0)) < settled.needles[0].value * .01
                     && std::abs (ui.position (1)) < settled.needles[1].value * .01,
                     "repeated invalid blocks return both needles instead of pinning their last reading");
            samples.assign (1024, .1f);
            tap.push (samples.data(), 512, 2);
            require (tap.reading().feeding && std::isfinite (tap.level (0)), "finite input resumes after repeated invalid blocks");
        }
}
void verifyStops()
{
    LiveTap tap; tap.prepare (48000, 512); tap.setEnabled (true);
    std::vector<float> samples (512, static_cast<float> (.1 * 2 / 3.14159265358979323846));
    std::int64_t position = 0; std::uint64_t time = 1'000'000'000;
    for (int block = 0; block < 200; ++block)
    {
        tap.push (samples.data(), 512, 1, true, true, position, time);
        position += 512; time += 10'666'666;
    }
    auto settled = tap.reading();
    for (const bool interrupted : { false, true })
    {
        NeedleMotionPair ui; ui.setVisible (true);
        auto reading = settled;
        if (interrupted) { tap.interrupt(); reading = tap.reading(); }
        const auto endpoint = reading.wallNanos + static_cast<std::uint64_t> (std::llround (reading.aheadSeconds * 1.0e9));
        ui.accept (reading, endpoint); ui.tick (1.0 / 60);
        for (int frame = 1; frame <= 30; ++frame)
        {
            ui.accept (reading); ui.tick (1.0 / 60);
            require (std::abs (ui.position (0) / settled.needles[0].value - (1 - response (frame / 60.0))) < 1.0e-7,
                     "missing callbacks and explicit stops return at the audio response speed");
        }
    }
    // Resume after silence must not resurrect the old deflection, even at a seek or stop edge.
    time += 2'000'000'000; position += 48000;
    samples.assign (512, 0.0f);
    tap.push (samples.data(), 512, 1, false, true, position, time);
    require (std::abs (tap.reading().needles[0].value) < 1.0e-9, "resume after a gap does not restore stale motion");
    samples[0] = std::numeric_limits<float>::quiet_NaN();
    tap.push (samples.data(), 512, 1);
    require (! tap.reading().feeding && std::isfinite (tap.reading().needles[0].value), "invalid input coasts without poisoning state");
    samples.assign (512, .1f); tap.push (samples.data(), 512, 1);
    require (tap.reading().feeding && std::isfinite (tap.level (0)), "the first finite block automatically resumes");
    NeedleMotionPair hidden; hidden.accept (tap.reading());
    require (! hidden.tick (1.0 / 60), "hidden callbacks do no frame work");
    hidden.setVisible (true); hidden.tick (1.0 / 60);
    for (int frame = 0; frame < 300; ++frame) hidden.tick (1.0 / 60);
    require (! hidden.active() && ! hidden.tick (1.0 / 60), "settled frames do no work");
    NeedleMotion invalid; invalid.snap (1); invalid.setTarget (NAN);
    require (! invalid.advance (0) && ! invalid.advance (-.1), "invalid durations do no work");
    invalid.advance (.3);
    require (std::abs (invalid.position() - .01) < 1.0e-10, "long UI frames integrate elapsed time without snapping");
}
void verifyReopening()
{
    LiveTap tap; tap.prepare (48000, 512); tap.setEnabled (true);
    std::vector<float> samples (512, .1f);
    std::uint64_t now = 1'000'000'000;
    for (int block = 0; block < 200; ++block)
    {
        tap.push (samples.data(), 512, 1, true, false, 0, now);
        now += 10'666'666;
    }
    for (const bool stopped : { false, true })
    {
        if (stopped) tap.interrupt();
        const auto reading = tap.reading();
        NeedleMotionPair ui; ui.setVisible (true);
        ui.accept (reading, now); ui.tick (1.0 / 60);
        for (int frame = 0; frame < 180; ++frame) ui.tick (1.0 / 60);
        require (std::abs (ui.position (0)) < 1.0e-10, "stopped needle finishes returning");
        ui.setVisible (false); ui.setVisible (true);
        ui.accept (reading, now + 3'000'000'000);
        require (std::abs (ui.position (0)) < 1.0e-10, "reopening after return cannot resurrect the last deflection");
        NeedleMotionPair reopened; reopened.setVisible (true);
        reopened.accept (reading, now + 3'000'000'000);
        require (std::abs (reopened.position (0)) < 1.0e-10, "a newly opened editor accounts for hidden elapsed time");
        NeedleMotionPair hidden; hidden.setVisible (true); hidden.accept (reading, now);
        hidden.setVisible (false);
        require (! hidden.tick (3), "hidden views do no animation work");
        hidden.setVisible (true); hidden.accept (reading, now + 3'000'000'000);
        require (std::abs (hidden.position (0)) < 1.0e-10, "hidden return uses the publication's time on first paint");
    }
}
void verifyBursts()
{
    for (const auto sizes : { std::vector<int> (32, 64), std::vector<int> { 63, 2048 },
                              std::vector<int> { 2048, 63 } })
    {
        LiveTap tap; tap.prepare (48000, 2048); tap.setEnabled (true);
        std::vector<float> samples (96000, .1f);
        tap.push (samples.data(), 96000, 1);
        const auto level = tap.reading().needles[0].value;
        NeedleMotionPair ui; ui.setVisible (true);
        std::uint64_t start = 1'000'000'000;
        std::int64_t position = 0;
        for (int burst = 0; burst < 24; ++burst)
        {
            int total = 0;
            for (int frames : sizes)
            {
                tap.push (samples.data(), frames, 1, true, true, position, start);
                position += frames; total += frames;
            }
            const double duration = static_cast<double> (total) / 48000;
            const auto reading = tap.reading();
            require (reading.aheadSeconds >= duration - 1.0e-7, "publication includes every block processed ahead of wall time");
            ui.accept (reading, start); ui.tick (1.0 / 60);
            for (double elapsed = 1.0 / 60; elapsed < duration; elapsed += 1.0 / 60)
            {
                ui.tick (1.0 / 60);
                require (std::abs (ui.position (0) - level) < 1.0e-8, "normal callback bursts do not make a steady needle pump");
            }
            start += static_cast<std::uint64_t> (std::llround (duration * 1.0e9));
        }
        NeedleMotionPair gap; gap.setVisible (true);
        gap.accept (tap.reading(), start + 300'000'000);
        require (std::abs (gap.position (0) / level - .01) < 1.0e-7, "a real gap returns after the processed audio horizon");
        const auto before = tap.reading();
        auto expected = before.needles[0];
        NeedleStep::forSeconds (.06 - before.aheadSeconds + 64.0 / 48000).apply (expected, 0.0);
        std::array<float, 64> zero {};
        tap.push (zero.data(), 64, 1, true, true, position, before.wallNanos + 60'000'000);
        require (std::abs (tap.reading().needles[0].value - expected.value) < .00005,
                 "resuming a burst without a stop notification coasts from the same endpoint as the UI");
    }
}
void verifyLargeBlockResumption()
{
    int cases = 0;
    for (int rate : { 44100, 48000, 88200, 96000, 176400, 192000 })
        for (int frames : { 4096, 8192 })
        {
            const auto duration = static_cast<std::uint64_t> (frames * 1.0e9 / rate);
            // The former jitter threshold was two block durations plus 20 ms from the
            // callback start, or one block duration plus 20 ms from the audio endpoint.
            for (const auto gap : { 0ull, 1'000'000ull, 100'000'000ull, 129'333'334ull,
                                   duration + 19'000'000ull, duration + 20'000'000ull,
                                   duration + 21'000'000ull })
                for (const float resumedInput : { 0.0f, .001f })
                    // No stop notification with continuous position, no position, or an explicit stop.
                    for (int stopKind : { 0, 1, 2 })
                {
                    const bool hasPosition = stopKind != 1;
                    LiveTap tap; tap.prepare (rate, frames); tap.setEnabled (true);
                    std::vector<float> samples (static_cast<size_t> (rate) * 4, .1f);
                    for (size_t index = 1; index < samples.size(); index += 2) samples[index] = -.05f;
                    tap.push (samples.data(), rate * 2, 2);
                    tap.push (samples.data(), frames, 2, true, hasPosition, 0, 1'000'000'000);
                    if (stopKind == 2) { tap.interrupt(); tap.interrupt(); }
                    const auto stopped = tap.reading();
                    const auto resumeTime = stopped.wallNanos + duration + gap;
                    NeedleMotionPair ui; ui.setVisible (true); ui.accept (stopped, resumeTime);
                    for (int ch = 0; ch < 2; ++ch)
                        require (std::abs (ui.position (ch) / stopped.needles[(size_t) ch].value
                                          - (1.0 - response (gap * 1.0e-9))) < 1.0e-7,
                                 "UI returns from the large block's endpoint with or without a stop notification");
                    samples.assign (128, resumedInput);
                    tap.push (samples.data(), 64, 2, true, hasPosition, frames, resumeTime);
                    const auto resumed = tap.reading();
                    require (resumed.feeding && resumed.coherent, "the first resumed block publishes automatically");
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto expected = stopped.needles[(size_t) ch];
                        NeedleStep::forSeconds (gap * 1.0e-9).apply (expected, 0.0);
                        NeedleStep::forSeconds (64.0 / rate).apply (expected, resumedInput * 1.5707963267948966);
                        require (std::abs (resumed.needles[(size_t) ch].value - expected.value) < .00005
                                 && std::abs (resumed.needles[(size_t) ch].velocity - expected.velocity) < .001,
                                 "callback gaps preserve both position and velocity on resumption without a jitter threshold");
                        if (resumedInput == 0.0f)
                        {
                            NeedleMotionPair silent; silent.setVisible (true);
                            silent.accept (stopped, resumeTime + static_cast<std::uint64_t> (64.0e9 / rate));
                            require (std::abs (resumed.needles[(size_t) ch].value - silent.position (ch)) < .00005,
                                     "silent resumption cannot resurrect the pre-stop deflection");
                        }
                    }
                    ++cases;
                }
        }
    std::cout << "Large-block resumption with/without stop and position notifications: " << cases << " boundary cases PASS\n";
}
}
int main()
{
    for (int rate : { 44100, 48000, 88200, 96000, 176400, 192000 }) verifyRate (rate);
    verifySineAndBlocks(); verifyStops(); verifyInvalidBlocks(); verifyReopening(); verifyBursts();
    verifyLargeBlockResumption();
    std::cout << "Legacy rectangle: half 150 ms, 99% 297 ms, overswing 0%; live follower adds about 14 ms\n";
}
