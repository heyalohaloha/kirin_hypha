#include "../src/PluginProcessor.h"
#include "LiveTimingFixtureAccess.h"
#include "ReferenceValidationStorageSandbox.h"
#include "LiveTimingAvailabilityTest.h"
#include <cstdlib>
#include <iostream>

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Content timing: " << message << '\n'; std::exit (1); }
}

int main()
{
    ReferenceValidationStorageSandbox sandbox;
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
    juce::ScopedJuceInitialiser_GUI initialise;
    const auto availabilityCases = verifyTimingAvailability (require);
    using Processor = KirinHyphaProcessorBase;
    using namespace hypha::live_compare;
    int cases = 0;
    for (auto wrapper : { juce::AudioProcessor::wrapperType_AudioUnit,
                          juce::AudioProcessor::wrapperType_VST3,
                          juce::AudioProcessor::wrapperType_AAX })
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
        Processor post (Processor::Role::Post);
        LiveTimingFixtureAccess::heldGains (post);
        auto estimate = LiveTimingFixtureAccess::holdContent (post);
        require (post.liveCompareStatus().contentHeld, "strong content change holds POST");
        require (post.beginLiveBlind() == StartResult::contentHeld,
                 "held BLIND is refused immediately with a distinct recoverable reason");
        require (post.liveBlindStatus().stage == BlindStage::idle, "refusal never starts an anonymous preparation");
        auto facts = LiveTimingFixtureAccess::jumpFacts (post);
        require (facts.lagFrames == 96 && std::abs (facts.peak - 0.4) < 1.0e-12 && std::abs (facts.dominance - 20.0) < 1.0e-12
            && facts.clockK == 4096 && facts.blockFrames == 512, "hold retains the full diagnostic evidence");
        post.kirinHostTimelineReset (true, true);
        require (post.liveCompareStatus().contentHeld, "playing seek cannot release content hold");
        post.kirinHostTimelineReset (false, false);
        require (post.liveCompareStatus().contentHeld, "missing transport cannot release content hold");
        const auto run = post.liveComparePlaybackRun();
        // No processBlock call, and no named shared-memory fixture: the actual host notification
        // releases hold even when the AU has no stopped render callbacks.
        post.kirinHostTimelineReset (true, false);
        require (! post.liveCompareStatus().contentHeld && ! post.isPlaying() && post.liveComparePlaybackRun() != run,
                 "explicit stopped transport ends the hold and its monitoring run without render");
        require (! post.holdLiveCompareForContentJump (estimate) && ! post.liveCompareStatus().contentHeld,
                 "an estimate sampled before stop cannot resurrect hold");
        auto status = post.liveCompareStatus();
        require (! status.matched && ! status.matchReady && status.gain == 0.75f
            && status.postTarget == 0.5f && status.postActual == 0.5f,
                 "stop revokes timing receipts while retaining the approved gains and attenuation");
        for (const auto phase : { BlindStage::preparing, BlindStage::settling, BlindStage::approval })
        {
            estimate = LiveTimingFixtureAccess::holdContent (post);
            LiveTimingFixtureAccess::blindWaiting (post, phase);
            post.serviceLiveBlind();
            const auto blind = post.liveBlindStatus();
            require (blind.stage == BlindStage::invalidated && blind.reason == RecoveryReason::contentChanged,
                     "hold while preparing/settling/approving terminates immediately with its reason");
            require (post.liveCompareStatus().postTarget == 0.5f, "failed preparation never raises POST");
            post.kirinHostTimelineReset (true, false);
            require (post.liveBlindStatus().stage == BlindStage::invalidated,
                     "stop does not silently restart an invalidated anonymous trial");
        }
        juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
        Processor pre (Processor::Role::Pre);
        const auto preRun = pre.liveComparePlaybackRun();
        pre.kirinHostTimelineReset (true, false);
        require (pre.liveComparePlaybackRun() == preRun && ! pre.liveCompareStatus().contentHeld,
                 "PRE receives no POST hold/output authority from reset notifications");
        ++cases;
    }
    ContentHold hold;
    const auto stale = hold.generation(); hold.hold (stale); hold.stopped(); hold.hold (stale);
    require (! hold.held(), "a late hold publication cannot race past the stopped generation");
    hold.hold (hold.generation()); require (hold.held(), "a new run can hold for a new real change");
    std::cout << "Content timing: PASS " << cases << " wrapper cases + stale-publication boundary; "
              << availabilityCases << " availability cases\n";
}
