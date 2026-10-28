#pragma once
#include "ReferenceControllerTestAccess.h"
#include "reference_runtime_v2_analysis_test_support.h"
#include "reference_runtime_v2_refresh_test_support.h"
#include "ReferenceLiveAuthorityFixture.h"

#include <functional>

namespace
{
    void verifyPublicationChangeRevokesGain (
        ref::RuntimeV2Controller& controller,
        std::function<void()>& beforeGateActivation,
        std::atomic<bool>& comparisonSuspended,
        const juce::File& presetFile,
        const juce::File& manifestFile,
        juce::var measuredPreset,
        const juce::String& presetId,
        const juce::String& revisionId)
    {
        verifyPlaybackIdentityDependencies();
        require (controller.selectB (-15.0, -2.0), "refresh fixture must enter gain-matched B");
        juce::AudioBuffer<float> output (2, 256);
        verifyUnrelatedPublicationPreservesPlayback (
            controller, presetFile, manifestFile, measuredPreset, presetId, revisionId, 128, output);
        controller.selectA();
        for (int i = 0; i < 4; ++i) { output.clear(); controller.renderSelectedB (output, 128, true); }
        reference_wait_diagnostic::await ("normal_fade_gate_release", controller, [&] { return reference_wait_diagnostic::all ({{ "gateReleased", !comparisonSuspended.load() }}); });
        require (!comparisonSuspended.load(), "completed normal fade releases the gate before testing a fresh acquisition");
        measuredPreset = measuredPreset.clone();
        const auto nextRevision = controller.snapshot().manifestRevision + 1;
        measuredPreset.getDynamicObject()->getProperty ("checks").getArray()
            ->getReference (0).getDynamicObject()->setProperty (
                "comparison_mode", "original");
        std::atomic<bool> replacementPublished { false };
        beforeGateActivation = [&]
        {
            replacementPublished.store (
                writeJson (presetFile, measuredPreset)
                && writeJson (manifestFile,
                              makeRuntimeV2Manifest (
                                  presetId, revisionId, presetFile, nextRevision)));
            reference_wait_diagnostic::await ("gate_activation_new_publication", controller, [&] {
                const auto state = controller.snapshot(); return reference_wait_diagnostic::all ({{ "nextRevision", state.manifestRevision == nextRevision }, { "original", state.comparisonMode == "original" }});
            });
        };
        require (! controller.selectB (-15.0, -2.0),
                 "a B activation must fail when its approved publication changes inside the output gate");
        beforeGateActivation = {};
        auto runtime = controller.snapshot();
        require (replacementPublished.load()
                 && runtime.manifestRevision == nextRevision
                 && runtime.comparisonMode == "original"
                 && ! runtime.bSelected && ! comparisonSuspended.load(),
                 "publication change must revoke both the stale gain and the temporary output gate");
        require (controller.selectB (-11.0, -2.0),
                 "the replacement original-mode publication must remain selectable");
        runtime = controller.snapshot();
        require (std::abs (runtime.appliedGainDb) < 1.0e-9,
                 "replacement original mode must not inherit the previous loudness-match gain");
        controller.suspendAudition();
        require (! controller.snapshot().bSelected && ! comparisonSuspended.load(),
                 "automatic suspension must release a normal-B reservation");
    }

