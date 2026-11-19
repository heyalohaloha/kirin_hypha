#pragma once
#include "LiveCompareAlignStormTest.h"

static void alignClockStormAfterTheBudget()
{
    using namespace live_compare_storm_test;
    for (auto mode : { Mode::listen, Mode::blind, Mode::candidate })
        for (bool selected : { false, true }) for (int frames : { 64, 128, 256, 1024 })
        {
            World world (1); world.mode = mode; world.host.candidate = mode == Mode::candidate;
            world.selectedPre = selected; world.frames = frames; world.content = false;
            world.sameStart = true; world.tickEvery = 4; world.allowClockChangesBeforeAnswer = true;
            world.run = rate * 8; world.wasValid = true; world.base = world.early = 1000; world.block();
            const auto settle = [&] (std::int64_t correction) {
                for (auto& l : world.listeners)
                {
                    auto estimate = world.input (l, PreAlignEvent::estimate);
                    estimate.value = { correction }; estimate.measuredRevision = estimate.revision; estimate.measuredK = estimate.k;
                    if (world.send (l, estimate).request)
                    { world.confirmedCandidate = world.host.candidate; world.confirmedConditions = world.host.conditions; }
                }
            };
            settle (1000); for (int i = 0; i < 8; ++i) world.block();
            require (world.listeners[0].address == 1000 && world.listeners[1].address == 1000,
                     "fixed storm starts with the confirmed correction under both assignments");
            const auto oldRevision = world.listeners[0].alignment.readiness.generation(); const auto oldK = world.k;
            bool delayedDeadlineAnswer = false;
            const int blocks = static_cast<int> ((rate + world.tickAllowance()) / frames) + 24;
            for (int i = 0; i < blocks; ++i)
            {
                world.moveK (64); world.block();
                if (i % 4 == 0)
                {
                    // Exercise the unanswered branch: at 128/256 frames expiry coincides with the
                    // regular tick. Delay that one service, rather than testing the answered A4 path.
                    if (!delayedDeadlineAnswer && world.listeners[0].alignment.budgetSpent()
                        && !world.listeners[0].alignment.clockReleased()) delayedDeadlineAnswer = true;
                    else world.tick();
                }
                for (const auto& l : world.listeners)
                {
                    require (l.waited <= rate + world.tickAllowance(), "fixed storm does not extend the single wait budget");
                    if (static_cast<std::int64_t> (i) * frames > rate + world.tickAllowance())
                        require (l.pathOpen && l.address == 0, "a proven continuing storm uses K after the budget");
                }
            }
            world.tick();
            for (auto& l : world.listeners)
            {
                require (l.owner.keptValue().frames == 0, "moved unanswered budget discards the retained correction");
                if (mode != Mode::candidate)
                    require (l.owner.notice(false).unmeasured, "discarded correction is explained as unmeasured");
                auto stale = world.input (l, PreAlignEvent::estimate);
                stale.value = { 1000 }; stale.measuredRevision = oldRevision; stale.measuredK = oldK;
                require (!world.send (l, stale).request, "old mapping cannot restore a discarded correction");
            }
            world.early = 2000; settle (2000);
            for (int i = 0; i < 8; ++i) world.block();
            if (mode == Mode::blind)
            {
                world.selectedPre = !selected; for (int i = 0; i < 4; ++i) world.block();
                world.selectedPre = selected; for (int i = 0; i < 4; ++i) world.block();
            }
            for (const auto& l : world.listeners)
                require (l.gateOpen && l.address == 2000 && !l.alignment.budgetSpent(),
                         "fresh confirmed estimate restores the correction and renews the budget");
        }
}

static void spentBudgetReleasesOnceAtAMovedClock()
{
    using namespace hypha::live_compare;
    RendererAlignment alignment; AlignPlayback playback; playback.context = { 3, 5, 7, 9 }; playback.verify = true;
    float weight = 0.0f; std::int64_t k = 512;
    alignment.update (playback, false, true, weight, 48000, 128, 64);
    alignment.readiness.observe (true, true, true, k);
    alignment.request ({ 1000 }, playback.context, alignment.readiness.generation());
    auto initial = alignment.update (playback, false, true, weight, 48000, 128, 64);
    alignment.readiness.observe (true, true, true, k); alignment.observed (true, true, initial);
    alignment.confirmApplied (initial.revision); alignment.served(); weight = 1.0f;
    require (alignment.value().frames == 1000, "release fixture starts at the retained address");
    alignment.readiness.recheck(); alignment.arm();
    for (int i = 0; i < 8 && !alignment.budgetSpent(); ++i)
    {
        auto update = alignment.update (playback, false, true, weight, 48000, 128, 64);
        alignment.resolve (k, weight); alignment.readiness.observe (true, true, true, k);
        alignment.observed (true, true, update);
    }
    require (alignment.budgetSpent() && !alignment.clockReleased(), "spending alone does not discard a retained correction");
    auto update = alignment.update (playback, false, true, weight, 48000, 128, 64);
    alignment.resolve (k + 64, weight);
    require (weight == 0.0f && alignment.value().frames == 0, "late moved mapping switches to K before reading");
    alignment.observed (false, false, update);
    require (!alignment.clockReleased(), "a rejected unproven block cannot publish a clock release");
    update = alignment.update (playback, false, true, weight, 48000, 128, 64);
    k += 64; weight = 0.3f; alignment.resolve (k, weight);
    alignment.readiness.observe (true, true, true, k); alignment.observed (true, true, update);
    require (alignment.clockReleased() && weight == 0.3f, "the proven block commits release without restarting a K fade");
    for (int i = 0; i < 10; ++i)
    {
        update = alignment.update (playback, false, true, weight, 48000, 128, 64);
        k += 64; weight = 0.3f; alignment.resolve (k, weight);
        alignment.readiness.observe (true, true, true, k); alignment.observed (true, true, update);
        require (weight == 0.3f && alignment.value().frames == 0, "later mappings never restart the same K fade");
    }
}

