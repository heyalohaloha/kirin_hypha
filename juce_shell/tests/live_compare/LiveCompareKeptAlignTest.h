#pragma once
#include "../../src/live_compare/LiveComparePreAlign.h"
#include "AaxChunkChain.h"

// INV-LC28: a correction confirmed before stays for the next LISTEN, BLIND or A/B although PRE's new
// run does not serve it yet when the wait ends. PRE plays at K, told as unmeasured, until the run
// reaches it; then it is applied again (LISTEN and A/B by the usual fade, a trial's source once it
// is on POST's side) and told as not re-measured. The Audio Thread's alignment and the owner are
// the product's; the clocks and PRE's run are a model, in the renderer's and the service's order.
namespace live_compare_kept_test
{
using namespace hypha::live_compare;
constexpr std::int64_t rate = 48000, unknown = std::numeric_limits<std::int64_t>::max();
constexpr int frames = 256;

struct Listener
{
    RendererAlignment alignment;
    PreAlignState owner;
    float weight = 0.0f;
    bool pre = false, playsPre = false;
    std::uint64_t seenRevision = 0;
    AlignNotice told() const noexcept { return owner.notice (alignment.unservedCorrection()); }
    std::int64_t address() const noexcept { return alignment.value().frames; }
};

struct Session
{
    AlignContext context { 3, 5, 7, 9 };
    PreAlignHost host;
    bool blind = false, wasValid = false, serviced = true;
    std::int64_t run = 0, clock = 0, k = 512;
    Listener listeners[2];

    void select (bool xIsPre) noexcept { listeners[0].pre = xIsPre; listeners[1].pre = ! xIsPre; }
    PreAlignInput input (const Listener& l, PreAlignEvent event) const noexcept
    {
        PreAlignInput in;
        in.event = event; in.context = context; in.host = host; in.active = true; in.agreement = 2;
        in.revision = l.alignment.readiness.generation(); in.k = l.alignment.readiness.clockK();
        in.clock = &l.alignment.readiness;
        return in;
    }
    void send (Listener& l, const PreAlignInput& in) noexcept
    {
        if (const auto out = l.owner.transition (in); out.request) l.alignment.request (out.value, context, out.revision);
    }
    void render (Listener& l) noexcept
    {
        AlignPlayback playback; playback.context = context; playback.verify = true;
        const auto update = l.alignment.update (playback, blind, l.pre, l.weight, wasValid ? run : unknown, rate, frames);
        const bool accepted = l.address() <= run;
        l.alignment.readiness.observe (true, true, true, k);
        l.alignment.observed (accepted, true, update);
        const bool waits = l.alignment.awaits (update);
        l.playsPre = false;
        if (! accepted || waits) { l.weight = 0.0f; return; }
        if (update.current) l.alignment.confirmApplied (update.revision);
        const float target = l.pre && ! update.fading ? 1.0f : 0.0f;
        if ((blind || target > 0.0f || ! l.pre) && ! update.deferred) l.alignment.served();
        l.playsPre = target > 0.0f; l.weight = target;
    }
    void service (Listener& l) noexcept
    {
        if (const auto revision = l.alignment.readiness.generation(); revision != l.seenRevision)
        {
            l.seenRevision = revision;
            send (l, input (l, PreAlignEvent::clockChanged));
        }
        auto tick = input (l, PreAlignEvent::tick);
        tick.pending = l.alignment.readiness.pending(); tick.spent = l.alignment.budgetSpent();
        tick.reach = l.alignment.servable();
        tick.now = static_cast<std::uint64_t> (clock) * 1000000000u / rate; tick.readRetryNanos = 500000000u;
        if (tick.pending) l.alignment.arm();
        send (l, tick);
    }
    void play (int blocks) noexcept
    {
        for (int i = 0; i < blocks; ++i)
        {
            clock += frames;
            for (auto& l : listeners) { render (l); if (serviced) service (l); }
            wasValid = true; run += frames;
        }
    }
    void measure (std::int64_t correction) noexcept
    {
        for (auto& l : listeners)
        {
            send (l, input (l, PreAlignEvent::measured));
            auto estimate = input (l, PreAlignEvent::estimate);
            estimate.value = { correction }; estimate.measuredRevision = estimate.revision; estimate.measuredK = estimate.k;
            send (l, estimate);
        }
    }
    // END, then the same mode starts again: PRE publishes a new run.
    void restart() noexcept
    {
        const auto format = context.format;
        if (host.candidate) ++context.format;
        for (auto& l : listeners)
        {
            auto ended = input (l, PreAlignEvent::transport); ended.active = false;
            send (l, ended);
            if (host.candidate)
            {
                l.owner.candidateRestarted (format, context.authority, host.peer);
                send (l, input (l, PreAlignEvent::contextChanged));
            }
            l.alignment.newSession(); l.weight = 0.0f;
        }
        select (true); run = 0; wasValid = false;
    }
};
}

