#pragma once
#include "ReaperFixture.h"
#include "LogicCycleClockFixture.h"
#include "../ProductTestClock.h"
#include "../../src/live_compare/PairedAnalysisClock.h"
#include <cmath>
#include <map>
#include <vector>

// Measured Logic callback clocks through the product PRE -> POST path. The PCM is synthetic and
// names what each side is playing from that side's OWN project and cycle (pass, position), so the
// expected PRE sample for every POST frame never comes from the render mapping under test.
// Three things are checked separately: no false reset in normal playback (including every cycle
// wrap and Logic's one-sample pre-roll step), no missed real break, and no wrong PRE/POST pair.
namespace logic_cycle_replay
{
using namespace reaper_fixture;
namespace clock = logic_cycle_clock;

struct Callback
{
    int role = 0;
    std::int64_t clock = 0, project = 0;
    double ppq = 0, bpm = 0, end = 0;
    bool playing = false, gap = false;
};

enum class Fault { none, seekForward, seekBack, stop, tempo, range, rate, projectOnly, ppqOnly, clockMoved };

inline double samplesPerBeat (const clock::Trial& t, double bpm) { return 60.0 * t.rate / bpm; }
inline double ppqOf (const clock::Trial& t, std::int64_t project, double bpm)
{
    const double steps = static_cast<double> (project) * 15360.0 / samplesPerBeat (t, bpm);
    return (steps >= 0 ? std::ceil (steps - 1.0e-9) : std::floor (steps + 1.0e-9)) / 15360.0;
}

// The host's own continuation after a transport change: Logic's measured rule at the new state.
inline Callback advance (const clock::Trial& t, Callback c)
{
    const auto spb = samplesPerBeat (t, c.bpm);
    c.clock += t.frames; c.project += t.frames; c.playing = true; c.gap = false;
    const auto end = std::llround (c.end * spb), length = std::llround ((c.end - t.start) * spb);
    if (t.cycle && c.project >= end) c.project -= length;
    c.ppq = ppqOf (t, c.project, c.bpm);
    return c;
}

inline std::vector<Callback> measured (const clock::Trial& t)
{
    std::vector<Callback> out;
    for (const auto& r : clock::decode (t))
        out.push_back ({ r.role, r.clock, r.project, static_cast<double> (r.ppqSteps) / 15360.0, t.bpm, t.end, r.playing, r.gap });
    return out;
}

// Transport changes act on the host (both sides, PRE keeping its PDC lead); later callbacks follow
// Logic's rule from the changed state. Observation faults change only POST's report of one block.
inline std::vector<Callback> hostWith (const clock::Trial& t, Fault fault, std::size_t at)
{
    auto host = measured (t);
    if (fault == Fault::none || fault == Fault::projectOnly || fault == Fault::ppqOnly || fault == Fault::clockMoved)
        return host;
    const auto stopAt = host.size() - 12;
    host.resize (at);
    Callback last[2] { host[at - 2], host[at - 1] };
    for (std::size_t i = at; i < stopAt; ++i)
    {
        auto& previous = last[i % 2];
        auto c = advance (t, previous);
        const auto spb = samplesPerBeat (t, c.bpm);
        if (i < at + 2)
        {
            if (fault == Fault::seekForward) c.project += 4999;
            if (fault == Fault::seekBack) c.project -= std::llround (spb);
            if (fault == Fault::rate) c.project += t.frames / 50;
            if (fault == Fault::tempo) c.bpm *= 1.01;
            if (fault == Fault::range) c.end -= 4.0;
            if (fault == Fault::stop) { c = previous; c.clock += t.frames; c.playing = false; }
        }
        if (fault == Fault::stop && i >= at + 2 && i < at + 8) { c = previous; c.clock += t.frames; c.playing = false; }
        if (fault == Fault::stop && i >= at + 8 && i < at + 10)
        { c.project = std::llround (2.0 * spb) + (c.role == 0 ? 4096 * (t.preLatency > t.postLatency) : 0); c.gap = true; }
        c.ppq = ppqOf (t, c.project, c.bpm);
        host.push_back (previous = c);
    }
    return host;
}

// What each callback plays: (pass, position). A cycle wrap continues inside its block at the cycle
// start of the next pass; any backward move starts a new pass on that side.
inline std::vector<std::vector<std::pair<long, long>>> contentOf (const clock::Trial& t, const std::vector<Callback>& host)
{
    std::vector<std::vector<std::pair<long, long>>> ids (host.size());
    long pass[2] {};
    for (std::size_t i = 0; i < host.size(); ++i)
    {
        const auto& c = host[i];
        const Callback* next = nullptr;
        for (auto j = i + 1; j < host.size() && next == nullptr; ++j)
            if (host[j].role == c.role && host[j].playing) next = &host[j];
        const auto spb = samplesPerBeat (t, c.bpm);
        const auto end = std::llround (c.end * spb), length = std::llround ((c.end - t.start) * spb);
        const bool wrap = t.cycle && next != nullptr && next->project == c.project + t.frames - length;
        for (int f = 0; f < t.frames; ++f)
        {
            const auto position = c.project + f;
            ids[i].push_back (wrap && position >= end ? std::make_pair (pass[c.role] + 1, long (position - length))
                                                      : std::make_pair (pass[c.role], long (position)));
        }
        if (c.playing && next != nullptr && next->project < c.project + t.frames) ++pass[c.role];
    }
    return ids;
}

inline float content (std::pair<long, long> id, int channel)
{ return Host::sample (id.first * 10000000L + id.second + 20000, channel); }

struct Result
{
    std::int64_t audible = 0, wrong = 0, falseResets = 0, preRunChanges = 0, deltaGaps = 0;
    std::int64_t audibleAfterFault = 0;
    bool faultSeen = false, faultHeld = true, faultPreRun = false;
    TimelineBreak faultCause = TimelineBreak::none;
};

inline Result replay (const clock::Trial& t, Fault fault = Fault::none, std::size_t at = 0)
{
    const auto host = hostWith (t, fault, at);
    const auto ids = contentOf (t, host);
    auto ring = std::make_unique<Ring>(); ring->initialise (pair, rate); ring->header.demand.store (1);
    PreFeeder feeder; TimingObserver observer; PostRenderer renderer; PostLevel level; PairedAnalysisClock analysis;
    renderer.prepare (t.frames, rate); level.configure (rate);
    NamedSelection selection; selection.select (true); NamedReentry reentry;
    hypha::tests::ProductTestClock time;
    std::vector<float> in0 (t.frames), in1 (t.frames), out0 (t.frames), out1 (t.frames);
    const float* in[] { in0.data(), in1.data() }; float* out[] { out0.data(), out1.data() };
    Result result;
    bool proven = false;
    std::uint64_t run = 0;
    // Normal playback ends at the first stopped callback of either side (PRE stops first with PDC).
    const auto firstStop = [&] { for (std::size_t i = 2; i < host.size(); ++i) if (! host[i].playing) return i; return host.size(); }();
    for (std::size_t i = 0; i < host.size(); ++i)
    {
        auto c = host[i];
        const bool faulty = c.role == 1 && i == at + 1;
        if (faulty && fault == Fault::projectOnly) c.project += 2;
        if (faulty && fault == Fault::ppqOnly) c.ppq += std::ceil ((2.0 + 4.0 * samplesPerBeat (t, c.bpm) / 15360.0) * 15360.0 / samplesPerBeat (t, c.bpm)) / 15360.0;
        if (c.role == 1 && i > at && fault == Fault::clockMoved) c.clock += 1;
        BlockClock b;
        b.clock = c.clock; b.project = c.project; b.frames = t.frames; b.playing = c.playing; b.afterGap = c.gap;
        b.clockValid = b.projectValid = true; b.outputPresentationValid = true; b.presentationSource = 2;
        b.outputPresentationSamples = c.role == 0 ? t.preLatency : t.postLatency;
        b.clockBasis = static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
        b.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::logicRender);
        b.loop = { t.cycle, true, c.ppq, t.start, c.end, c.bpm };
        b.wallNanos = time.nanos();
        if (c.role == 0)
        {
            for (int f = 0; f < t.frames; ++f) { in0[f] = content (ids[i][f], 0); in1[f] = content (ids[i][f], 1); }
            feeder.feed (*ring, b, in, 2);
            const auto now = ring->header.run.load();
            if (proven && now != run && i < at) ++result.preRunChanges;
            if (fault != Fault::none && i == at && now != run) result.faultPreRun = true;
            run = now;
            continue;
        }
        TimingSnapshot peer; require (readTiming (ring->header.timing, peer), "Logic replay metadata");
        const auto proof = observer.observe (peer, b, rate, ringCapacityFrames);
        const bool qualified = qualifiedAudioUnitPair (peer.block, b, proof);
        analysis.observeLocal (b, rate, true); analysis.observePair (b, proof, qualified);
        if (reentry.request (selection.command(), true, renderer.needsNewAdmission())) renderer.renewNamedTiming();
        renderer.adoptInitialTiming (*ring, b, proof);
        std::fill (out0.begin(), out0.end(), -0.25f); std::fill (out1.begin(), out1.end(), -0.25f);
        const auto report = renderer.render (*ring, pair, rate, b, out, 2, true, 1.0f, level, 1.0f, 1.0f, false);
        if (report.timelineChanged) reentry.lost (report.loss, true);
        const bool beforeFault = fault == Fault::none || i <= at;
        if (proven && beforeFault && i < firstStop)
        {
            if (report.timelineChanged || ! report.preAudible) ++result.falseResets;
            if (! analysis.paired.valid) ++result.deltaGaps;
        }
        if (faulty && fault != Fault::none)
        {
            result.faultSeen = report.timelineChanged;
            result.faultCause = report.loss;
            result.faultHeld = ! report.preAudible;
        }
        if (report.stableSource && report.preAudible)
        {
            proven = true;
            result.audible += t.frames;
            if (i > at + 1 && fault != Fault::none) result.audibleAfterFault += t.frames;
            for (int f = 0; f < t.frames; ++f)
                if (out0[f] != content (ids[i][f], 0) || out1[f] != content (ids[i][f], 1)) ++result.wrong;
        }
        time.advanceBlock (t.frames, rate);
    }
    return result;
}

