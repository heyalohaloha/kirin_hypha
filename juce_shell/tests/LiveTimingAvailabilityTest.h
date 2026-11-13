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
                    require (!screen.guidanceShown, "short timing loss stays quiet in every wrapper");
                    if (phase == BlindStage::approval)
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
                require (!screen.guidanceShown && !screen.approveShown,
                         "short compensation loss stays quiet without granting unsafe approval");
                ++cases;
            }
            LiveTimingFixtureAccess::compensationDisabled (post, false);
            LiveTimingFixtureAccess::timingObservation (post, RecoveryReason::none, authority);
            LiveTimingFixtureAccess::noticeContent (post);
            const auto held = post.liveCompareStatus();
            require (held.contentHeld && ! held.timingUnavailable
                && hypha::live_compare_ui::namedPresentation (held).action == hypha::live_compare_ui::RecoveryAction::none,
                     "a short content timing notice stays quiet");
            for (auto phase : { BlindStage::preparing, BlindStage::approval, BlindStage::settling })
            {
                LiveTimingFixtureAccess::blindWaiting (post, phase);
                const auto blind = post.liveBlindStatus();
                const auto screen = hypha::live_blind_ui::screenFor (blind, true, 1.0f);
                require (!blind.timingUnavailable && !screen.guidanceShown,
                         "a content notice does not interrupt the current preparation");
                auto prolonged = blind; prolonged.prolongedWait = true;
                const auto longScreen = hypha::live_blind_ui::screenFor (prolonged, true, 1.0f);
                require (longScreen.guidanceShown && longScreen.recovery == hypha::live_compare_ui::chainDelayNotice()
                    && longScreen.cause == hypha::live_compare_ui::cause (RecoveryReason::preLate)
                    && ! longScreen.recovery.contains ("POST") && ! longScreen.alignOffShown,
                         "a long late-PRE notice names the chain delay, no output and no END");
                // A clock restriction that really plays POST outranks the notice.
                LiveTimingFixtureAccess::timingObservation (post, RecoveryReason::loopClockUnavailable, authority);
                auto restricted = post.liveBlindStatus(); restricted.prolongedWait = true;
                require (restricted.timingUnavailable && restricted.contentHeld
                    && hypha::live_blind_ui::screenFor (restricted, true, 1.0f).recovery != hypha::live_compare_ui::chainDelayNotice(),
                         "the clock's POST reason is told before the content notice");
                LiveTimingFixtureAccess::timingObservation (post, RecoveryReason::none, authority);
                ++cases;
            }
            juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
            Processor pre (Processor::Role::Pre);
            require (! pre.liveCompareStatus().timingUnavailable, "PRE gains no comparison restriction from its wrapper");
        }
    return cases;
}