static void keptCorrectionWaitsForPresNewRun()
{
    using namespace live_compare_kept_test;
    for (const int mode : { 0, 1, 2 }) // LISTEN, BLIND, A/B
        for (const std::int64_t correction : { std::int64_t (1065), std::int64_t (103008) }) // 22 ms and 2.1 s
        {
            Session s; s.blind = mode == 1; s.host = { mode == 2, 11, 1, 1 };
            auto& x = s.listeners[0]; auto& y = s.listeners[1];
            s.select (true); s.play (8);
            s.measure (correction);
            s.play (500); s.select (false); s.play (4); s.select (true); s.play (8);
            require (x.playsPre && x.address() == correction && x.owner.keptValue().frames == correction,
                     "the fixture confirmed the correction and plays PRE with it");
            s.restart();
            std::int64_t firstAddress = -1;
            for (int i = 0; i < 282; ++i) // 1.5 s: past the one-second wait, short of the long correction
            {
                s.play (1);
                if (x.playsPre && firstAddress < 0) firstAddress = x.address();
            }
            const bool beyond = correction > s.run;
            require (x.owner.keptValue().frames == correction && y.owner.keptValue().frames == correction,
                     "a kept correction PRE's new run does not serve yet stays kept");
            if (beyond)
                require (x.playsPre && x.address() == 0 && x.told().unmeasured && ! x.told().notRemeasured,
                         "until PRE's new run reaches a kept correction, PRE plays at K, told as unmeasured, never POST");
            else
                require (x.playsPre && firstAddress == correction && x.told().notRemeasured && ! x.told().unmeasured,
                         "a kept correction the run serves plays from the first audible block, told as not re-measured");
            if (s.blind)
                require (x.told().unmeasured == y.told().unmeasured && x.told().notRemeasured == y.told().notRemeasured,
                         "both BLIND sources are told alike while the kept correction waits");
            s.play (200); // 2.57 s: the run serves either correction
            require (x.owner.keptValue().frames == correction && x.told().notRemeasured && ! x.told().unmeasured
                     && y.told().notRemeasured && ! y.told().unmeasured,
                     "once PRE's run reaches the kept correction it is told as not re-measured");
            if (s.blind && beyond)
            {
                require (x.playsPre && x.address() == 0 && y.address() == correction,
                         "a trial's playing PRE stays at K; the source on POST's side takes the kept correction");
                s.select (false); s.play (4); s.select (true); s.play (8);
            }
            require (x.playsPre && x.address() == correction, "PRE plays with the kept correction again once its run reaches it");
        }
}

// INV-LC28: a host that takes the kept delay over is known by that one step of K, whatever K does
// after it (a buffer size, a re-calibrated origin) before the service has seen either step. The
// Audio Thread compares each step with the kept amount; nothing else can tell the two apart later.
static void takeoverIsKnownByItsOwnStepOfK()
{
    using namespace live_compare_kept_test;
    constexpr std::int64_t correction = 96000;
    for (const bool blind : { false, true })
        // 0: the takeover and another change; 1: two other changes; 2: an estimate from before the takeover;
        // 3: two other changes that add up to the kept amount; 4: a new estimate from before another
        // change; 5: the takeover between END and the next start.
        for (const int scenario : { 0, 1, 2, 3, 4, 5 })
        {
            Session s; s.blind = blind;
            auto& x = s.listeners[0]; auto& y = s.listeners[1];
            s.select (true); s.play (8);
            s.measure (correction);
            s.play (500);
            require (x.owner.keptValue().frames == correction && y.owner.keptValue().frames == correction, "the fixture keeps a correction");
            // An estimate of the same amount whose snapshot of the mapping is taken before K moves.
            PreAlignInput stale[2];
            for (int i = 0; i < 2; ++i)
            {
                stale[i] = s.input (s.listeners[i], PreAlignEvent::estimate);
                stale[i].value = { scenario == 4 ? correction + 16 : correction }; stale[i].measuredRevision = stale[i].revision; stale[i].measuredK = stale[i].k;
            }
            s.serviced = false; // both changes happen between two services
            if (scenario == 5) s.restart();
            s.k += scenario == 1 || scenario == 4 ? 512 : scenario == 3 ? correction + 256 : correction; s.play (1);
            if (scenario == 2 || scenario == 4) for (int i = 0; i < 2; ++i) s.send (s.listeners[i], stale[i]);
            s.k += scenario == 3 ? -256 : 256; s.play (1);
            s.serviced = true; s.play (400);
            const bool keeps = scenario == 1 || scenario == 3 || scenario == 4;
            const auto kept = keeps ? correction : 0;
            require (x.owner.keptValue().frames == kept && y.owner.keptValue().frames == kept,
                     scenario == 0 ? "K moved by the kept amount and then again: the host took the delay over, the correction is dropped"
                     : scenario == 1 ? "two other changes of K keep the correction"
                     : scenario == 2 ? "an estimate from before the takeover never puts the correction back"
                     : scenario == 3 ? "no single step of K was the kept amount: their sum is no takeover"
                     : scenario == 4 ? "an estimate from before another change of K is refused alone: the kept correction stays"
                                     : "K moved by the kept amount from one session's last mapping to the next one's first: dropped");
            if (! keeps)
                require (x.address() == 0 && y.address() == 0, "PRE never plays the kept amount late after the host took it over");
        }
}

