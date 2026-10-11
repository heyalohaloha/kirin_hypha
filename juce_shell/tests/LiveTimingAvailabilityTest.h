#pragma once
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLiveCompareRecoveryText.h"

template <typename Require>
int verifyTimingAvailability (Require require)
{
    using Processor = KirinHyphaProcessorBase;
    using namespace hypha::live_compare;
    int cases = 0;
    for (auto wrapper : { juce::AudioProcessor::wrapperType_AudioUnit,
                          juce::AudioProcessor::wrapperType_VST3,
                          juce::AudioProcessor::wrapperType_AAX })
        for (auto authority : { std::uint8_t { 0 }, std::uint8_t { 1 }, std::uint8_t { 2 } })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
            Processor post (Processor::Role::Post);
            for (auto observation : { RecoveryReason::none, RecoveryReason::calibrating,
                    RecoveryReason::notWritten, RecoveryReason::writing, RecoveryReason::loopWaiting,
                    RecoveryReason::loopUnproven, RecoveryReason::loopClockUnavailable,
                    RecoveryReason::loopTooShort })
            {
                LiveTimingFixtureAccess::timingObservation (post, observation, authority);
                const bool structural = observation == RecoveryReason::loopClockUnavailable
                    || observation == RecoveryReason::loopTooShort;
                const auto named = post.liveCompareStatus();
                require (named.timingUnavailable == structural,
                         "availability follows the observed failure, never the host authority number");
                for (auto phase : { BlindStage::preparing, BlindStage::approval, BlindStage::settling })
                {
                    LiveTimingFixtureAccess::blindWaiting (post, phase);
                    const auto blind = post.liveBlindStatus();
                    const auto screen = hypha::live_blind_ui::screenFor (blind, true, 1.0f);
                    require (blind.timingUnavailable == structural, "Blind preparation uses the same observed failure");
                    require (screen.instruction.contains ("unavailable") == structural,
                             "AU preparing, approving and settling never falsely claim unavailable");
                    if (! structural && phase == BlindStage::approval)
                        require (screen.approveShown && screen.instruction == "Lower POST to match levels?",
                                 "ordinary AU approval keeps its question and button together");
                    ++cases;
                }
            }
            LiveTimingFixtureAccess::timingObservation (post, RecoveryReason::none, authority);
            LiveTimingFixtureAccess::compensationDisabled (post, true);
            for (auto phase : { BlindStage::preparing, BlindStage::approval, BlindStage::settling })
            {
                LiveTimingFixtureAccess::blindWaiting (post, phase);
                const auto screen = hypha::live_blind_ui::screenFor (post.liveBlindStatus(), true, 1.0f);
                require (screen.guidanceShown && ! screen.approveShown
                    && screen.cause == hypha::live_compare_ui::cause (RecoveryReason::compensationOff)
                    && screen.recovery == "Enable DAW delay compensation", "observed disabled compensation explains its own remedy");
                ++cases;
            }
            LiveTimingFixtureAccess::compensationDisabled (post, false);
            LiveTimingFixtureAccess::timingObservation (post, RecoveryReason::loopClockUnavailable, authority);
            LiveTimingFixtureAccess::holdContent (post);
            const auto held = post.liveCompareStatus();
            require (! held.timingUnavailable
                && hypha::live_compare_ui::namedPresentation (held).action == hypha::live_compare_ui::RecoveryAction::stopPlay,
                     "content hold explains stop/play instead of structural unavailability");
            for (auto phase : { BlindStage::preparing, BlindStage::approval, BlindStage::settling })
            {
                LiveTimingFixtureAccess::blindWaiting (post, phase);
                const auto blind = post.liveBlindStatus();
                const auto screen = hypha::live_blind_ui::screenFor (blind, true, 1.0f);
                require (! blind.timingUnavailable && screen.instruction == "Comparison paused; POST plays"
                    && screen.recovery == "Stop/play DAW; END, then BLIND", "content hold retains its explicit remedy");
                ++cases;
            }
            juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
            Processor pre (Processor::Role::Pre);
            require (! pre.liveCompareStatus().timingUnavailable, "PRE gains no comparison restriction from its wrapper");
        }
    return cases;
}
