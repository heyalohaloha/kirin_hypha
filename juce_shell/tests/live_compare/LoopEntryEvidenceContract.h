#pragma once
#include "LiveCompareLoopOracle.h"
#include <array>
#include <cstdio>
#include <cstdlib>

// NON-SHIPPING feasibility checks. Observe BEFORE a hypothetical LISTEN click. Merely keeping
// more clocks must not be confused with acquiring an unambiguous correspondence. These tests
// do not provide a host certificate, start an audition, or inject a K into the product Consumer.
namespace loop_entry_evidence
{
using namespace loop_feasibility;

inline void check (bool value, const char* why)
{
    if (! value) { std::fprintf (stderr, "early LOOP evidence: %s\n", why); std::abort(); }
}

// A fixed-size, metadata-only candidate. Its input is a pair of coherent fixture observations,
// NOT a claim that the most recently arriving real PRE/POST callbacks share an instant.
// Linear project origins can establish K; repeated positions cannot create a new one.
struct RetainedCandidate
{
    std::int64_t k = 0, candidate = 0;
    unsigned streak = 0;
    bool valid = false, previous = false;
    BlockClock pre {}, post {};

    void observe (const Observation& now) noexcept
    {
        const auto& a = now.pre;
        const auto& b = now.post;
        const bool usable = a.playing && b.playing && a.clockValid && b.clockValid
            && a.projectValid && b.projectValid && a.frames > 0 && b.frames > 0
            && ! a.afterGap && ! b.afterGap;
        const bool clocksContinue = ! previous
            || (a.clock == pre.clock + pre.frames && b.clock == post.clock + post.frames);
        if (! usable || ! clocksContinue)
        {
            valid = false; streak = 0; previous = false;
            return;
        }
        if (! a.loop.active && ! b.loop.active && previous)
        {
            const bool linear = a.project == pre.project + pre.frames
                && b.project == post.project + post.frames;
            const auto next = (b.clock - b.project) - (a.clock - a.project);
            if (! linear || (valid && next != k)) { valid = false; streak = 0; }
            if (linear)
            {
                streak = streak != 0 && next == candidate ? streak + 1 : 1;
                candidate = next;
                if (streak >= 8) { k = next; valid = true; }
            }
        }
        // This prototype tests acquisition only. Keeping a value is NOT permission to play:
        // the real Consumer must still check loop movement, PRE run, range, and atomic reads.
        pre = a; post = b; previous = true;
    }
};

inline void retainedLinearPositiveControl()
{
    unsigned cases = 0;
    for (auto clock : { Clock::vst3, Clock::renderCounter })
    for (int delay : { 0, 4096, 23999, 24000, 24001 })
    {
        Oracle host (24000, delay, clock, Position::content, false, false);
        RetainedCandidate witness;
        Decision decision;
        // Ordinary playback precedes the click. The fixture's ring/PCM is an oracle only;
        // RetainedCandidate itself neither sees nor stores PCM.
        for (int n = 0; n < 260; ++n)
        {
            witness.observe (host.observations (128));
            decision = host.step (128);
        }
        check (witness.valid && decision.kValid && witness.k == decision.k,
               "prior linear observations must establish the same exact offset");
        const auto before = witness.k;
        host.enableLoopAtCurrentPosition();
        for (int n = 0; n < 1000; ++n)
        {
            witness.observe (host.observations (128));
            decision = host.step (128);
            check (witness.valid && witness.k == before && decision.k == before,
                   "a later click in the loop must not discard the earlier offset");
        }
        check (host.tally().wrong == 0, "retained positive control must never pick another lap");
        ++cases;
    }
    std::printf ("early linear evidence: %u positive controls; metadata bytes=%zu\n", cases,
                 sizeof (RetainedCandidate));
}

inline void loopOnlyHistoryRemainsAmbiguous()
{
    unsigned identical = 0;
    for (int length : { 6000, 24000, 192000 })
    {
        Oracle direct (length, 0, Clock::renderCounter, Position::content);
        Oracle delayed (length, length, Clock::renderCounter, Position::content);
        RetainedCandidate first, second;
        for (int n = 0; n < 1500; ++n)
        {
            const auto a = direct.observations (128), b = delayed.observations (128);
            check (sameObservation (a, b), "full observed history must be identical in the counterexample");
            first.observe (a); second.observe (b);
            direct.step (128); delayed.step (128);
            check (direct.expectedDiffers (delayed, 128), "independent delay line must distinguish the laps");
            // The first 1000 callbacks are BEFORE the hypothetical comparison request.
            // Another 500 callbacks do not add the absent absolute loop occurrence.
            check (! first.valid && ! second.valid, "observation duration cannot manufacture a proof");
            ++identical;
        }
    }
    std::printf ("early loop-only evidence: %u identical metadata / differing PCM blocks; unresolved\n",
                 identical);
}

inline void lifecycleInvalidatesRetainedEvidence()
{
    using Mutation = void (*) (Observation&);
    const Mutation mutations[] {
        [] (Observation& o) { o.pre.afterGap = true; },
        [] (Observation& o) { o.post.afterGap = true; },
        [] (Observation& o) { o.pre.playing = false; },
        [] (Observation& o) { o.post.playing = false; },
        [] (Observation& o) { o.pre.clockValid = false; },
        [] (Observation& o) { o.post.projectValid = false; },
        [] (Observation& o) { o.pre.clock += 24000; },
        [] (Observation& o) { o.post.clock -= 24000; }
    };
    for (auto mutate : mutations)
    {
        Oracle host (24000, 4096, Clock::renderCounter, Position::content, false, false);
        RetainedCandidate witness;
        for (int n = 0; n < 80; ++n) { witness.observe (host.observations (128)); host.step (128); }
        check (witness.valid, "fault test requires a valid preceding offset");
        host.enableLoopAtCurrentPosition();
        auto faulty = host.observations (128);
        mutate (faulty); witness.observe (faulty); host.step (128);
        check (! witness.valid, "a retained offset must expire with its clock observations");
        for (int n = 0; n < 400; ++n)
        {
            witness.observe (host.observations (128)); host.step (128);
            check (! witness.valid, "loop metadata must not resurrect the expired offset");
        }
    }
    RetainedCandidate restored;
    Oracle alreadyLooping (24000, 4096, Clock::renderCounter, Position::content);
    for (int n = 0; n < 1000; ++n)
    { restored.observe (alreadyLooping.observations (128)); alreadyLooping.step (128); }
    check (! restored.valid, "late plugin creation cannot inherit unobserved early clocks");
    std::puts ("early evidence lifecycle: 8 invalidations and late creation PASS");
}

// Necessary information, not a shipping shortcut: under the model's expressly shared CONTENT
// clock, a whole-lap delay changes the continuous coordinate. The render-counter model lacks
// that distinction. A format label or two equal numbers is not a certificate of this semantic.
inline void commonClockPositiveAndNegativeControls()
{
    unsigned distinguished = 0;
    Oracle direct (24000, 0, Clock::vst3, Position::content);
    Oracle delayed (24000, 24000, Clock::vst3, Position::content);
    for (int n = 0; n < 1500; ++n)
    {
        const auto a = direct.observations (128), b = delayed.observations (128);
        check (a.pre.project == b.pre.project && a.post.project == b.post.project,
               "positions alone should still look identical");
        check (a.post.clock - b.post.clock == 24000,
               "a genuinely shared content clock must expose the whole-lap delay");
        // Check the semantic against the independent physical delay, not a guessed K.
        direct.step (128); delayed.step (128);
        check (a.post.clock - 777000 == direct.expectedStart()
            && b.post.clock - 777000 == delayed.expectedStart(), "content clock disagrees with PCM oracle");
        auto alias = b;
        alias.post.clock += 24000;
        check (sameObservation (a, alias), "a false origin can masquerade as a shared clock");
        check (direct.expectedDiffers (delayed, 128), "clock alias must not hide different correct audio");
        ++distinguished;
    }
    std::printf ("shared-content-clock model: %u distinguishable blocks; false-origin negative control PASS\n",
                 distinguished);
}

inline void sharedEngineCounterDoesNotIdentifyContentLap()
{
    // The two measured AddClock offsets fit a common engine counter, not a compensated
    // content clock. This is a mathematical model counterexample, NOT an unperformed AAX
    // long-delay host test. No amount of equal, continuous timestamps disambiguates the lap.
    unsigned identical = 0;
    for (int length : { 6000, 24000, 131072 })
    {
        Oracle direct (length, 0, Clock::engineCounter, Position::content);
        Oracle delayed (length, length, Clock::engineCounter, Position::content);
        for (int n = 0; n < 1500; ++n)
        {
            const auto a = direct.observations (128), b = delayed.observations (128);
            check (a.pre.clock == a.post.clock && b.pre.clock == b.post.clock,
                   "common engine origins remain equal at the current callback");
            check (sameObservation (a, b), "engine/native/loop metadata must be identical for both delays");
            direct.step (128); delayed.step (128);
            check (direct.expectedDiffers (delayed, 128), "equal engine clocks must not erase a whole-lap PCM difference");
            ++identical;
        }
    }
    Oracle linear (24000, 4096, Clock::engineCounter, Position::content, false, false);
    Decision decision;
    for (int n = 0; n < 1000; ++n) decision = linear.step (128);
    check (decision.kValid && decision.k == 4096 && linear.tally().wrong == 0,
           "the engine counter still needs an independently acquired content offset");
    std::printf ("shared engine clock: %u identical metadata / differing PCM blocks; linear K=4096 PASS\n",
                 identical);
}

inline void verify()
{
    static_assert (sizeof (RetainedCandidate) <= 256, "prototype metadata must remain bounded");
    retainedLinearPositiveControl();
    loopOnlyHistoryRemainsAmbiguous();
    lifecycleInvalidatesRetainedEvidence();
    commonClockPositiveAndNegativeControls();
    sharedEngineCounterDoesNotIdentifyContentLap();
    std::puts ("early evidence feasibility controls PASS; fresh LOOP admission/product NOT implemented");
}
}