static void clockReleaseStartsAfterTheSpentBlock()
{
    using namespace hypha::live_compare;
    for (int movedBlock : { 2, 3, 4 })
    {
        RendererAlignment alignment; AlignPlayback playback; playback.context = { 3, 5, 7, 9 }; playback.verify = true;
        float weight = 0.0f; std::int64_t k = 512;
        alignment.update (playback, false, true, weight, 48000, 128, 64);
        alignment.readiness.observe (true, true, true, k);
        alignment.request ({ 1000 }, playback.context, alignment.readiness.generation());
        const auto initial = alignment.update (playback, false, true, weight, 48000, 128, 64);
        alignment.readiness.observe (true, true, true, k); alignment.observed (true, true, initial);
        alignment.confirmApplied (initial.revision); alignment.served(); weight = 1.0f;
        alignment.readiness.recheck(); alignment.arm();
        for (int block = 1; block <= 4; ++block)
        {
            const auto update = alignment.update (playback, false, true, weight, 48000, 128, 64);
            if (block == movedBlock) k += 64;
            alignment.resolve (k, weight); alignment.readiness.observe (true, true, true, k);
            alignment.observed (true, true, update);
            require (alignment.budgetSpent() == (block >= 3), "boundary fixture spends the budget on the third block");
            require (alignment.clockReleased() == (movedBlock == 4 && block == 4),
                     "a mapping on or before the spent block cannot release the unanswered budget");
        }
        require (alignment.value().frames == (movedBlock == 4 ? 0 : 1000),
                 "only a later mapping discards the retained address at the deadline boundary");
    }
}

// A valid observed K may still have no proven read path (loop wait or a missing token).
static void repeatedRefusalsKeepTheCorrectionUntilProof()
{
    using namespace hypha::live_compare;
    for (bool blind : { false, true }) for (bool selected : { false, true })
    {
        RendererAlignment alignment; AlignPlayback playback; playback.context = { 3, 5, 7, 9 }; playback.verify = true;
        float weight = 0.0f; std::int64_t k = 512;
        alignment.update (playback, blind, selected, weight, 48000, 128, 64);
        alignment.readiness.observe (true, true, true, k);
        alignment.request ({ 1000 }, playback.context, alignment.readiness.generation());
        const auto initial = alignment.update (playback, blind, selected, weight, 48000, 128, 64);
        alignment.readiness.observe (true, true, true, k); alignment.observed (true, true, initial);
        alignment.confirmApplied (initial.revision); alignment.served(); weight = 1.0f;
        alignment.readiness.recheck(); alignment.arm();
        for (int i = 0; i < 8 && !alignment.budgetSpent(); ++i)
        {
            const auto update = alignment.update (playback, blind, selected, weight, 48000, 128, 64);
            alignment.resolve (k, weight); alignment.readiness.observe (true, true, true, k);
            alignment.observed (true, true, update);
        }
        require (alignment.budgetSpent(), "refusal fixture starts with a spent unanswered budget");
        for (int i = 0; i < 4; ++i)
        {
            const auto update = alignment.update (playback, blind, selected, weight, 48000, 128, 64);
            require (!alignment.clockReleased(), "revision alone cannot notify retained correction release");
            if (i == 0) k += 64;
            // Rejected Consumer path never reaches resolve(), but still observes valid K.
            alignment.readiness.observe (true, true, true, k);
            alignment.observed (false, false, update);
            require (!alignment.clockReleased(), "consecutive unproven blocks preserve the retained correction");
        }
        const auto proven = alignment.update (playback, blind, selected, weight, 48000, 128, 64);
        require (!alignment.clockReleased(), "the final update still waits for proof");
        alignment.resolve (k, weight); alignment.readiness.observe (true, true, true, k);
        alignment.observed (true, true, proven);
        require (alignment.clockReleased() && alignment.value().frames == 0,
                 "the same proven block releases and adopts K under either assignment");
    }
}
