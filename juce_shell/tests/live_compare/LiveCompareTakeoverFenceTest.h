#pragma once
#include "../../src/live_compare/LiveComparePreAlign.h"

static void takeoverReceiptCompletesBeforeFallback()
{
    using namespace hypha::live_compare;
    for (bool takesOver : { false, true }) for (bool blind : { false, true }) for (bool selected : { false, true })
    {
        RendererAlignment renderer; PreAlignState owner; const AlignContext context { 3, 5, 7, 9 };
        AlignPlayback playback; playback.context = context; playback.verify = true; float weight = 0.0f;
        renderer.update (playback, blind, selected, weight, 48000, 128, 64);
        renderer.readiness.observe (true, true, true, 0);
        PreAlignInput seed; seed.context = context; seed.clock = &renderer.readiness;
        seed.event = PreAlignEvent::estimate; seed.revision = renderer.readiness.generation();
        seed.measuredRevision = seed.revision; seed.value = { 1000 }; seed.agreement = 2;
        const auto kept = owner.transition (seed); require (kept.request, "fence fixture keeps a settled correction");
        renderer.request (kept.value, context, kept.revision);
        auto initial = renderer.update (playback, blind, selected, weight, 48000, 128, 64);
        renderer.readiness.observe (true, true, true, 0); renderer.observed (true, true, initial);
        renderer.confirmApplied (initial.revision); renderer.served(); renderer.readiness.recheck(); renderer.arm();
        for (int i = 0; i < 8 && !renderer.budgetSpent(); ++i)
        {
            const auto update = renderer.update (playback, blind, selected, weight, 48000, 128, 64);
            renderer.resolve (0, weight); renderer.readiness.observe (true, true, true, 0);
            renderer.observed (true, true, update);
        }
        require (renderer.budgetSpent(), "fence fixture reaches the unanswered deadline");
        static RendererAlignment* interruptedRenderer = nullptr; static PreAlignState* interruptedOwner = nullptr;
        static AlignContext interruptedContext; static bool premature = false;
        interruptedRenderer = &renderer; interruptedOwner = &owner; interruptedContext = context; premature = false;
        afterAlignWatchReadForTest = [] (const AlignReadiness& clock)
        {
            PreAlignInput tick; tick.context = interruptedContext; tick.clock = &clock;
            tick.revision = clock.generation(); tick.k = clock.clockK(); tick.pending = tick.spent = true;
            tick.reach = 48000; tick.agreement = 2;
            tick.spent = false; interruptedOwner->transition (tick);
            require (!clock.provisionalClockIs (tick.revision), "before the deadline no provisional marker is created");
            tick.spent = true;
            const auto out = interruptedOwner->transition (tick);
            premature = out.request;
            if (out.request) interruptedRenderer->request (out.value, interruptedContext, out.revision);
            require (!out.request, "no fallback is confirmed while its takeover receipts are incomplete");
            require (interruptedRenderer->requestFor (interruptedContext).frames == 0,
                     "new revision cannot hear a double-corrected request from an interrupted owner tick");
        };
        renderer.readiness.observe (true, true, true, takesOver ? 1000 : 64);
        afterAlignWatchReadForTest = nullptr; interruptedRenderer = nullptr; interruptedOwner = nullptr;
        require (!premature, "owner did not publish an unchecked fallback");
        const auto provisional = renderer.update (playback, blind, selected, weight, 48000, 128, 64);
        renderer.resolve (takesOver ? 1000 : 64, weight);
        renderer.readiness.observe (true, true, true, takesOver ? 1000 : 64);
        renderer.observed (true, true, provisional);
        require (renderer.value().frames == 0 && !renderer.awaits (provisional),
                 "before the next tick both assignments use proven provisional K without extra waiting");
        renderer.served();
        require (renderer.budgetSpent(), "provisional K does not renew the unanswered correction budget");
        weight = 0.3f; renderer.resolve (takesOver ? 1000 : 64, weight);
        require (weight == 0.3f, "an existing provisional K fade is not restarted");
        require (owner.keptValue().frames == 1000 && renderer.readiness.pending() && !renderer.clockReleased(),
                 "provisional K neither discards, confirms, nor renews the retained correction");
        PreAlignInput tick; tick.context = context; tick.clock = &renderer.readiness;
        tick.revision = renderer.readiness.generation(); tick.k = renderer.readiness.clockK();
        tick.pending = tick.spent = true; tick.reach = 48000; tick.agreement = 2;
        const auto checked = owner.transition (tick);
        require (checked.request && checked.value.frames == (takesOver ? 0 : 1000),
                 "after receipt completion only a correction not taken over can be reused");
        renderer.request (checked.value, context, checked.revision);
        require (renderer.requestFor (context).frames == (takesOver ? 0 : 1000),
                 "the checked request preserves a legitimate correction and never doubles a takeover");
        weight = 0.0f;
        const auto resumed = renderer.update (playback, blind, false, weight, 48000, 128, 64);
        renderer.resolve (takesOver ? 1000 : 64, weight);
        require (resumed.current && renderer.value().frames == (takesOver ? 0 : 1000),
                 "the checked response supersedes a marker still stored for the same revision");
    }
}