    void verifyBlindSourceReplacementReturn (
        ref::RuntimeV2Controller& controller,
        std::function<void()>& beforeGateActivation,
        std::atomic<bool>& comparisonSuspended,
        const juce::File& v2Root,
        const juce::File& presetFile,
        const juce::File& manifestFile,
        const juce::File& alignedBFile,
        const juce::String& alignedBFileHash,
        const juce::String& alignedBPcmHash,
        const juce::String& recordingId,
        const juce::String& presetId,
        const juce::String& revisionId,
        std::int64_t alignedBFrames,
        const ref::RuntimeACaptureAudio& liveA,
        std::int64_t hostPosition,
        juce::AudioBuffer<float>& output)
    {
        for (int i = 0; i < 4; ++i) { output.clear(); controller.renderSelectedB (output, hostPosition, true); }
        namespace control = hypha::tests::reference_control;
        require (control::waitUntil (std::chrono::steady_clock::now() + std::chrono::minutes (5),
                     [&] { return ! comparisonSuspended.load(); }), "normal return releases its gate before the worker watchdog");
        require (!comparisonSuspended.load(), "normal return completes before the new Blind acquisition race");
        const auto initial = controller.snapshot();
        const auto gateRaceRevision = initial.manifestRevision + 1;
        const juce::String gateRaceVersionId =
            "cdcdcdcd-cdcd-4dcd-8dcd-cdcdcdcdcdcd";
        const auto gateRaceSourceReceipt = stageRuntimeV2Artifact (
            v2Root, "sources", makeRuntimeV2WorkVersionSource (
                alignedBFile, alignedBFileHash, alignedBPcmHash, recordingId,
                gateRaceVersionId, alignedBFrames));
        const auto gateRacePreset = bindRuntimeV2WorkVersionPresetToSource (
            presetId, revisionId, gateRaceSourceReceipt, alignedBFileHash,
            alignedBPcmHash, recordingId, gateRaceVersionId, alignedBFrames);
        const juce::String replacementVersionId = "efefefef-efef-4fef-8fef-efefefefefef";
        const auto replacementSourceReceipt = stageRuntimeV2Artifact (
            v2Root, "sources", makeRuntimeV2WorkVersionSource (
                alignedBFile, alignedBFileHash, alignedBPcmHash, recordingId, replacementVersionId, alignedBFrames));
        const auto replacementPreset = bindRuntimeV2WorkVersionPresetToSource (
            presetId, revisionId, replacementSourceReceipt, alignedBFileHash,
            alignedBPcmHash, recordingId, replacementVersionId, alignedBFrames);
        const ref::RuntimeIdentity identity { runtimeId, workId, 42 };
        resetForFreshBlindCapture (controller, identity, initial);
        feedFreshBlindCapture (controller, v2Root, identity, liveA, "transaction_reset");
        controller.observeTransport (hostPosition, true, true);
        RefreshTransport transport (controller, hostPosition);
        const auto old = control::publicationFactsFor (controller, {});
        const auto completedPcm = control::captureFor (controller, {}).currentAudio();
        require (completedPcm != nullptr, "the gate race retains the actual completed PCM before its hook");
        const auto completedRevision = completedPcm->dawRevisionId, completedHash = completedPcm->cuePcmSha256;
        const auto requireSamePcm = [&] {
            const auto current = control::captureFor (controller, {}).currentAudio();
            if (current != completedPcm)
            {
                const auto reset = ref::referenceTestCaptureResetFacts (&control::captureFor (controller, {}));
                std::cerr << "BLIND_GATE_PCM_LOST present=" << (current != nullptr)
                          << " resetReason=" << reset.reason << " resetAt=" << reset.atMs << " resetCount=" << reset.count << '\n';
            }
            require (current && current == completedPcm && current->dawRevisionId == completedRevision
                && current->cuePcmSha256 == completedHash, "the race may never lose or replace its original completed PCM");
        };
        require (old.ready && old.snapshot.blindEligible && old.snapshot.blindLowerAApprovalRequired && ! comparisonSuspended.load()
                     && liveBlindCaptureCurrent (controller, v2Root, identity), "the race starts with real current authority, PCM and an unheld gate");
        const auto heldAGain = std::pow (
            10.0f, static_cast<float> (-old.snapshot.blindRequiredAAttenuationDb / 20.0));
        std::atomic<bool> gateRacePublished { false };
        bool hookReached = false, newPublicationObserved = false, refreshConsumedAtWait = false;
        std::uint64_t newEpoch = 0, refreshTicket = 0;
        juce::String newIdentity;
        std::chrono::steady_clock::time_point hookStarted, writeCompleted, waitCompleted;
        beforeGateActivation = [&]
        {
            hookStarted = std::chrono::steady_clock::now();
            hookReached = true;
            gateRacePublished.store (
                writeJson (presetFile, gateRacePreset)
                && writeJson (manifestFile, makeRuntimeV2Manifest (
                    presetId, revisionId, presetFile, gateRaceRevision)));
            require (gateRacePublished.load(), "the gate race actually publishes both disposable fixture files");
            writeCompleted = std::chrono::steady_clock::now();
            refreshTicket = ref::requestReferenceTestWorkspaceRefresh (&controller);
            require (refreshTicket != 0, "one test-only worker read request is accepted after the actual publication");
            newPublicationObserved = control::waitUntil (std::chrono::steady_clock::now() + std::chrono::minutes (5), [&] {
                requireSamePcm();
                if (! ref::referenceTestWorkspaceRefreshConsumed (refreshTicket)) return false;
                const auto current = control::publicationFactsFor (controller, {});
                if (! current.ready || current.snapshot.manifestRevision != gateRaceRevision
                    || current.snapshot.playbackIdentity == old.snapshot.playbackIdentity || current.epoch == old.epoch) return false;
                newEpoch = current.epoch; newIdentity = current.snapshot.playbackIdentity; return true;
            });
            waitCompleted = std::chrono::steady_clock::now();
            refreshConsumedAtWait = ref::referenceTestWorkspaceRefreshConsumed (refreshTicket);
            if (! newPublicationObserved) ref::cancelReferenceTestWorkspaceRefresh (&controller, refreshTicket);
        };
        const auto staleStarted = controller.approveBlindLowerAAndStart (-14.0, -6.0);
        beforeGateActivation = {};
        if (! hookReached || ! gateRacePublished.load() || ! newPublicationObserved || staleStarted)
        {
            const auto current = control::publicationFactsFor (controller, {});
            const auto authority = juce::JSON::parse (ref::RuntimeABindingRepository (v2Root).bindingFile (runtimeId));
            const auto elapsedMs = [] (auto start, auto end) { return std::chrono::duration_cast<std::chrono::milliseconds> (end - start).count(); };
            std::cerr << "BLIND_GATE_RACE hook=" << hookReached << " published=" << gateRacePublished.load()
                      << " observed=" << newPublicationObserved << " started=" << staleStarted << " gateHeld=" << comparisonSuspended.load()
                      << " refreshTicket=" << refreshTicket << " refreshConsumed=" << refreshConsumedAtWait
                      << " writeMs=" << elapsedMs (hookStarted, writeCompleted) << " waitMs=" << elapsedMs (writeCompleted, waitCompleted)
                      << " oldEpoch=" << old.epoch << " epoch=" << current.epoch << " ready=" << current.ready
                      << " expectedRevision=" << gateRaceRevision << " revision=" << current.snapshot.manifestRevision
                      << " identityChanged=" << (current.snapshot.playbackIdentity != old.snapshot.playbackIdentity)
                      << " eligible=" << current.snapshot.blindEligible << " capture=" << current.snapshot.aCaptureAvailable
                      << " binding=" << current.snapshot.aBindingAvailable << " rejection=" << current.snapshot.rejectionCode
                      << " now=" << juce::Time::currentTimeMillis()
                      << " issued=" << static_cast<juce::int64> (authority["issued_at_ms"])
                      << " expires=" << static_cast<juce::int64> (authority["lease_expires_at_ms"]) << '\n';
        }
        require (hookReached && gateRacePublished.load() && newPublicationObserved && ! staleStarted,
                 "Blind rejects a proven new publication and epoch inside the output gate before the worker watchdog");
        require (! comparisonSuspended.load(), "the rejected stale Blind start returns its gate before the command returns");
        requireSamePcm();
        LiveBlindCaptureFacts preparedCapture;
        const auto preparationStarted = std::chrono::steady_clock::now();
        const bool preparedNewEpoch = control::waitUntil (preparationStarted + std::chrono::minutes (5), [&] {
            requireSamePcm();
            const auto current = control::publicationFactsFor (controller, {});
            require (current.epoch == newEpoch && current.snapshot.manifestRevision == gateRaceRevision
                && current.snapshot.playbackIdentity == newIdentity, "a different later epoch cannot satisfy the race result");
            preparedCapture = liveBlindCaptureFacts (controller, v2Root, identity);
            requireSamePcm();
            return current.ready && current.snapshot.blindEligible && preparedCapture.current()
                && preparedCapture.revision == completedRevision && preparedCapture.pcmHash == completedHash;
        });
        if (! preparedNewEpoch)
        {
            const auto current = control::publicationFactsFor (controller, {});
            const auto reset = ref::referenceTestCaptureResetFacts (&control::captureFor (controller, {}));
            std::cerr << "BLIND_GATE_PREPARATION ready=" << current.ready << " eligible=" << current.snapshot.blindEligible
                      << " binding=" << current.snapshot.aBindingAvailable << " capture=" << current.snapshot.aCaptureAvailable
                      << " rejection=" << current.snapshot.rejectionCode << " epoch=" << current.epoch
                      << " expectedEpoch=" << newEpoch << " revision=" << current.snapshot.manifestRevision
                      << " expectedRevision=" << gateRaceRevision << " gateHeld=" << comparisonSuspended.load()
                      << " elapsedMs=" << std::chrono::duration_cast<std::chrono::milliseconds> (
                          std::chrono::steady_clock::now() - preparationStarted).count()
                      << " resetReason=" << reset.reason << " resetAt=" << reset.atMs << " resetCount=" << reset.count;
            printLiveBlindCaptureFacts (std::cerr, preparedCapture);
            std::cerr << '\n';
        }
        require (preparedNewEpoch,
                 "the rejected stale Blind start must return its gate and prepare only the new epoch");
        require (controller.approveBlindLowerAAndStart (-14.0, -6.0),
                 "explicit approval must start the current lower-A Blind context");
        require (control::publicationFactsFor (controller, {}).session.auditionEpoch == newEpoch,
                 "the explicit new Blind session belongs to the same captured publication epoch");
        output.clear();
        require (controller.renderSelectedB (output, hostPosition, true),
                 "the approved Blind context must reach one real audio callback");
        verifyUnrelatedPublicationPreservesPlayback (
            controller, presetFile, manifestFile, gateRacePreset, presetId, revisionId, hostPosition, output);
        const auto replacementRevision = controller.snapshot().manifestRevision + 1;

        require (writeJson (presetFile, replacementPreset)
                 && writeJson (manifestFile,
                               makeRuntimeV2Manifest (
                                   presetId, revisionId, presetFile, replacementRevision)),
                 "a replacement source must publish before invalidating active Blind");
        hypha::tests::reference_control::await ([&] {
            const auto state = controller.snapshot(); return state.manifestRevision == replacementRevision
                && state.blindPhase == ref::BlindPhase::invalidated;
        });
        const auto invalidated = controller.snapshot();
        require (invalidated.manifestRevision == replacementRevision
                 && invalidated.blindPhase == ref::BlindPhase::invalidated
                 && comparisonSuspended.load(),
                 "source replacement must invalidate Blind without releasing the lower-A gate");
        output.clear();
        output.setSample (0, 0, 0.5f);
        require (controller.renderSelectedB (output, hostPosition, true)
                 && std::abs (output.getSample (0, 0) - 0.5f * heldAGain) < 1.0e-6f,
                 "invalidated Blind must hold approved A attenuation across source replacement");
        controller.endBlind();
        require (comparisonSuspended.load(),
                 "the return request must retain the output gate until normal A is audible");
        output.clear();
        output.setSample (0, 0, 0.5f);
        require (controller.renderSelectedB (output, hostPosition, true)
                 && std::abs (output.getSample (0, 0) - 0.5f) < 1.0e-9f,
                 "the return callback must pass normal A unchanged");
        hypha::tests::reference_control::await ([&] { return ! (comparisonSuspended.load()); });
        require (! comparisonSuspended.load(),
                 "the worker must release the output gate only after consuming the return receipt");
    }
}
