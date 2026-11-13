#include "../src/PluginProcessor.h"
#include "LiveTimingFixtureAccess.h"
#include "ReferenceValidationStorageSandbox.h"
#include "LiveTimingAvailabilityTest.h"
#include "TestProcessorStorage.h"
#include "AutoAlignStateProductTest.h"
#include <cstdlib>
#include <iostream>

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "Content timing: " << message << '\n'; std::exit (1); }
}

int main()
{
    ReferenceValidationStorageSandbox sandbox;
    hypha::tests::editor_product::ScopedChainTimingStorage preferences (sandbox.directory());
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
        verifyAutoAlignState (wrapper, require);
        juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
        Processor post (Processor::Role::Post);
        auto layout = post.getBusesLayout();
        layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
        layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
        require (post.setBusesLayout (layout), "each wrapper negotiates stereo, not an unknown AAX multi-mono group");
        hypha::tests::prepareFixtureProcessor (post, 48000, 512);
        LiveTimingFixtureAccess::heldGains (post);
        auto estimate = LiveTimingFixtureAccess::noticeContent (post);
        require (post.liveCompareStatus().contentHeld, "strong content change raises the timing notice");
        const auto start = post.beginLiveBlind();
        if (start != StartResult::returnRequired)
            std::cerr << "wrapper=" << int (wrapper) << " admission=" << int (start)
                      << " supported=" << post.liveCompareSupported() << "," << post.localBlindProductSupported()
                      << " held=" << post.liveCompareStatus().postTarget << "\n";
        require (start == StartResult::returnRequired,
                 "an ended session's held attenuation needs RETURN; content timing is not an entry refusal");
        require (post.liveBlindStatus().stage == BlindStage::idle, "refusal never starts an anonymous preparation");
        auto facts = LiveTimingFixtureAccess::jumpFacts (post);
        require (facts.lagFrames == 96 && std::abs (facts.peak - 0.4) < 1.0e-12 && std::abs (facts.dominance - 20.0) < 1.0e-12
            && facts.clockK == 4096 && facts.blockFrames == 512, "the notice retains the full diagnostic evidence");
        post.kirinHostTimelineReset (true, true);
        require (post.liveCompareStatus().contentHeld, "playing seek cannot end the content notice");
        post.kirinHostTimelineReset (false, false);
        require (post.liveCompareStatus().contentHeld, "missing transport cannot end the content notice");
        const auto run = post.liveComparePlaybackRun();
        // No processBlock call, and no named shared-memory fixture: the actual host notification
        // ends the notice even when the AU has no stopped render callbacks.
        post.kirinHostTimelineReset (true, false);
        require (! post.liveCompareStatus().contentHeld && ! post.isPlaying() && post.liveComparePlaybackRun() != run,
                 "explicit stopped transport ends the notice and its monitoring run without render");
        require (! post.noticeLiveCompareContentTiming (estimate) && ! post.liveCompareStatus().contentHeld,
                 "an estimate sampled before stop cannot resurrect the notice");
        auto status = post.liveCompareStatus();
        require (! status.matched && ! status.matchReady && status.gain == 0.75f
            && status.postTarget == 0.5f && status.postActual == 0.5f,
                 "stop revokes timing receipts while retaining the approved gains and attenuation");
        for (const auto phase : { BlindStage::preparing, BlindStage::settling, BlindStage::approval })
        {
            estimate = LiveTimingFixtureAccess::noticeContent (post);
            LiveTimingFixtureAccess::blindWaiting (post, phase);
            post.serviceLiveBlind();
            const auto blind = post.liveBlindStatus();
            require (blind.stage == phase && blind.reason != RecoveryReason::contentChanged,
                     "a content notice keeps the preparation stage instead of terminating it");
            require (post.liveCompareStatus().postTarget == 0.5f, "failed preparation never raises POST");
            post.kirinHostTimelineReset (true, false);
            require (post.liveBlindStatus().stage == phase,
                     "stop retains the same preparation request");
        }
        juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
        Processor pre (Processor::Role::Pre);
        const auto preRun = pre.liveComparePlaybackRun();
        pre.kirinHostTimelineReset (true, false);
        require (pre.liveComparePlaybackRun() == preRun && ! pre.liveCompareStatus().contentHeld,
                 "PRE receives no POST notice/output authority from reset notifications");
        ++cases;
    }
    ContentHold hold;
    const auto stale = hold.generation(); hold.hold (stale); hold.stopped(); hold.hold (stale);
    require (! hold.held(), "a late notice publication cannot race past the stopped generation");
    hold.hold (hold.generation()); require (hold.held(), "a new run can notice a new real change");
    std::cout << "Content timing: PASS " << cases << " wrapper cases + stale-publication boundary; "
              << availabilityCases << " availability cases\n";
}
