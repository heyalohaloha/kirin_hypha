#pragma once
#include "ReaperFixture.h"

// The real MATCH worker copies the proven ring/history windows. No synthetic gain receipt,
// guessed offset or loop-folded audio is substituted for the production measurement.
inline void verifyRuntimeLoopMatch()
{
    using namespace reaper_fixture;
    for (int channels : { 1, 2 })
    {
        Host host; host.channels = channels; host.runtimeBound = true;
        host.length = 384000; host.runtimeDelay = 4096;
        for (int n = 0; n < 8; ++n) host.publish();
        for (int n = 0; n < 1700; ++n)
        {
            host.consume();
            if (n != 700 && n != 1600) continue;
            const auto measured = computeMatch (*host.ring, host.renderer, rate);
            require (measured.ok() && std::abs (measured.measuredDb + 6.0206) < 0.1,
                     "REAPER MATCH copies the correct occurrence before and after the split wrap");
            const auto plan = planMatch (measured, 0);
            require (!plan.needsApproval, "attenuation MATCH needs no ceiling exception");
            require (std::abs (plan.preGainDb + 6.0206) < 0.1, "REAPER MATCH gain plan");
        }
    }
    for (int delay : { 0, 4096 })
    {
        auto ring = std::make_unique<Ring>(); ring->initialise (pair, rate); ring->header.demand.store (1);
        PreFeeder feeder; TimingObserver observer; PostRenderer renderer; PostLevel level;
        renderer.prepare (frames, rate); level.configure (rate);
        std::array<std::array<float, frames>, 2> input {}, output {};
        const float* in[] { input[0].data(), input[1].data() };
        float* out[] { output[0].data(), output[1].data() };
        auto clock = [&] (std::int64_t render, bool pre)
        {
            BlockClock b; b.clock = render; b.project = positiveModulo (render, 384123);
            b.clockValid = b.projectValid = b.playing = true; b.frames = frames;
            b.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
            b.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::logicRender);
            b.presentationSource = 2; b.outputPresentationValid = true;
            b.outputPresentationSamples = static_cast<std::uint32_t> (685 + (pre ? delay : 0));
            b.loop = { true, true, b.project / 48000.0, 0, 384123.0 / 48000.0, 120 };
            return b;
        };
        auto publish = [&] (std::int64_t render)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < frames; ++i)
                input[c][i] = Host::sample (render + i, c);
            feeder.feed (*ring, clock (render, true), in, 2);
        };
        for (int n = 0; n < delay; n += frames) publish (n);
        float gain = 1;
        for (int n = 0; n < 1700; ++n)
        {
            const auto render = static_cast<std::int64_t> (n) * frames;
            publish (render + delay); auto post = clock (render, false);
            TimingSnapshot peer; require (readTiming (ring->header.timing, peer), "MATCH metadata");
            const auto evidence = observer.observe (peer, post, rate, ringCapacityFrames);
            renderer.adoptInitialTiming (*ring, post, evidence);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < frames; ++i)
                output[c][i] = Host::sample (render + i, c) * 0.5f;
            const auto report = renderer.render (*ring, pair, rate, post, out, 2, true, gain, level, 1, 1);
            if (report.stableSource && report.preAudible && report.gainSettled)
                for (int c = 0; c < 2; ++c) for (int i = 0; i < frames; ++i)
                    require (std::abs (output[c][i] - Host::sample (render + i, c) * gain) < 1.0e-6f,
                             "Logic MATCH gain applies only to correctly aligned audition samples");
            if (n != 700 && n != 1600) continue;
            const auto measured = computeMatch (*ring, renderer, rate);
            require (measured.ok() && std::abs (measured.measuredDb + 6.0206) < 0.1,
                     "Logic MATCH is measured across cycles, including zero latency");
            gain = static_cast<float> (std::pow (10.0, planMatch (measured, 0).preGainDb / 20.0));
        }
    }
    std::puts ("Runtime loop MATCH: correct windows, gain and zero latency PASS");
}
