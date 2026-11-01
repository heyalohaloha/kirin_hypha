#include "../src/HyphaVuNeedleMotion.h"
#include "../src/HyphaVuLiveTap.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
void require (bool value, const char* message)
{
    if (! value) { std::cerr << message << '\n'; std::exit (1); }
}
}

int main()
{
    using hypha::hybrid_vu::NeedleMotion;
    NeedleMotion sixty, fast;
    sixty.setTarget (1.0);
    fast.setTarget (1.0);
    double previous = 0.0;
    for (int i = 1; i <= 6; ++i)
    {
        sixty.advance (1.0 / 60.0);
        require (sixty.position() >= previous && sixty.position() <= 1.0,
                 "step must be monotone and not overshoot");
        previous = sixty.position();
    }
    require (sixty.position() >= 0.99, "step must reach 99% within 100 ms");
    for (int i = 0; i < 12; ++i) fast.advance (1.0 / 144.0);
    NeedleMotion other;
    other.setTarget (1.0);
    for (int i = 0; i < 5; ++i) other.advance (1.0 / 60.0);
    require (std::abs (fast.position() - other.position()) < 1.0e-10,
             "elapsed time must determine position");
    NeedleMotion ramp;
    double largestFrame = 0.0;
    double previousFrame = 0.0;
    bool foundIntermediate = false;
    for (int frame = 0; frame < 60; ++frame)
    {
        if (frame % 6 == 0) ramp.setTarget (0.1 * (frame / 6 + 1));
        ramp.advance (1.0 / 60.0);
        const auto position = ramp.position();
        largestFrame = std::max (largestFrame, std::abs (position - previousFrame));
        foundIntermediate |= position > previousFrame && position < ramp.target();
        previousFrame = position;
    }
    require (foundIntermediate && largestFrame < 0.05,
             "60 Hz ramp must interpolate the 100 ms publication steps");
    hypha::hybrid_vu::NeedleMotionPair pair;
    pair.setTarget (0, 1.0);
    int work = 0;
    for (int frame = 0; frame < 30; ++frame) work += pair.tick (1.0 / 60.0);
    require (work == 0, "hidden callbacks must do no frame work");
    pair.setVisible (true);
    for (int frame = 0; frame < 120; ++frame) work += pair.tick (1.0 / 60.0);
    const auto settledWork = work;
    for (int frame = 0; frame < 30; ++frame) work += pair.tick (1.0 / 60.0);
    require (settledWork > 0 && work == settledWork, "settled callbacks must do no frame work");
    pair.setTarget (1, 0.5);
    require (pair.active(), "a new publication must resume work");
    pair.setVisible (false);
    require (! pair.tick (1.0 / 60.0), "hiding must stop frame work");
    NeedleMotion missing;
    missing.snap (1.0);
    missing.setTarget (std::numeric_limits<double>::quiet_NaN());
    require (! missing.advance (0.0) && ! missing.advance (-0.1), "invalid dt must do no work");
    missing.advance (0.251);
    require (missing.position() == 0.0 && ! missing.active(), "missing value must rest at zero");
    NeedleMotion jump;
    jump.setTarget (0.7);
    jump.advance (0.251);
    require (jump.position() == 0.7 && ! jump.active(), "long pause must snap");
    for (int i = 0; i < 120 && sixty.active(); ++i) sixty.advance (1.0 / 60.0);
    require (! sixty.active() && sixty.position() == 1.0, "motion must settle");
    require (! sixty.advance (1.0 / 60.0), "settled motion must do no work");
    sixty.setTarget (0.0);
    previous = sixty.position();
    for (int frame = 0; frame < 6; ++frame)
    {
        sixty.advance (1.0 / 60.0);
        require (sixty.position() <= previous && sixty.position() >= 0.0,
                 "falling step must be monotone and not overshoot");
        previous = sixty.position();
    }
    require (sixty.position() <= 0.01, "falling step must reach 99% within 100 ms");
    sixty.setTarget (1.0);
    for (int i = 0; i < 120 && sixty.active(); ++i)
    {
        sixty.advance (1.0 / 60.0);
        require (sixty.position() >= 0.0 && sixty.position() <= 1.0,
                 "retarget must stay within the scale");
    }
    // 針の値が毎 frame（60 Hz）届くとき：300 ms で立ち上がる VU の積分（段差への応答）に 30 ms 以内で追い、
    // 行き過ぎず、段にならない。
    {
        NeedleMotion live;
        live.setLive (true);
        const double dt = 1.0 / 60.0, top = 0.6;
        double reachedHalfAt = -1.0, largestJump = 0.0, previousPosition = 0.0;
        for (int frame = 1; frame <= 60; ++frame)
        {
            const double t = frame * dt;
            const double fed = std::min (top, top * t / 0.3);
            live.setTarget (fed);
            live.advance (dt);
            const auto position = live.position();
            require (position <= fed + 1.0e-9 && position >= previousPosition - 1.0e-9,
                     "a live-fed needle never passes the integrated value and never reverses on a rising ramp");
            largestJump = std::max (largestJump, position - previousPosition);
            if (reachedHalfAt < 0.0 && position >= top * 0.5) reachedHalfAt = t;
            previousPosition = position;
        }
        std::cout << "live needle: half reached at " << reachedHalfAt * 1000.0 << " ms (fed value reached half at 150 ms)"
                  << " largest frame jump=" << largestJump << '\n';
        require (reachedHalfAt > 0.0 && reachedHalfAt - 0.15 <= 0.030, "a live-fed needle lags the integrated VU by at most 30 ms");
        require (largestJump < top / 18.0 * 1.5, "a live-fed needle moves smoothly, not in steps");
        require (live.position() >= top - 0.01, "a live-fed needle settles on the held value");
        live.setTarget (0.2);
        for (int frame = 0; frame < 30; ++frame)
        {
            live.advance (dt);
            require (live.position() >= 0.2 - 1.0e-9, "a live-fed needle does not overshoot a falling target");
        }
    }
    // 針の値を audio thread で作って block ごとに公開する tap：公開の間隔は 1 block、窓はちょうど 300 ms、
    // 校正は Rust の VU と同じ（正弦波の peak が dBFS になる）。
    {
        using hypha::hybrid_vu::LiveTap;
        LiveTap tap;
        tap.prepare (48000.0, 512);
        std::vector<float> block (512 * 2, 0.0f);
        require (! std::isfinite (tap.read()[0]) && tap.publications() == 0, "an unprepared or disabled tap publishes nothing");
        tap.push (block.data(), 512, 2);
        require (tap.publications() == 0 && ! std::isfinite (tap.read()[0]), "a disabled tap does no work");
        tap.setEnabled (true);
        auto publications = tap.publications();
        for (int n = 0; n < 40; ++n)   // 426 ms of silence covers the window
        {
            tap.push (block.data(), 512, 2);
            require (tap.publications() == ++publications, "every pushed block publishes once");
        }
        require (! std::isfinite (tap.read()[0]), "a silent window reads as no level, like the Rust meter");
        double phase = 0.0;
        const auto sine = [&] (float peak)
        {
            for (int f = 0; f < 512; ++f)
            {
                const auto v = static_cast<float> (peak * std::sin (phase));
                block[(std::size_t) f * 2] = v; block[(std::size_t) f * 2 + 1] = 0.5f * v;
                phase += 2.0 * 3.14159265358979323846 * 1000.0 / 48000.0;
            }
        };
        int pushed = 0; double atHalf = 0.0, atFull = 0.0, previous = -300.0; bool monotone = true;
        while (pushed < 14400 + 512)
        {
            sine (0.1f); tap.push (block.data(), 512, 2); pushed += 512;
            const auto level = tap.read()[0];
            if (std::isfinite (level)) { monotone = monotone && level >= previous - 1.0e-9; previous = level; }
            if (pushed == 7168) atHalf = level;
            if (pushed == 14336) atFull = level;
        }
        std::cout << "live tap: at 7168 frames=" << atHalf << " dBFS, at 14336 frames=" << atFull << " dBFS, final=" << tap.read()[0] << '\n';
        require (std::abs (tap.read()[0] - (-20.0)) < 0.05, "a -20 dBFS peak sine reads -20 dBFS after a full window");
        require (std::abs (tap.read()[1] - (-26.02)) < 0.05, "the second channel is integrated separately");
        require (atHalf < -25.0 && atHalf > -27.5 && atFull > -20.3, "the window is a 300 ms average that rises for exactly 300 ms");
        require (monotone, "a rising step integrates without steps backwards");
        tap.setEnabled (false);
        tap.push (block.data(), 512, 2);
        require (! std::isfinite (tap.read()[0]), "disabling clears the published level");
        tap.setEnabled (true);
        for (int n = 0; n < 10; ++n) { sine (0.1f); tap.push (block.data(), 512, 2); }
        require (! std::isfinite (tap.read()[0]), "after re-enabling a whole window must pass before a level is published");
        // 非有限の入力が 1 つでも混ざると、和と ring が食い違ったまま残らない：窓・和・公開を一緒に失効し、
        // 有限な連続 300 ms の後に正しい値へ戻る。
        for (int n = 0; n < 40; ++n) { sine (0.1f); tap.push (block.data(), 512, 2); }
        require (std::abs (tap.read()[0] - (-20.0)) < 0.05, "a settled sine reads its level before the fault");
        sine (0.1f); block[7] = std::numeric_limits<float>::quiet_NaN(); tap.push (block.data(), 512, 2);
        require (! std::isfinite (tap.read()[0]), "a non-finite sample invalidates the published level");
        for (int n = 0; n < 28; ++n) { sine (0.1f); tap.push (block.data(), 512, 2); }   // 298.7 ms
        require (! std::isfinite (tap.read()[0]), "no level until a whole finite window has passed");
        sine (0.1f); tap.push (block.data(), 512, 2);
        require (std::abs (tap.read()[0] - (-20.0)) < 0.05, "a whole finite window restores the exact level after a NaN");
        // 計測の不連続（seek・停止⇄再生・callback の空白）で窓を失効し、新しい連続 300 ms まで公開しない。
        LiveTap continuous; continuous.prepare (48000.0, 512); continuous.setEnabled (true);
        std::int64_t position = 96000; std::uint64_t nanos = 1'000'000'000ull;
        const auto feed = [&] (bool playing, bool hasPosition)
        {
            sine (0.1f);
            continuous.push (block.data(), 512, 2, playing, hasPosition, position, nanos);
            position += 512; nanos += 512ull * 1'000'000'000ull / 48000ull;
        };
        for (int n = 0; n < 40; ++n) feed (true, true);
        require (std::abs (continuous.read()[0] - (-20.0)) < 0.05, "continuous playback reads its level");
        position += 48000; feed (true, true);
        require (! std::isfinite (continuous.read()[0]), "a seek invalidates the window");
        for (int n = 0; n < 29; ++n) feed (true, true);
        require (std::abs (continuous.read()[0] - (-20.0)) < 0.05, "300 ms after the seek the level is back");
        feed (false, true);
        require (! std::isfinite (continuous.read()[0]), "a stop invalidates the window");
        for (int n = 0; n < 30; ++n) feed (false, true);
        require (std::isfinite (continuous.read()[0]), "steady callbacks while stopped (monitoring) integrate again");
        feed (true, true);
        require (! std::isfinite (continuous.read()[0]), "starting playback after a stop invalidates the window");
        for (int n = 0; n < 30; ++n) feed (true, true);
        nanos += 500'000'000ull; feed (true, false);
        require (! std::isfinite (continuous.read()[0]), "a gap in callbacks (bypass, hidden) invalidates the window");
        for (int n = 0; n < 30; ++n) feed (true, false);
        require (std::isfinite (continuous.read()[0]), "without a position, steady callbacks are continuous");
        // 実の processor に近い道：(a) 可変長の callback（2048 と 64 が交互、時刻はその長さぶん進む）は連続、
        // (b) PRE（時刻が常に 0）は計測しない callback の「途切れ」の印と再生の切替で見つける、
        // (c) 停止中（tap が呼ばれない間）の off→on は次の push で失効する。
        LiveTap realistic; realistic.prepare (48000.0, 2048); realistic.setEnabled (true);
        std::int64_t at = 48000; std::uint64_t clockNanos = 5'000'000'000ull;
        const auto pushVariable = [&] (int frames, bool playing, bool timed)
        {
            std::vector<float> wide (static_cast<std::size_t> (frames) * 2, 0.0f);
            for (int f = 0; f < frames; ++f)
            {
                const auto v = static_cast<float> (0.1 * std::sin (phase));
                wide[(std::size_t) f * 2] = v; wide[(std::size_t) f * 2 + 1] = v;
                phase += 2.0 * 3.14159265358979323846 * 1000.0 / 48000.0;
            }
            realistic.push (wide.data(), frames, 2, playing, true, at, timed ? clockNanos : 0);
            at += frames; clockNanos += static_cast<std::uint64_t> (frames) * 1'000'000'000ull / 48000ull;
        };
        for (int n = 0; n < 24; ++n) { pushVariable (2048, true, true); pushVariable (64, true, true); }
        require (std::abs (realistic.read()[0] - (-20.0)) < 0.05, "alternating 2048/64 callbacks are continuous");
        // 1 周期の 2048 を 64 × 32 で続けて呼び、次の周期まで待つ host（時刻は束の頭だけ進む）も連続。
        for (int period = 0; period < 12; ++period)
        {
            const auto periodStart = clockNanos;
            for (int n = 0; n < 32; ++n) { const auto saved = clockNanos; pushVariable (64, true, true); clockNanos = saved; }
            clockNanos = periodStart + 2048ull * 1'000'000'000ull / 48000ull;
        }
        require (std::abs (realistic.read()[0] - (-20.0)) < 0.05, "bursts of short callbacks within one period are continuous");
        clockNanos += 400'000'000ull; pushVariable (64, true, true);
        require (! std::isfinite (realistic.read()[0]), "a real gap after short callbacks still ends the window");
        for (int n = 0; n < 7; ++n) { pushVariable (2048, true, true); pushVariable (64, true, true); }
        require (std::isfinite (realistic.read()[0]), "and the level is back after a whole new window");
        LiveTap pre; pre.prepare (48000.0, 512); pre.setEnabled (true);
        LiveTap* target = &pre;
        const auto pushPre = [&] (int frames, bool playing)
        {
            std::vector<float> wide (static_cast<std::size_t> (frames) * 2, 0.0f);
            for (int f = 0; f < frames; ++f)
            {
                const auto v = static_cast<float> (0.1 * std::sin (phase));
                wide[(std::size_t) f * 2] = v; wide[(std::size_t) f * 2 + 1] = v;
                phase += 2.0 * 3.14159265358979323846 * 1000.0 / 48000.0;
            }
            target->push (wide.data(), frames, 2, playing, true, at, 0);
            at += frames;
        };
        for (int n = 0; n < 40; ++n) pushPre (512, true);
        require (std::isfinite (pre.read()[0]), "PRE without a wall clock integrates while playing");
        for (int n = 0; n < 5; ++n) pre.interrupt();   // stopped: callbacks that do not measure
        require (! std::isfinite (pre.read()[0]), "a callback that does not measure ends the window at once");
        for (int n = 0; n < 28; ++n) pushPre (512, true);  // resumes at the position where it ended (298.7 ms)
        require (! std::isfinite (pre.read()[0]), "resuming at the same position still needs a whole new window");
        pushPre (512, true);
        require (std::isfinite (pre.read()[0]), "300 ms after the resume the level is back");
        pre.setEnabled (false); pre.setEnabled (true);     // off -> on while stopped: no push in between
        pushPre (512, true);
        require (! std::isfinite (pre.read()[0]), "enabling again invalidates the window even at a continuing position");
        for (int n = 0; n < 29; ++n) pushPre (512, true);
        require (std::isfinite (pre.read()[0]), "and publishes again after a whole window");
        std::vector<float> mono (512, 0.1f);
        LiveTap single; single.prepare (48000.0, 512); single.setEnabled (true);
        for (int n = 0; n < 40; ++n) single.push (mono.data(), 512, 1);
        require (std::isfinite (single.read()[0]) && ! std::isfinite (single.read()[1]), "a mono input leaves the second needle without a value");
    }
    return 0;
}