inline TimelineBreak expectedCause (Fault fault)
{
    return fault == Fault::stop ? TimelineBreak::stopped
         : fault == Fault::tempo || fault == Fault::range ? TimelineBreak::loopEdited
         : fault == Fault::clockMoved ? TimelineBreak::clockMoved : TimelineBreak::projectMoved;
}

inline void verifyMeasuredClocks()
{
    int faults = 0;
    for (const auto& t : clock::trials)
    {
        const auto rows = clock::decode (t);
        require (static_cast<int> (rows.size()) == t.callbacks, "the fixture decodes every measured callback");
        // The measured clock property itself: every playing PPQ is within one grid step of project.
        for (const auto& r : rows)
            if (r.playing)
                require (std::abs (static_cast<double> (r.ppqSteps) / 15360.0 * samplesPerBeat (t, t.bpm) - static_cast<double> (r.project))
                             < samplesPerBeat (t, t.bpm) / 15360.0, "Logic PPQ lies within one grid step of project");
        // Host truth, independent of the product: the PRE callback with POST's render clock plays
        // the same (pass, position) as POST.
        const auto host = measured (t);
        const auto ids = contentOf (t, host);
        std::map<std::int64_t, std::pair<long, long>> preByClock;
        for (std::size_t i = 0; i < host.size(); ++i)
            if (host[i].role == 0 && host[i].playing)
                for (int f = 0; f < t.frames; ++f) preByClock[host[i].clock + f] = ids[i][f];
        std::int64_t paired = 0;
        for (std::size_t i = 0; i < host.size(); ++i)
            if (host[i].role == 1 && host[i].playing)
                for (int f = 0; f < t.frames; ++f)
                    if (auto it = preByClock.find (host[i].clock + f); it != preByClock.end())
                    { require (it->second == ids[i][f], "measured PRE and POST render the same content at one render clock"); ++paired; }
        require (paired > 1300000, "the measured trial covers its whole playback");

        const auto steady = replay (t);
        require (steady.wrong == 0, "no wrong PRE/POST pair on measured Logic clocks");
        require (steady.falseResets == 0 && steady.preRunChanges == 0 && steady.deltaGaps == 0,
                 "no false reset through cycle wraps and the pre-roll step");
        require (steady.audible > 1300000, "Logic plays PRE for the whole measured playback");

        const std::size_t at = rows.size() / 2 + (rows.size() / 2) % 2; // a PRE callback mid-playback
        for (auto fault : { Fault::seekForward, Fault::seekBack, Fault::stop, Fault::tempo, Fault::range,
                            Fault::rate, Fault::projectOnly, Fault::ppqOnly, Fault::clockMoved })
        {
            // With Cycle off, tempo, cycle range and PPQ are not part of continuity.
            if (! t.cycle && (fault == Fault::tempo || fault == Fault::range || fault == Fault::ppqOnly)) continue;
            const auto r = replay (t, fault, at);
            require (r.faultSeen && r.faultHeld && r.faultCause == expectedCause (fault), "every real break is reported with its cause");
            const bool hostFault = fault != Fault::projectOnly && fault != Fault::ppqOnly && fault != Fault::clockMoved;
            require (r.faultPreRun == hostFault, "PRE starts a new run exactly for a host transport break");
            require (r.wrong == 0, "no wrong PRE/POST pair around a real break");
            require (fault == Fault::clockMoved ? r.audibleAfterFault == 0 : r.audibleAfterFault > 0,
                     "fresh proof resumes PRE; a moved render clock never does");
            ++faults;
        }
    }
    std::printf ("Logic measured clocks: %zu trials, %d breaks, wrong pairs 0 PASS\n",
                 sizeof (clock::trials) / sizeof (clock::trials[0]), faults);
}
}