// The owner promotes after the audio snapshot but before the receipt is written.
static void takeoverDuringPromotion()
{
    using namespace hypha::live_compare;
    AlignReadiness clock;
    clock.observe (true, true, true, 0);
    clock.watchKept (1000, 2);
    clock.watchNext (2000, 2);
    const auto revision = clock.generation();
    require (clock.generation() == revision, "the estimate passed its generation re-check");
    afterAlignWatchReadForTest = [] (const AlignReadiness& c) { c.watchKept (2000, 2); };
    clock.observe (true, true, true, 2000);
    afterAlignWatchReadForTest = nullptr;
    require (clock.takenOver() == 2000, "promotion cannot hide the new correction's takeover");
    clock.observe (true, true, true, 4000);
    require (clock.takenOver() == 2000, "a snapshot taken after promotion also catches the takeover");
}

// One step can match both amounts; a rejected new estimate must not hide the old match.
static void takeoverOfBothWatchSlots()
{
    using namespace live_compare_kept_test;
    Session s; s.select (true); s.play (8); s.measure (1000);
    auto& l = s.listeners[0]; auto& clock = l.alignment.readiness;
    auto estimate = s.input (l, PreAlignEvent::estimate);
    estimate.value = { 1003 }; estimate.measuredRevision = estimate.revision; estimate.measuredK = estimate.k;
    clock.watchNext (1003, 2);
    clock.observe (true, true, true, estimate.k + 1001);
    require (clock.generation() != estimate.revision, "the new estimate is rejected after the clock step");
    clock.watchNext (0, 2);
    s.send (l, estimate);
    require (l.owner.keptValue().frames == 0, "a rejected overlapping estimate cannot hide the kept takeover");

    AlignReadiness rejected;
    rejected.observe (true, true, true, 0);
    rejected.watchKept (1000, 2); rejected.watchNext (2000, 2);
    rejected.observe (true, true, true, 2000);
    rejected.watchNext (0, 2);
    require (rejected.takenOver() == 0, "a rejected next-slot match never drops the previous correction");
}

// INV-LC28 through the renderer itself: once the one budget is spent and answered, a new clock
// mapping waits for no content check. PRE goes on at K from the first proven block.
static void spentBudgetNeverWaitsAgainInTheRenderer()
{
    using namespace aax_chunk_test;
    Options options; options.k = -512; options.candidate = true; // alignment is verified
    Chain chain (options);
    chain.run (30);
    const auto& alignment = chain.postRenderer().alignState();
    for (int i = 0; i < 600 && ! alignment.budgetSpent(); ++i) { alignment.arm(); chain.chunk(); }
    require (alignment.budgetSpent() && chain.report.alignWaiting, "the fixture spends the budget waiting for a content check");
    chain.relocate (48000);
    chain.run (4);
    chain.confirmAlign ({ 96000 }); // the service's answer: a kept correction PRE's new run does not serve yet
    chain.run (12);
    require (chain.report.preAudible && ! chain.report.alignWaiting && alignment.budgetSpent()
             && chain.postRenderer().alignFrames (alignOwner ({}, chain.sharedRing())) == 0, "PRE plays at K while the kept correction waits for its run");
    chain.setK (options.k + 1024);
    int waited = 0;
    for (int i = 0; i < 30; ++i)
        chain.chunk ([&] (int callback, BlockClock&) { if (callback > 0 && chain.report.alignWaiting) ++waited; });
    require (chain.decision.kValid && chain.decision.k == options.k + 1024, "the fixture proves the new clock mapping");
    require (waited == 0 && chain.report.preAudible, "a spent budget never puts POST in PRE's place for a new content check");
}
