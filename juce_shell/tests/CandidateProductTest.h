#pragma once
#include "CandidateProductHost.h"
#include "CandidateAuditTest.h"
#include "CandidateKeysTest.h"

namespace hypha::tests
{
template<class Require> void verifyCandidateProduct (Require require, const juce::File& storage)
{
    using namespace live_compare;
    writeCandidateFixtureIdentity (require, storage);
    verifyCandidateSurfaces (require);
    verifyCandidateAudit (require);
    ProductTestClock wall;
    verifyCandidateKeys (wall, require);
    for (const auto [channels, block] : { std::pair<int, int> { 1, 256 }, { 2, 512 }, { 2, 1024 } })
    {
        std::cout << "Candidate product: " << channels << " channels, " << block << " frames\n" << std::flush;
        const i18n::ScopedLanguage language (channels == 1 ? i18n::Language::english : i18n::Language::japanese);
        CandidateHost<Require> host (wall, require, channels, block, block == 1024);
        host.pair();
        auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), false) == StartResult::notReady,
                 "placement confirmation precedes replacement");
        host.requireKeep (post, "ordinary Keep starts through its product API");
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::comparisonBusy,
                 "A/B cannot start over a real Keep transaction");
        for (int tick = 0; tick < 24 && !post.isRecording(); ++tick)
        {
            host.render (1, false, false);
            for (auto& p : host.instances) awaitAnalysisExchange (*p, true);
        }
        require (post.isRecording(), "the ordinary Keep reaches actual Record through its workers");
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::comparisonBusy
            && post.isRecording(), "A/B refuses active Record without ending or replacing it");
        post.stopPair();
        host.render (20, false, false);
        for (auto& p : host.instances) awaitAnalysisExchange (*p, true);
        juce::MemoryBlock beforeStart;
        if (channels == 1)
        {
            require (post.startLiveCompare() == StartResult::started, "prepare an ordinary held-level comparison");
            host.render (100, false, false);
            MatchResult level; level.failure = MatchFailure::none; level.measuredDb = 8.0;
            level.prePeakDbtp = -1.0; level.postPeakDbtp = -4.0; level.ceilingDbtp = -1.0;
            require (post.applyLiveCompareMatch (planMatch (level, 0.0), MatchChoice::lowerPost),
                     "fixture explicitly approves the existing POST attenuation");
            host.render (30, false, false); post.stopLiveCompare(); host.render (10, false, false);
            enterCandidateThroughEditor (post, require, [&] (juce::AudioProcessorEditor& editor)
            {
                require (!post.candidateCompareActive(), "confirmed A/B waits for the existing RETURN");
                for (int i = 0; i < 450 && !post.candidateCompareActive(); ++i) // RETURN rises over 2 s
                { host.render (1, false, false); candidateTick (editor); }
            }, &beforeStart);
        }
        else enterCandidateThroughEditor (post, require, {}, &beforeStart);
        require (post.pairedPreInstanceId() == host.pre1().instanceId(), "normal pair is untouched");
        require (post.candidateTrackMismatch(), "host track names advise without blocking the confirmed entry");
        juce::MemoryBlock saved;
        post.getStateInformation (saved);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
        require (xml && xml->getStringAttribute ("paired_pre_instance_id") == host.pre1().instanceId()
            && saved == beforeStart, "saved state is identical before and during A/B, including the ordinary pair");
        require (! post.keepPair() && ! post.keepAll(), "Record / Keep cannot capture comparison input");
        require (post.requestLocalBlindProductCapture (meter_context::MeterContext::twoMix)
            != local_blind::CaptureAdmission::ready, "capture cannot consume comparison input");
        host.wholeOutput = true;
        host.auditTransition = true; host.render (180, true, false); host.auditTransition = false;
        require (! post.measureLiveCompare().ok(), "transition and downstream history are outside MATCH");
        post.selectLiveComparePre (true);
        host.render (180, true, true);
        require (host.replacementBlocks > 100 && host.matchedBlocks > 100, "both paths actually rendered");
        const auto beforeOverlap = host.replacementBlocks;
        host.overlapLease = true; host.render (20, true, true); host.overlapLease = false;
        host.heartbeatAhead = true; host.render (20, true, true); host.heartbeatAhead = false;
        require (host.replacementBlocks == beforeOverlap + 40,
                 "heartbeat writer overlap and a newer heartbeat never interrupt the actual PRE2 callback");
        host.render (1500, true, true);
        require (post.candidateMatchSecondsRemaining() == 0, "MATCH countdown reaches zero from verified accumulation");
        host.clocks[0].available = false;
        host.render (1, true, true); // lost source clock, no skipped PCM or warm-up interval
        host.clocks[0].available = true;
        host.render (150, true, true);
        for (auto& clock : host.clocks) clock.offset -= 4096;
        host.render (150, true, true);
        host.delayB += 31; host.render (150, true, true);
        for (auto& clock : host.clocks) clock.playing = false;
        host.render (1, true, true);
        for (auto& clock : host.clocks) clock.playing = true;
        host.render (1700, true, true);
        host.wholeOutput = false;
        verifyCandidateTrace (host, require, storage);
        const auto measured = post.measureLiveCompare();
        require (measured.ok(), "settled independent effects supply MATCH's real four-second window");
        require (post.applyLiveCompareMatch (planMatch (measured, 0.0), MatchChoice::basis), "candidate MATCH uses LIVE");
        host.render (100, true, true);
        require (post.followLiveCompareGain (measured.measuredDb + 0.5), "the approved AUTO step moves only the LIVE gain");
        host.render (100, true, true);
        require (post.beginLiveBlind() == StartResult::started, "A/B enters the existing LIVE BLIND");
        host.render (500, true, false);
        require (post.liveBlindStatus().stage == BlindStage::active, "candidate BLIND actually starts");
        const auto epoch = post.liveBlindStatus().trial.epoch;
        const auto gain = post.liveCompareStatus().gain;
        require (post.selectLiveBlind (2), "second anonymous source selected");
        host.render (30, true, false, false);
        require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(), "both actual sources enable REVEAL");
        for (auto& clock : host.clocks) clock.playing = false;
        host.render (1, true, false, false);
        require (post.liveBlindStatus().stage == BlindStage::active && post.liveBlindStatus().trial.epoch == epoch,
                 "one stopped block preserves BLIND and its assignment");
        for (auto& clock : host.clocks) clock.playing = true;
        host.render (100, true, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted
            && candidateIdentical (post.liveCompareStatus().gain, gain), "fresh clocks resume the fixed gain");
        for (auto& clock : host.clocks) clock.available = false;
        host.render (1, true, false, false);
        require (post.liveBlindStatus().stage == BlindStage::active && post.liveBlindStatus().trial.epoch == epoch,
                 "one missing project clock preserves the anonymous trial");
        for (auto& clock : host.clocks) clock.available = true;
        host.render (100, true, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted, "fresh project clocks restore the selected source");
        for (auto& clock : host.clocks) clock.offset -= 6000;
        host.render (100, true, false);
        require (post.liveBlindStatus().trial.epoch == epoch, "seek keeps the same trial");
        host.delayB += 127;
        host.render (150, true, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted, "changed B delay is proved again");
        host.delayA += 79;
        host.render (150, true, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted
            && post.liveBlindStatus().trial.epoch == epoch, "changed A delay resumes both readers and the same BLIND");
        for (auto& clock : host.clocks) { clock.offset = 0; clock.loopLength = 24000; }
        const auto beforeLoops = host.replacementBlocks;
        host.render (250, true, false);
        if (host.aax) require (host.replacementBlocks > beforeLoops + 100, "loop PCM resumes actual PRE2 replacement");
        else require (post.liveCompareStatus().verdict != Verdict::accepted,
                      "a generic host without a loop certificate never guesses an ambiguous lap");
        require (post.liveBlindStatus().trial.epoch == epoch, "loop retains anonymous assignment");
        for (auto& clock : host.clocks) clock.loopLength = 0;
        host.render (100, true, false);
        require (post.candidateMeasurementIsolated(), "normal measurement remains isolated through interruptions");
        host.auditTransition = true; host.lastWeight = 1.0f;
        post.finishLiveCompare();
        host.render (10, true, false, false);
        host.auditTransition = false;
        std::cout << "Candidate product: END queues another entry\n" << std::flush;
        for (auto& clock : host.clocks) clock.playing = false;
        host.render (1, false, false);
        enterCandidateThroughEditor (post, require, [&] (juce::AudioProcessorEditor& editor)
        {
            require (post.candidateReturning() && !post.candidateCompareActive(),
                     "END keeps the next confirmed entry pending while measurement is isolated");
            require ((post.referenceOwnOutputStates() & output_owner::bit (output_owner::State::localBlind)) != 0,
                     "the ended candidate BLIND still owns its capture scope during return");
            wall.advance (std::chrono::seconds (12));
            expireCandidateEntryDeadline (editor); // deterministic editor timeout, with the host stopped
            candidateTick (editor);
            require (!post.candidateCompareActive(), "no audio callback fabricates a completed return");
            require (candidateEntryPending (editor), "a stopped host preserves the next confirmed entry past its deadline");
            for (auto& clock : host.clocks) clock.playing = true;
            for (int tick = 0; tick < 850 && !post.candidateCompareActive(); ++tick)
            {
                host.render (1, false, false); candidateTick (editor);
                if (!post.candidateCompareActive() && !candidateEntryPending (editor))
                { std::cerr << "candidate entry cleared on tick " << tick << '\n'; break; }
            }
        });
        std::cout << "Candidate product: queued entry resumed\n" << std::flush;
        host.render (100, true, false);
        finishCandidateWithoutClock (host, require); host.render (10, true, false, false);
        host.render (800, false, false);
        require (! post.candidateMeasurementIsolated(), "END releases normal measurement after the return history");
        require (post.pairedPreInstanceId() == host.pre1().instanceId(), "END retains the original pair");
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "another explicit comparison starts");
        if (block == 512)
        {
            host.changeFormat (host.pre1());
            host.render (10, true, false, false); host.render (410, false, false);
            require (!post.candidateMeasurementIsolated() && !post.liveCompareStatus().active,
                     "a new PRE1 owner before receiver preparation ends the old permission");
            require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                     "the new PRE1 owner needs a fresh explicit comparison");
        }
        if (channels == 2) host.inputScale = 20.0f;
        const auto beforeHighInput = host.replacementBlocks;
        host.render (100, true, false);
        require (host.replacementBlocks > beforeHighInput + 50, "unity source transfer also accepts finite samples above full scale");
        host.postEnabled = false;
        host.render (130, true, false, false);
        host.render (10, false, false);
        host.postEnabled = true;
        std::cout << "Candidate product: offline and bypass boundaries\n" << std::flush;
        host.render (120, true, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted, "a stalled POST resumes without a second click");
        host.pre2().setNonRealtime (true); post.setNonRealtime (true);
        host.render (2, false, false);
        host.pre2().setNonRealtime (false); post.setNonRealtime (false);
        host.render (120, true, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted, "offline blocks cannot substitute and realtime resumes");
        host.pre2().getBypassParameter()->setValue (1.0f);
        host.render (1, true, false, false); // the short fade out of x
        host.render (192000 / block, false, false);
        verifyCandidateRecovery (post, require, true);
        host.pre2().getBypassParameter()->setValue (0.0f);
        host.render (120, true, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted, "PRE2 bypass resumes the same selected comparison");
        host.clocks[0].available = false; host.render (192000 / block, true, false, false);
        verifyCandidateRecovery (post, require, false);
        host.clocks[0].available = true; host.render (120, true, false);
        post.getBypassParameter()->setValue (1.0f);
        host.render (5, true, false, false); host.render (2, false, false);
        post.getBypassParameter()->setValue (0.0f);
        host.render (120, true, false);
        require (post.liveCompareStatus().verdict == Verdict::accepted, "POST bypass releases PRE2 and resumes the same selection");
        if (channels == 1)
        {
            std::cout << "Candidate product: PRE2 reprepare\n" << std::flush;
            host.pre2().releaseResources();
            prepareFixtureProcessor (host.pre2(), 48000, host.block);
            host.render (10, true, false);
            require (post.candidateCompareActive(), "same-format prepare preserves the explicit comparison");
            host.changeFormat (host.pre2());
            host.render (820, false, false);
            require (!post.candidateMeasurementIsolated(), "format reprepare ends the old scope");
            require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                     "a fresh explicit request can use the prepared format");
            host.render (100, true, false);
            host.instances[1].reset();
            host.render (820, false, false);
            require (!post.candidateMeasurementIsolated() && !post.liveCompareStatus().active,
                     "removing PRE2 returns POST to the ordinary chain");
        }
        else
        {
            post.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            host.render (10, true, false, false); host.render (810, false, false);
            require (!post.candidateMeasurementIsolated() && !post.liveCompareStatus().active,
                     "restoring saved state never restarts A/B");
            require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                     "normal pair remains available after restore");
            host.render (100, true, false);
            if (block == 1024)
            {
                host.changeFormat (post);
                host.render (10, true, false, false); host.render (810, false, false);
                require (!post.candidateMeasurementIsolated() && !post.liveCompareStatus().active,
                         "POST format reprepare ends the old comparison authority");
                require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                         "POST reprepare requires another explicit start");
                host.render (100, true, false);
            }
            host.instances[2].reset();
            host.render (130, true, false, false);
            host.render (10, false, false);
        }
    }
}
}
