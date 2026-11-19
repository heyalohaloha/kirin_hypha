#pragma once
#include "../../src/live_compare/LiveComparePreAlign.h"
#include <cstdlib>
#include <deque>
#include <vector>

// INV-LC28 under a transport storm. The Audio Thread's alignment (RendererAlignment) and the owner
// of PRE's alignment state (PreAlignState) are the product's; the clocks, PRE's run, the host and the
// content estimates are a model with a known truth: PRE is `early` frames early at clock difference K.
// Two listeners hear the same events: in a Blind trial they hold opposite assignments.
namespace live_compare_storm_test
{
using namespace hypha::live_compare;
constexpr std::int64_t rate = 48000, agreement = 2, unknown = std::numeric_limits<std::int64_t>::max();
enum class Mode { listen, blind, candidate };

struct Listener
{
    RendererAlignment alignment;
    PreAlignState owner;
    float weight = 0.0f;
    bool gateOpen = false, pathOpen = false, playsPre = false, slotPre = false, rereading = false;
    std::int64_t address = 0, waited = 0, total = 0;
    struct Estimate { AlignValue value; std::uint64_t revision; std::int64_t k; int due; };
    std::deque<Estimate> estimates;
    std::vector<Estimate> validated;
};

struct World
{
    explicit World (std::uint32_t seedIn) : seed (seedIn), state (seedIn * 2654435761u + 1u) {}
    std::uint32_t seed, state;
    void check (bool ok, const char* message) const
    {
        if (! ok) std::printf ("alignment transport storm: seed %u, block %ld, mode %d, K %lld, early %lld\n", seed, blocks,
                               static_cast<int> (mode), static_cast<long long> (k), static_cast<long long> (early));
        require (ok, message);
    }
    std::uint32_t next() noexcept { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    int pick (int count) noexcept { return static_cast<int> (next() % static_cast<std::uint32_t> (count)); }

    Mode mode = Mode::listen;
    AlignContext context { 3, 5, 7, 9 };
    PreAlignHost host { false, 11, 1, 1 };
    std::int64_t k = 512, early = 0, base = 0, run = 0, clock = 0;
    int frames = 256, calibrating = 0, stopped = 0, tickEvery = 2, sinceTick = 0, burst = 0;
    bool selectedPre = true, content = true, raceNext = false, unseenK = false, wasValid = false, sameStart = false;
    bool takeoverUnheard = false; // the host took the delay over and no block has been proven at that K yet
    bool allowClockChangesBeforeAnswer = false;
    bool confirmedCandidate = false; std::uint64_t confirmedConditions = 0;
    Listener listeners[2];
    long blocks = 0, substituted = 0, notices = 0, events = 0, staleEstimates = 0, fallbacks = 0, blindBlocks = 0, races = 0, burstBlocks = 0;
    std::int64_t longestWait = 0;

    // The service sees a budget waiting at one tick and answers it, once spent, at another: two ticks
    // of lag for each budget, however many clock mappings (K changes) and seeks the wait went through.
    // The one documented addition: a kept correction whose position cannot be read yet is read again
    // for at most half a second, once, before the budget is answered.
    std::int64_t tickAllowance() const noexcept { return static_cast<std::int64_t> (2 * tickEvery + 2) * 1024; }
    std::int64_t limit (const Listener& l) const noexcept { return rate + tickAllowance() + (l.rereading ? rate / 2 + static_cast<std::int64_t> (tickEvery) * 1024 : 0); }
    bool preSelected (int listener) const noexcept { return mode == Mode::blind && listener == 1 ? ! selectedPre : selectedPre; }

    // A trial's two assignments are one history up to its start. After an earlier trial the model's
    // two listeners may differ (each kept the address it applied): such a trial is not compared.
    bool alike() const noexcept
    {
        const auto& x = listeners[0]; const auto& y = listeners[1];
        const auto same = [] (const auto& a, const auto& b) noexcept
        {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (a[i].value != b[i].value || a[i].revision != b[i].revision || a[i].k != b[i].k || a[i].due != b[i].due) return false;
            return true;
        };
        return x.alignment.value() == y.alignment.value() && x.alignment.requestFor (context) == y.alignment.requestFor (context)
            && x.alignment.readiness.generation() == y.alignment.readiness.generation()
            && x.alignment.readiness.pending() == y.alignment.readiness.pending()
            && x.owner.keptValue() == y.owner.keptValue() && x.owner.keptK() == y.owner.keptK()
            && same (x.estimates, y.estimates) && same (x.validated, y.validated);
    }
    void newSession() noexcept { for (auto& l : listeners) { l.alignment.newSession(); l.weight = 0.0f; l.waited = l.total = 0; l.rereading = false; } }
    void restartRun (int calibration) noexcept { run = 0; calibrating = calibration; }
    void moveK (std::int64_t by) noexcept { k += by; unseenK = true; }

    PreAlignInput input (Listener& l, PreAlignEvent event) const noexcept
    {
        PreAlignInput in;
        in.event = event; in.context = context; in.host = host; in.active = true; in.agreement = agreement;
        in.revision = l.alignment.readiness.generation(); in.k = l.alignment.readiness.clockK();
        in.clock = &l.alignment.readiness;
        return in;
    }

    // One block for one listener, in the renderer's order (LiveCompareSession.h).
    void render (Listener& l, int index)
    {
        const bool playing = stopped == 0, kValid = playing && calibrating == 0, blind = mode == Mode::blind, pre = preSelected (index);
        AlignPlayback playback; playback.context = context; playback.verify = true;
        // As in the renderer, the run's reach is known from the block after the one that calibrates.
        const auto update = l.alignment.update (playback, blind, pre, l.weight, kValid && wasValid ? run : unknown, rate, playing ? frames : 0);
        l.address = l.alignment.value().frames;
        const bool accepted = kValid && l.address <= run;
        l.alignment.readiness.observe (true, playing, kValid, k);
        l.alignment.observed (accepted, kValid, update);
        l.gateOpen = l.playsPre = false;
        const bool waits = l.alignment.awaits (update);
        l.pathOpen = kValid && ! waits;
        l.slotPre = accepted && ! waits; // what the source that is PRE would sound: PRE, else POST
        if (! accepted || waits) l.weight = 0.0f;
        else
        {
            if (update.current) l.alignment.confirmApplied (update.revision);
            const float target = pre && ! update.fading ? 1.0f : 0.0f;
            if ((blind || target > 0.0f || ! pre) && ! update.deferred) l.alignment.served();
            l.gateOpen = ! update.fading; l.playsPre = target > 0.0f; l.weight = target;
        }
        // The truth. PRE never sounds at a correction the present K does not need.
        if (l.playsPre)
            check (l.address == 0 || std::llabs (l.address - early) <= agreement, "PRE never plays a correction against another K");
        // The wait: clock-proven playback of PRE's side without PRE (total: since PRE last played at its address).
        const auto told = l.owner.notice (l.alignment.unservedCorrection());
        if (kValid && (pre || blind) && ! l.gateOpen) { l.waited += frames; l.total += frames; if (index == 0) ++substituted; }
        else if (l.gateOpen) l.waited = 0;
        if (l.gateOpen && ! l.alignment.unservedCorrection()) { l.total = 0; l.rereading = false; } // the budget is whole again
        if (l.waited > longestWait) longestWait = l.waited;
        check (l.waited <= limit (l), "no single wait for PRE's address outlasts the one-second budget");
        check (l.total <= limit (l), "waits never add up beyond the budget before PRE plays at its address");
        if (index == 0 && (told.unmeasured || told.notRemeasured)) ++notices;
    }

    PreAlignOutput send (Listener& l, const PreAlignInput& in)
    {
        const auto out = l.owner.transition (in);
        if (! out.request) return out;
        // A publication for a mapping the Audio Thread has already left is refused there by its revision.
        if (out.revision == l.alignment.readiness.generation() && l.alignment.readiness.clockK() == k && out.value.frames != 0)
            check (std::llabs (out.value.frames - early) <= agreement, "no correction is published against a K it was not measured with");
        l.alignment.request (out.value, context, out.revision);
        return out;
    }

    // The content service's order (PluginProcessorContentTiming.cpp): the budget, then the estimates.
    void service (Listener& l, int delay)
    {
        auto tick = input (l, PreAlignEvent::tick);
        tick.pending = l.alignment.readiness.pending(); tick.spent = l.alignment.budgetSpent();
        tick.reach = l.alignment.servable(); tick.now = static_cast<std::uint64_t> (clock) * 1000000000u / rate; tick.readRetryNanos = 500000000u;
        if (tick.pending) l.alignment.arm();
        const auto out = send (l, tick);
        if (tick.pending && tick.spent && ! out.request) l.rereading = true;
        if (out.request)
        {
            ++fallbacks;
            if (out.value.frames != 0)
                check (confirmedCandidate == host.candidate && (! host.candidate || confirmedConditions == host.conditions),
                         "a kept correction never crosses LISTEN / BLIND and A/B or changed host conditions");
        }
        for (auto& e : l.estimates) --e.due;
        while (! l.estimates.empty() && l.estimates.front().due <= 0)
        {
            const auto e = l.estimates.front(); l.estimates.pop_front();
            if (e.revision != l.alignment.readiness.generation() || e.k != l.alignment.readiness.clockK()) { ++staleEstimates; continue; }
            send (l, input (l, PreAlignEvent::measured));
            const auto requested = l.alignment.requestFor (context).frames;
            if (l.alignment.readiness.pending() || e.value.frames == 0 || requested == 0
                || std::llabs (e.value.frames - requested) > agreement) l.validated.push_back (e);
        }
        if (content && l.alignment.pathProven())
            l.estimates.push_back ({ { early }, l.alignment.readiness.generation(), l.alignment.readiness.clockK(), delay });
    }

    // A validated estimate is bound and published; K may have changed since it was validated.
    void confirm (Listener& l)
    {
        for (const auto& e : l.validated)
        {
            auto confirmed = input (l, PreAlignEvent::estimate);
            confirmed.value = e.value; confirmed.measuredRevision = e.revision; confirmed.measuredK = e.k;
            if (send (l, confirmed).request) { confirmedCandidate = host.candidate; confirmedConditions = host.conditions; }
        }
        l.validated.clear();
    }

    void tick()
    {
        const int delay = 1 + pick (3);
        for (auto& l : listeners) service (l, delay);
        // The owner sees K at its services: the model changes K again only after one has seen the last change.
        if (listeners[0].alignment.readiness.clockK() == k && listeners[1].alignment.readiness.clockK() == k) unseenK = false;
        if (raceNext && ! unseenK && ! answering() && early > 0 && ! (listeners[0].validated.empty() && listeners[1].validated.empty()))
        {   // the host takes the delay over after the estimate was validated, before it is bound (not
            // while a spent budget's answer has not played: see answering())
            raceNext = false; ++races;
            moveK (early); early = 0; takeoverUnheard = true; calibrating = 2;
            for (int i = 0; i < 3; ++i) block();
        }
        for (auto& l : listeners) confirm (l);
    }

    void block()
    {
        ++blocks; clock += frames;
        for (int i = 0; i < 2; ++i) render (listeners[i], i);
        if (mode == Mode::blind && sameStart)
        {
            ++blindBlocks;
            auto& x = listeners[0]; auto& y = listeners[1];
            if (x.pathOpen && x.alignment.readiness.pending())
                check (x.address == 0 && y.address == 0, "unconfirmed BLIND paths both use the clock address");
            const auto toldX = x.owner.notice (x.alignment.unservedCorrection()), toldY = y.owner.notice (y.alignment.unservedCorrection());
            check (x.gateOpen == y.gateOpen && x.slotPre == y.slotPre,
                   "both BLIND assignments sound PRE or its substitute at the same blocks");
            check (x.pathOpen == y.pathOpen && x.alignment.pathProven() == y.alignment.pathProven()
                     && x.alignment.readiness.pending() == y.alignment.readiness.pending()
                     && x.alignment.budgetSpent() == y.alignment.budgetSpent() && toldX.unmeasured == toldY.unmeasured
                     && toldX.notRemeasured == toldY.notRemeasured, "both BLIND assignments wait and are told alike at every block");
        }
        wasValid = stopped == 0 && calibrating == 0;
        if (wasValid) takeoverUnheard = false; // the Audio Thread has seen that step of K by itself
        if (stopped > 0) { if (--stopped == 0) restartRun (pick (20)); }
        else { run += frames; if (calibrating > 0) --calibrating; }
    }

    // The model's one assumption about the host: once a budget is spent, the clock mapping holds
    // until the service's answer has played (K does not move between the two). Before that K moves freely.
    bool answering() const noexcept
    {
        if (allowClockChangesBeforeAnswer) return false; // diagnostic only; normal fixture keeps its assumption
        for (const auto& l : listeners) if (l.alignment.budgetSpent() && l.waited > 0) return true;
        return false;
    }
    // Seeks, short loops and re-calibrated clock origins faster than the service tick.
    void burstStep()
    {
        ++burstBlocks;
        switch (pick (4))
        {
            case 0: restartRun (pick (2)); break;                                // short loop / seek
            // (not before one block was proven at a takeover's K: no clock tells two changes without a mapping between them apart)
            case 1: case 2: if (stopped == 0 && ! answering() && ! takeoverUnheard) { moveK ((1 + pick (16)) * 64 * (pick (2) ? 1 : -1)); restartRun (pick (2)); } break;
            default: break;
        }
    }

    void event()
    {
        ++events;
        switch (pick (15))
        {
            case 0: case 1: case 2: restartRun (pick (40)); break;              // seek
            case 3: stopped = 1 + pick (30); break;                              // stop, then play
            case 4: restartRun (pick (3)); break;                                // loop wrap
            case 5: if (unseenK || answering()) break; moveK ((1 + pick (16)) * 64 * (pick (2) ? 1 : -1)); restartRun (pick (40)); break; // buffer / origin
            case 6: if (! unseenK && ! answering() && early > 0) { moveK (early); early = 0; takeoverUnheard = true; restartRun (pick (40)); } break; // host takes over
            case 7: if (! unseenK && ! answering() && early == 0 && base > 0) { moveK (-base); early = base; restartRun (pick (40)); } break; // and stops
            case 8:
            {
                const auto before = mode; mode = static_cast<Mode> (pick (3));
                if (mode == before) break;
                host.candidate = mode == Mode::candidate;
                if (host.candidate)
                {
                    const auto format = context.format++;
                    for (auto& l : listeners) l.owner.candidateRestarted (format, context.authority, host.peer);
                }
                for (auto& l : listeners) l.owner.transition (input (l, PreAlignEvent::contextChanged));
                selectedPre = true; newSession(); restartRun (pick (20));
                sameStart = mode == Mode::blind && alike();
                break;
            }
            case 9: selectedPre = ! selectedPre; break;
            case 10: frames = 64 << pick (5); break;
            case 11: content = ! content; break;
            case 12: if (host.candidate) { ++host.conditions; ++context.format; newSession(); } break; // the host's conditions changed
            case 13: raceNext = true; break;
            case 14: burst = 200 + pick (1200); break;
        }
    }
};

inline void storm (World& world, int blocks)
{
    const std::int64_t delays[] = { 0, 1065, 12000, 38400, 96000 };
    world.base = world.early = delays[world.pick (5)];
    world.tickEvery = 1 + world.pick (8);
    for (int i = 0; i < blocks; ++i)
    {
        if (world.burst > 0) { --world.burst; world.burstStep(); }
        else if (world.pick (60) == 0) world.event();
        world.block();
        if (++world.sinceTick >= world.tickEvery) { world.sinceTick = 0; world.tick(); }
    }
}
}

static void alignTransportStorm()
{
    using namespace live_compare_storm_test;
    constexpr std::uint32_t firstSeed = 1, seeds = 300; constexpr int blocksPerSeed = 6000;
    long blocks = 0, events = 0, substituted = 0, notices = 0, stale = 0, fallbacks = 0, blind = 0, races = 0, bursts = 0; std::int64_t longest = 0;
    for (std::uint32_t seed = firstSeed; seed < firstSeed + seeds; ++seed)
    {
        World world (seed);
        storm (world, blocksPerSeed);
        blocks += world.blocks; events += world.events; substituted += world.substituted; notices += world.notices;
        stale += world.staleEstimates; fallbacks += world.fallbacks; blind += world.blindBlocks; races += world.races; bursts += world.burstBlocks;
        if (world.longestWait > longest) longest = world.longestWait;
    }
    std::printf ("alignment transport storm: seeds %u..%u, %ld blocks (%ld compared in BLIND), %ld events, %ld waiting blocks, "
                 "%ld told blocks, %ld fallbacks, %ld stale estimates refused, %ld takeovers between validation and binding, longest wait %.3f s\n",
                 firstSeed, firstSeed + seeds - 1, blocks, blind, events, substituted, notices, fallbacks, stale, races,
                 static_cast<double> (longest) / rate);
    require (events > 10000 && fallbacks > 100 && stale > 100 && blind > 100000 && notices > 1000 && races > 20 && bursts > 10000 && longest > rate / 2,
             "the storm reaches the budget's end, kept corrections, stale estimates, BLIND and changes faster than the service");
}
