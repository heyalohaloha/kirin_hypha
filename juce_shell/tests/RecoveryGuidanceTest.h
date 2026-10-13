#pragma once
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaLiveBlindComponent.h"
namespace hypha::tests
{
template <class Require> void verifyRecoveryGuidance (Require require)
{
    using namespace live_compare;
    using namespace live_compare_ui;
    for (auto reason : { RecoveryReason::reaperFirstCycle, RecoveryReason::reaperDelayUnverified,
                        RecoveryReason::loopWaiting, RecoveryReason::loopTooShort })
    for (auto restriction : { ReaperChainRestriction::none, ReaperChainRestriction::unavailable })
    {
        Status named; named.active = named.preSelected = named.preWaiting = true;
        named.observation = reason; named.reaperRestriction = restriction;
        LiveBlindStatus blind; blind.stage = BlindStage::preparing;
        blind.observation = reason; blind.reaperRestriction = restriction;
        require (juce::String (namedRecovery (named)).isEmpty()
            && !live_blind_ui::screenFor (blind, true, 1).guidanceShown, "no guidance for a short recovery");
        named.prolongedWait = blind.prolongedWait = true;
        const juce::String text = namedRecovery (named);
        require (text.isNotEmpty() && text == blindRecovery (blind, false).instruction,
                 "LISTEN and BLIND explain the same long recovery the same way");
        if (reason == RecoveryReason::loopWaiting || (reason == RecoveryReason::reaperFirstCycle
            && restriction == ReaperChainRestriction::none))
            require (text == "Waiting for the first loop pass (POST)", "a first pass never asks for a longer loop");
        if (restriction == ReaperChainRestriction::unavailable
            && (reason == RecoveryReason::reaperFirstCycle || reason == RecoveryReason::reaperDelayUnverified))
            require (text.contains ("turn loop off") && !text.contains ("longer"), "API failure has an applicable remedy");
        named.preSelected = named.preWaiting = false;
        require (juce::String (namedRecovery (named)).isEmpty(), "POST selection has no PRE timing notice");
    }
    for (auto loop : { LoopPairingBlock::auCycle, LoopPairingBlock::aaxLoop })
    {
        Status named; named.active = named.prolongedWait = true; named.loopBlock = loop;
        require (juce::String (namedRecovery (named)).isEmpty(), "POST does not ask to disable a loop");
    }
    Status named; named.active = named.signalWait = true;
    LiveBlindStatus blind; blind.stage = BlindStage::preparing; blind.signalWait = blind.prolongedWait = true;
    const auto screen = live_blind_ui::screenFor (blind, true, 1);
    require (screen.cause.isEmpty() && screen.recovery == namedRecovery (named)
        && screen.recovery == "MATCH waits for more signal", "signal wait is one matching line, without an invented timing fault");
}
}