static void blindUnappliedNoticeIsSymmetric()
{
    using namespace live_compare_storm_test;
    for (bool selected : { false, true })
    {
        World world (1); world.mode = Mode::blind; world.selectedPre = selected;
        world.content = false; world.run = rate * 8; world.wasValid = true;
        world.sameStart = true; world.base = world.early = 1000; world.block();
        for (auto& l : world.listeners)
        {
            auto estimate = world.input (l, PreAlignEvent::estimate);
            estimate.value = { 1000 }; estimate.measuredRevision = estimate.revision; estimate.measuredK = estimate.k;
            require (world.send (l, estimate).request, "blind fixture accepts its first estimate");
        }
        for (int i = 0; i < 8; ++i) world.block();
        world.early = 1001; // a changed request within the model's existing truth tolerance
        for (auto& l : world.listeners)
        {
            auto estimate = world.input (l, PreAlignEvent::estimate);
            require (estimate.blind && !estimate.pending, "settled BLIND estimate uses production input flags");
            estimate.value = { 1001 }; estimate.measuredRevision = estimate.revision; estimate.measuredK = estimate.k;
            require (world.send (l, estimate).request, "blind fixture accepts a later correction");
            require (l.owner.notice(false).unapplied, "both assignments show the not-yet-heard notice");
        }
        world.block();
        for (auto& l : world.listeners)
        {
            world.send (l, world.input (l, PreAlignEvent::heardBoth));
            require (!l.owner.notice(false).unapplied, "both assignments clear on the same heard-both event");
        }
        world.block();
    }
}

static void comparisonContinuesAfterWatchExhaustion()
{
    using namespace hypha::live_compare;
    RendererAlignment renderer; PreAlignState owner; AlignPlayback playback;
    playback.context = { 3, 5, 7, 9 }; playback.verify = true; float weight = 0.0f;
    renderer.update (playback, false, true, weight, 48000, 48000, 64);
    renderer.readiness.observe (true, true, true, 0);
    renderer.readiness.watchIdentityNearLimitForTest();
    for (auto amount : { 1000, 2000 })
    {
        PreAlignInput estimate; estimate.event = PreAlignEvent::estimate; estimate.context = playback.context;
        estimate.clock = &renderer.readiness; estimate.revision = renderer.readiness.generation();
        estimate.measuredRevision = estimate.revision; estimate.value = { amount }; estimate.agreement = 2;
        const auto out = owner.transition (estimate);
        require (out.request, "watch exhaustion does not disable a valid comparison request");
        renderer.request (out.value, playback.context, out.revision);
        const auto update = renderer.update (playback, false, true, weight, 48000, 48000, 64);
        renderer.resolve (0, weight); renderer.readiness.observe (true, true, true, 0);
        renderer.observed (true, true, update);
        require (renderer.value().frames == amount && !renderer.awaits (update),
                 "comparison still adopts a validated address after watch identities run out");
    }
}
