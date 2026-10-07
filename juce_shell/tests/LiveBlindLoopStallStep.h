#pragma once

// Included in BlindContract. A deliberate 1.1 s stall of the test machine during a LOOP that was
// entered from linear playback with a fixed MATCH. This host has no certified loop clock, so Hypha
// cannot identify the PRE occurrence again: it must keep POST, give the cause, and leave END
// (README, LOOP proof). Nothing may play PRE after the stall.
void loopStallStep()
{
    using Reason = hypha::live_compare::RecoveryReason;
    const auto status = post->liveCompareStatus();
    switch (loopStallStage)
    {
        case 0: // arm once the named LOOP plays PRE with the fixed MATCH
            if (! status.matchReady || ! status.preAudible || clock.loop.laps.load() < 20) return;
            stallAtLap.store (clock.loop.laps.load() + 2);
            loopStallStage = 1;
            return;
        case 1: // the product reports the occurrence it cannot identify
            if (stallBlock.load() < 0 || status.observation != Reason::loopClockUnavailable) return;
            require (status.active && ! status.preAudible && ! status.matched && status.reason == Reason::callbackGap,
                     "after the stall POST plays, the MATCH is not current, and the cause is the gap");
            loopStallLap = clock.loop.laps.load();
            loopStallStage = 2;
            return;
        case 2: // ten more laps: still POST, never PRE
            if (clock.loop.laps.load() < loopStallLap + 10) return;
            require (! status.preAudible && stallPreBlocks.load() == 0 && stallPostErrors.load() == 0
                         && stallPostFrames.load() >= 9 * LiveBlindLoopFixture::length,
                     "no PRE after the stall; every steady block is exact POST");
            require (loopPcmErrors.load() == 0, "before the stall, the named LOOP was exact");
            require (click ("observatory-live-end"), "END stays available");
            loopStallStage = 3;
            return;
        default: // END returns the normal output
        {
            post->serviceLiveCompare(); // consume the RT receipt before reading the result
            const auto ended = post->liveCompareStatus();
            if (ended.active || ended.finishing) return;
            require (ended.postActual == 1.0f, "END returns to exact unity POST");
            std::cout << "Live Blind product: PASS loop stall keeps POST, gives the cause, ends\n";
            passed = true;
            stopTimer();
            juce::MessageManager::getInstance()->stopDispatchLoop();
            return;
        }
    }
}

int loopStallStage = 0;
std::int64_t loopStallLap = 0;
