#pragma once
#include "../../src/live_compare/LiveComparePreAlign.h"

namespace live_compare_release_resend_test
{
using namespace hypha::live_compare;
struct ClockCase
{
    RendererAlignment r;
    PreAlignState owner;
    AlignPlayback playback;
    PreAlignHost host;
    std::int64_t k = 512;
    float weight = 0.0f;
    bool blind, selected;
    ClockCase (bool isBlind, bool choosesPre, bool candidate) : blind (isBlind), selected (choosesPre)
    {
        playback.context = { 3, 5, 7, 9 }; playback.verify = true; host.candidate = candidate;
        block (true, true); estimate (1000); block (true, true); r.served();
        r.readiness.recheck(); r.arm();
        for (int n = 0; n < 8 && !r.budgetSpent(); ++n) block (true, true);
        require (r.budgetSpent() && r.releasedAt() == 0, "release race starts spent and unanswered");
    }
    AlignUpdate block (bool reaches, bool proven)
    {
        auto update = r.update (playback, blind, selected, weight, 48000, 128, 64);
        if (reaches) r.resolve (k, weight);
        r.readiness.observe (playback.enabled, true, true, k);
        r.observed (proven, proven, update);
        return update;
    }
    PreAlignInput input (PreAlignEvent event)
    {
        PreAlignInput in;
        in.event = event; in.context = playback.context; in.host = host; in.active = true;
        in.blind = blind; in.requested = r.requestFor (in.context);
        in.clock = &r.readiness; in.revision = r.readiness.generation(); in.k = r.readiness.clockK();
        in.pending = r.readiness.pending(); in.spent = r.budgetSpent(); in.releasedAt = r.releasedAt();
        in.reach = r.servable(); in.now = 1000000000; in.readRetryNanos = 500000000; in.agreement = 2;
        return in;
    }
    PreAlignOutput send (PreAlignInput in)
    {
        const auto out = owner.transition (in);
        if (out.request) r.request (out.value, in.context, out.revision, out.resend);
        return out;
    }
    void estimate (std::int64_t frames)
    {
        auto in = input (PreAlignEvent::estimate);
        in.value = { frames }; in.measuredRevision = in.revision; in.measuredK = in.k;
        const auto out = send (in);
        require (out.request && !out.resend && out.value.frames == frames, "fresh estimate is not a retained resend");
    }
    PreAlignOutput tick() { return send (input (PreAlignEvent::tick)); }
    void movedWithoutProof() { k += 64; block (false, false); require (r.releasedAt() == 0, "unproven K cannot release"); }
    void mustReadK (const AlignUpdate& update)
    { require (r.value().frames == 0 && !r.awaits (update), "proven C2 block reads K without waiting"); }
    void mustDiscard()
    {
        const auto out = tick();
        require (out.request && out.value.frames == 0 && owner.keptValue().frames == 0,
                 "owner consumes release even after a stale resend cleared pending");
        const auto notice = owner.notice (false);
        require (!notice.notRemeasured && notice.unmeasured == !host.candidate, "discard has the proper owner notice");
    }
};

inline void staleResendAfterReleaseIsRefused()
{
    ClockCase x (true, true, false), y (true, false, false);
    for (auto* c : { &x, &y })
    {
        c->movedWithoutProof(); const auto raced = c->input (PreAlignEvent::tick);
        c->mustReadK (c->block (true, true));
        require (c->r.releasedAt() != 0, "proof publishes release before the stale owner tick");
        const auto stale = c->send (raced);
        require (stale.request && stale.resend && stale.value.frames == 1000, "stale input really does send kept correction");
        for (int n = 0; n < 2; ++n)
        {
            const auto b = c->block (true, true);
            require (!b.current, "discarded resend cannot confirm the current address"); c->mustReadK (b);
        }
        require (!c->r.readiness.pending(), "stale resend exercises release consumption with pending false");
    }
    require (x.r.releasedAt() == y.r.releasedAt(), "both BLIND assignments release the same generation");
    for (auto* c : { &x, &y })
    {
        c->mustDiscard();
        const auto b = c->block (true, true); c->mustReadK (b);
        require (b.current, "owner confirms zero after consuming the release");
    }
}

inline void releaseSurvivesAnEndedContext()
{
    for (bool candidate : { false, true })
    {
        ClockCase c (false, true, candidate);
        c.movedWithoutProof(); c.mustReadK (c.block (true, true));
        const auto release = c.r.releasedAt();
        auto ended = c.input (PreAlignEvent::tick);
        ended.context.ownerA = 0; ended.active = false; ended.pending = false;
        c.send (ended);
        require (c.owner.keptValue().frames == 1000, "unmatched END context cannot consume the held-value receipt");
        c.r.newSession(); c.r.readiness.recheck();
        auto restarted = c.input (PreAlignEvent::tick);
        restarted.pending = restarted.spent = true; restarted.reach = 48000;
        require (restarted.releasedAt == release, "release receipt survives session restart");
        const auto answer = c.send (restarted);
        require (answer.request && answer.value.frames == 0 && c.owner.keptValue().frames == 0,
                 "same context restart consumes release before any retained resend");
    }
}

inline void resendBeforeProofKeepsTheRelease()
{
    ClockCase x (true, true, false), y (true, false, false);
    for (auto* c : { &x, &y })
    {
        c->movedWithoutProof();
        const auto out = c->tick();
        require (out.request && out.resend && out.value.frames == 1000, "owner resend really arrives ahead of proof");
        const auto refused = c->block (false, false);
        require (c->r.releasedAt() == 0 && c->r.awaits (refused), "resend is not an answer while proof is missing");
        c->mustReadK (c->block (true, true));
    }
    require (x.r.releasedAt() != 0 && x.r.releasedAt() == y.r.releasedAt(), "proof releases both assignments");
    x.mustDiscard(); y.mustDiscard();
}

inline void freshEstimateIsNeverRefused()
{
    ClockCase x (true, true, false), y (true, false, false);
    x.weight = 1.0f;
    x.movedWithoutProof(); y.movedWithoutProof();
    x.estimate (2000); y.estimate (2000);
    const auto bx = x.block (true, true), by = y.block (true, true);
    require (bx.current && by.current && x.r.releasedAt() == 0 && y.r.releasedAt() == 0,
             "a fresh estimate answers rather than releases the budget");
    require (x.r.value().frames == 1000 && y.r.value().frames == 2000,
             "BLIND retains only the audible old address until switching");
    require (x.owner.notice(false).unapplied == y.owner.notice(false).unapplied, "fresh estimate notice is assignment-neutral");
    for (int n = 0; n < 3; ++n)
        for (auto* c : { &x, &y })
        {
            c->tick();
            require (c->owner.keptValue().frames == 2000, "owner retains the fresh estimate");
        }
}

inline void freshEstimateAfterReleaseSurvives()
{
    for (bool candidate : { false, true })
    {
        ClockCase c (false, true, candidate);
        c.movedWithoutProof(); c.mustReadK (c.block (true, true));
        require (c.r.releasedAt() != 0, "release predates owner notification");
        c.estimate (2000);
        const auto next = c.tick();
        require (!next.request && c.owner.keptValue().frames == 2000, "old release cannot erase a fresh estimate");
    }
}

inline void unreachedBlockNeverReleases()
{
    ClockCase c (false, true, false);
    c.movedWithoutProof();
    const auto b = c.r.update (c.playback, false, true, c.weight, 48000, 128, 64);
    c.r.readiness.observe (true, true, true, c.k);
    c.r.observed (false, true, b); // Path classification alone, without reaching the pre-read resolver.
    require (c.r.releasedAt() == 0 && c.owner.keptValue().frames == 1000, "unreached block cannot release");
    c.mustReadK (c.block (true, true));
    require (c.r.releasedAt() != 0, "first resolved proof can release");
}

inline void settingChangeIsNoClockMove()
{
    for (bool toggle : { false, true })
    {
        ClockCase c (false, true, false);
        if (toggle)
        {
            c.playback.enabled = false; c.block (true, true);
            c.playback.enabled = true; c.block (true, true);
        }
        else
        {
            // Complete a measurement revision, then generate another revision without changing K.
            c.r.readiness.confirm (c.r.readiness.generation());
            c.r.readiness.recheck(); c.block (true, true);
        }
        require (c.r.releasedAt() == 0, "non-clock revision cannot discard correction");
        const auto resent = c.tick();
        require (resent.request && resent.resend && resent.value.frames == 1000, "non-clock revision accepts retained answer");
        c.block (true, true);
        require (c.r.value().frames == 1000 && c.owner.notice(false).notRemeasured, "retained answer keeps prior notice");
    }
}
inline void blindPathUsesTheSameRequestSnapshot()
{
    for (bool selected : { false, true })
    {
        ClockCase c (true, selected, false);
        c.movedWithoutProof(); c.mustReadK (c.block (true, true));
        const auto generation = c.r.readiness.generation();
        c.r.request ({}, c.playback.context, generation);
        static ClockCase* changing = nullptr; changing = &c;
        afterAlignRequestedReadForTest = [] {
            changing->r.request ({1000}, changing->playback.context, changing->r.readiness.generation(), true);
        };
        const auto one = c.r.update (c.playback, true, selected, c.weight, 500, 48000, 64);
        afterAlignRequestedReadForTest = nullptr; changing = nullptr;
        require (one.current && !one.deferred && !c.r.unservedCorrection(),
                 "BLIND reach and notice use the same zero snapshot, not a later rejected resend");
        const auto next = c.r.update (c.playback, true, selected, c.weight, 500, 48000, 64);
        require (!next.current && !c.r.unservedCorrection(), "the next snapshot refuses the stale resend for both assignments");
    }
}

}
