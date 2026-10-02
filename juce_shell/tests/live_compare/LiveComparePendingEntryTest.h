#pragma once

// A fresh requested entry is not an already-admitted source. Missing startup clocks may
// withhold output, but cannot consume its only admission before a complete proof exists.
void pendingInitialLoopWaitsForFirstUsableClock()
{
    using Mutate = void (*) (BlockClock&);
    const Mutate incomplete[] {
        [] (BlockClock& b) { b.clockValid = false; },
        [] (BlockClock& b) { b.projectValid = false; },
        [] (BlockClock& b) { b.playing = false; },
        [] (BlockClock& b) { b.frames = 0; },
        [] (BlockClock& b) { b.afterGap = true; }
    };
    for (auto change : incomplete)
    {
        Fixture fixture (24000, 4096, ClockMode::certifiedContent);
        fixture.enableLoop();
        fixture.ring->header.demand.store (1);
        Consumer waiting;
        fixture.step();
        auto startup = fixture.post;
        change (startup);
        float* out[] { fixture.scratch[0].data(), fixture.scratch[1].data() };
        for (auto& channel : fixture.scratch) channel.fill (-0.75f);
        require (waiting.process (*fixture.ring, pair, rate, startup, out, 2).verdict != Verdict::accepted,
                 "an incomplete first callback never outputs PRE");
        for (const auto& channel : fixture.scratch)
            for (const auto sample : channel)
                require (sample == -0.75f, "startup wait never copies PCM into the output scratch");
        for (int n = 0; n < 160; ++n)
        {
            fixture.step();
            require (fixture.read (waiting).verdict != Verdict::accepted,
                     "a later clock alone never authorises PRE before current proof adoption");
        }
        require (fixture.evidence.valid
                 && waiting.adoptInitialTiming (*fixture.ring, fixture.post, fixture.evidence),
                 "a complete fresh proof admits the same requested entry after a startup clock hole");
        fixture.step();
        require (fixture.read (waiting).verdict == Verdict::accepted,
                 "the first fully proven block ends the startup wait without another gesture");
        fixture.checkPcm();
        change (fixture.post);
        require (fixture.read (waiting).verdict != Verdict::accepted,
                 "the same fault after admission revokes the completed source");
        fixture.step();
        require (! waiting.adoptInitialTiming (*fixture.ring, fixture.post, fixture.evidence),
                 "a revoked completed proof cannot resurrect through pending-entry handling");
    }
    for (auto change : incomplete)
    {
        Fixture partial (24000, 0);
        partial.ring->header.demand.store (1);
        Consumer waiting;
        for (int n = 0; n < 7; ++n)
        {
            partial.step();
            require (partial.read (waiting).verdict != Verdict::accepted,
                     "a partial linear candidate is not a completed proof");
        }
        change (partial.post);
        require (partial.read (waiting).verdict != Verdict::accepted,
                 "a fault during partial preparation keeps POST");
        for (int n = 0; n < 8; ++n)
        {
            partial.step();
            require (partial.read (waiting).verdict != Verdict::accepted,
                     "fresh calibration never carries a pre-fault candidate streak");
        }
        partial.step();
        require (partial.read (waiting).verdict == Verdict::accepted,
                 "eight fresh linear joins may complete the same unadmitted entry");
        partial.checkPcm();
    }
}
