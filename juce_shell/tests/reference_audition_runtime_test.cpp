#include "reference_runtime_v2_analysis_test_support.h"

void testRuntimeV2Workspace (const juce::File& sandbox);

int main()
{
    require (ref::safeId (workId), "Work UUID must be a safe ID");
    require (! ref::safeId ("../escape"), "path separators must be rejected");
    require (ref::safeUuid (preparationId), "preparation UUID must validate");

    const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getNonexistentChildFile ("hypha-reference-audition", {}, false);
    require (sandbox.createDirectory(), "sandbox directory must be created");
    testRuntimeEventTransport (sandbox);
    testRuntimeV2Blind (sandbox);
    testRuntimeACapture (sandbox.getChildFile ("plugin_data").getChildFile ("reference")
                                .getChildFile ("v2"));
    const auto source = sandbox.getChildFile ("version-a.wav");
    require (source.replaceWithText ("immutable measured source"), "source fixture must be written");
    const auto sourceHash = juce::SHA256 (source).toHexString();

    const auto root = sandbox.getChildFile ("plugin_data").getChildFile ("hypha_ab")
                             .getChildFile ("v1");
    const auto receiptFile = root.getChildFile ("sources").getChildFile ("pending.json");
    require (writeJson (receiptFile, makeSourceReceipt (source, sourceHash)),
             "receipt fixture must be written");
    const auto receiptHash = juce::SHA256 (receiptFile).toHexString();
    const auto finalReceipt = receiptFile.getSiblingFile (receiptHash + ".json");
    require (receiptFile.moveFileTo (finalReceipt), "receipt must use its content hash");
    const auto preparationFile = root.getChildFile ("preparations").getChildFile (workId + ".json");
    require (writeJson (preparationFile,
                        makePreparation (source, sourceHash, receiptHash, finalReceipt.getSize())),
             "preparation fixture must be written");

    ref::Repository repository (root);
    const auto loaded = repository.load (workId);
    require (loaded.accepted(), "exact preparation and receipt must load");
    require (loaded.receipt.matches (loaded.preparation), "source summary must match receipt");
    require (repository.verifySourceFile (loaded.receipt).isEmpty(),
             "exact immutable source must verify");
    require (source.replaceWithText ("changed source"), "source mutation must succeed");
    require (repository.verifySourceFile (loaded.receipt) == "source_changed",
             "changed source must fail closed");

    auto malformed = makePreparation (source, sourceHash, receiptHash, finalReceipt.getSize());
    malformed.getDynamicObject()->setProperty ("unexpected", true);
    require (writeJson (preparationFile, malformed), "malformed preparation must be written");
    const auto rejected = repository.load (workId);
    require (rejected.state == ref::LoadState::rejected
             && rejected.rejectionCode == "preparation_contract_rejected",
             "unknown preparation fields must reject");

    require (writeJson (preparationFile,
                        makePreparation (source, sourceHash, receiptHash, finalReceipt.getSize())),
             "valid preparation must be restored");
    const ref::RuntimeIdentity identity { runtimeId, workId, 42 };
    const auto now = static_cast<std::int64_t> (1'788'390'000'000);
    require (ref::writeCapability (root, identity, now), "capability must publish atomically");
    require (ref::writeAcknowledgement (root, identity,
                                        repository.load (workId).preparation,
                                        "source_changed", now),
             "rejected acknowledgement must publish atomically");
    const auto files = ref::runtimeFiles (root, identity);
    const auto capability = juce::JSON::parse (files.capability);
    const auto acknowledgement = juce::JSON::parse (files.acknowledgement);
    require (capability.getDynamicObject() != nullptr
             && hasExactKeys (*capability.getDynamicObject(), {
                 "format", "version", "runtime_instance_id", "host_process_id", "work_id",
                 "target_role", "preparation_protocol", "acknowledgement_protocol",
                 "observed_at_ms", "lease_expires_at_ms" }),
             "capability must match the exact OS schema");
    require (acknowledgement.getDynamicObject() != nullptr
             && hasExactKeys (*acknowledgement.getDynamicObject(), {
                 "format", "version", "runtime_instance_id", "host_process_id", "work_id",
                 "target_role", "preparation_id", "source_sha256_file", "receipt_status",
                 "rejection_code", "observed_at_ms", "lease_expires_at_ms" })
             && acknowledgement.getDynamicObject()->getProperty ("receipt_status") == "rejected"
             && acknowledgement.getDynamicObject()->getProperty ("rejection_code") == "source_changed",
             "acknowledgement must match the exact rejection schema");
    require (static_cast<std::int64_t> (
                 capability.getDynamicObject()->getProperty ("lease_expires_at_ms")) - now <= 10'000,
             "runtime lease must remain bounded");
    ref::removeRuntimeFiles (files);
    require (! files.capability.exists() && ! files.acknowledgement.exists(),
             "runtime owner must remove its own lease files");

    const auto recoveryRoot = sandbox.getChildFile ("plugin_data").getChildFile ("reference")
                                     .getChildFile ("v2");
    const juce::String recordingId = "22222222-2222-4222-8222-222222222222";
    ref::RuntimeABindingRepository aBindingRepository (recoveryRoot);
    const auto aBindingFile = aBindingRepository.bindingFile (runtimeId);
    require (writeJson (aBindingFile,
                        makeRuntimeABinding (identity, recordingId, now, now + 3'000)),
             "approved A binding fixture must be written");
    const auto aBinding = aBindingRepository.load (identity, now + 1);
    require (aBinding.has_value()
             && aBinding->recordingId == recordingId
             && aBinding->bindingId == "99999999-9999-4999-8999-999999999999",
             "Hypha must accept the exact live nine-field A binding");
    auto expandedABinding = makeRuntimeABinding (identity, recordingId, now, now + 3'000);
    expandedABinding.getDynamicObject()->setProperty ("version_id", preparationId);
    require (writeJson (aBindingFile, expandedABinding)
             && ! aBindingRepository.load (identity, now + 1).has_value(),
             "A binding must not claim a Work Version or accept expanded fields");
    require (writeJson (aBindingFile,
                        makeRuntimeABinding (identity, recordingId, now, now + 3'000)),
             "valid A binding fixture must be restored");
    ref::RecoveryTransport recovery (recoveryRoot);
    const ref::RecoveryAuthority recoveryAuthority { runtimeId, 42, workId };
    const ref::RecoveryContext recoveryContext {
        "55555555-5555-4555-8555-555555555555",
        "66666666-6666-4666-8666-666666666666",
        "77777777-7777-4777-8777-777777777777",
    };
    const auto recoveryRequest = recovery.writeRequest (
        recoveryAuthority, ref::RecoveryDestination::candidateSource,
        recoveryContext, now, requestId);
    require (recoveryRequest.has_value(), "exact Recovery request must write atomically");
    const auto requestJson = juce::JSON::parse (recovery.requestFile (runtimeId));
    require (requestJson.getDynamicObject() != nullptr
             && hasExactKeys (*requestJson.getDynamicObject(), {
                 "format", "version", "request_id", "runtime_instance_id",
                 "host_process_id", "work_id", "destination", "context",
                 "requested_at_ms" })
             && recovery.requestFile (runtimeId).getSize() <= ref::maximumRecoveryRequestBytes,
             "Recovery request must match the approved nine-field bounded schema");
    auto recoveryAck = new juce::DynamicObject();
    recoveryAck->setProperty ("format", "kirin_hypha_reference_recovery_acknowledgement");
    recoveryAck->setProperty ("version", "1.0");
    recoveryAck->setProperty ("request_id", requestId);
    recoveryAck->setProperty ("runtime_instance_id", runtimeId);
    recoveryAck->setProperty ("host_process_id", static_cast<juce::int64> (42));
    recoveryAck->setProperty ("outcome", "safe_fallback_opened");
    recoveryAck->setProperty ("handled_at_ms", now + 1);
    juce::var recoveryAckValue (recoveryAck);
    require (writeJson (recovery.acknowledgementFile (runtimeId), recoveryAckValue),
             "Recovery acknowledgement fixture must be written");
    const auto acceptedRecovery = recovery.loadAcknowledgement (*recoveryRequest);
    require (acceptedRecovery.has_value()
             && acceptedRecovery->outcome == ref::RecoveryOutcome::safeFallbackOpened,
             "exact Candidate request may accept a safe Reference fallback");
    recoveryAck->setProperty ("request_id", "88888888-8888-4888-8888-888888888888");
    require (writeJson (recovery.acknowledgementFile (runtimeId), recoveryAckValue)
             && ! recovery.loadAcknowledgement (*recoveryRequest).has_value(),
             "acknowledgement for a different request must remain silent");
    require (! recovery.writeRequest (
                 recoveryAuthority, ref::RecoveryDestination::candidateMeasurement,
                 {}, now, requestId).has_value(),
             "Candidate recovery must require the complete stable-ID context");
    require (recovery.writeRequest (
                 { runtimeId, 42, {} }, ref::RecoveryDestination::workBinding,
                 {}, now, requestId).has_value(),
             "Work binding recovery must remain available before a Work exists");

    require (source.deleteFile() && writeStereoWav (source),
             "decodable Reference fixture must be written");
    const auto wavHash = juce::SHA256 (source).toHexString();
    require (writeJson (receiptFile, makeSourceReceipt (source, wavHash)),
             "decodable receipt must be staged");
    const auto wavReceiptHash = juce::SHA256 (receiptFile).toHexString();
    const auto wavReceipt = receiptFile.getSiblingFile (wavReceiptHash + ".json");
    require (receiptFile.moveFileTo (wavReceipt), "decodable receipt must be content-addressed");
    require (writeJson (preparationFile,
                        makePreparation (source, wavHash, wavReceiptHash, wavReceipt.getSize())),
             "decodable preparation must be written");
    {
        std::atomic<bool> comparisonSuspended { false };
        ref::Controller controller (root, [&comparisonSuspended] (bool bSelected)
        {
            comparisonSuspended.store (bSelected);
            return true;
        }, [] { return true; }); // deterministic fixture: stimulus 1 is B
        controller.observeTransport (128, true, true);
        controller.configure (identity, 48'000.0, 2);
        for (int attempt = 0; attempt < 300
             && controller.snapshot().state != ref::RuntimeState::ready; ++attempt)
            juce::Thread::sleep (10);
        require (controller.snapshot().state == ref::RuntimeState::ready
                 && controller.snapshot().auditionBuffered,
                 "verified and decoded preparation must become ready");
        controller.observeTransport (128, true, false);
        require (! controller.startBlind (-14.0, -2.0)
                 && controller.snapshot().blindPhase == ref::BlindPhase::inactive,
                 "Blind Compare must remain unavailable while transport is stopped");
        controller.observeTransport (128, true, true);
        const auto liveAck = juce::JSON::parse (files.acknowledgement);
        require (liveAck.getDynamicObject() != nullptr
                 && liveAck.getDynamicObject()->getProperty ("receipt_status") == "accepted",
                 "only a decoded source may receive an accepted acknowledgement");
        require (controller.selectB (-14.0, -2.0),
                 "explicit B selection must succeed when ready");
        require (comparisonSuspended.load(),
                 "B selection must suspend the normal PRE comparison through its gate");
        const auto matchedSnapshot = controller.snapshot();
        require (std::abs (matchedSnapshot.loudnessDeltaBMinusA) < 1.0e-9
                 && std::abs (matchedSnapshot.truePeakDeltaBMinusA + 1.0) < 1.0e-9,
                 "B deltas must report adjusted B minus the frozen A measurement");
        juce::AudioBuffer<float> audition (2, 256);
        audition.clear();
        require (controller.renderSelectedB (audition, 128, true),
                 "selected B must render from the exact host position");
        require (std::abs (audition.getSample (0, 1)) > 0.001f,
                 "rendered B must contain the prepared Reference audio");
        controller.selectA();
        require (! comparisonSuspended.load(),
                 "A return must resume the normal PRE comparison through its gate");
        require (controller.selectB (-11.0, -2.0),
                 "safe gain-limited B selection must remain available");
        const auto limitedSnapshot = controller.snapshot();
        require (limitedSnapshot.gainLimited
                 && std::abs (limitedSnapshot.appliedGainDb - 2.0) < 1.0e-9
                 && std::abs (limitedSnapshot.loudnessDeltaBMinusA + 1.0) < 1.0e-9
                 && std::abs (limitedSnapshot.adjustedBMaximumTruePeakDbtp + 1.0) < 1.0e-9,
                 "B delta must expose a TP-limited loudness mismatch instead of hiding it");
        controller.selectA();
        audition.clear();
        audition.setSample (0, 0, 0.75f);
        require (! controller.renderSelectedB (audition, 384, true)
                 && audition.getSample (0, 0) == 0.75f,
                 "A selection must leave the DAW buffer untouched");

        require (controller.startBlind (-14.0, -2.0),
                 "Blind Compare must start only from a ready realtime A/B condition");
        auto blind = controller.snapshot();
        require (blind.blindPhase == ref::BlindPhase::active
                 && blind.activeBlindStimulus == 0
                 && blind.pendingBlindStimulus == 0
                 && blind.blindReveal.isEmpty()
                 && ! blind.bSelected,
                 "Blind start must hide assignment and remain on live A");
        require (! controller.selectBlindStimulus (0)
                 && ! controller.selectBlindStimulus (3),
                 "only anonymous stimulus 1 or 2 may be selected");
        juce::Thread::sleep (650);
        blind = controller.snapshot();
        require (std::abs (blind.aIntegratedLoudness + 14.0) < 1.0e-9
                 && std::abs (blind.aMaximumTruePeakDbtp + 2.0) < 1.0e-9,
                 "background preparation refresh must preserve the frozen blind A facts");
        require (controller.selectBlindStimulus (1),
                 "blind stimulus 1 request must be accepted");
        blind = controller.snapshot();
        require (blind.activeBlindStimulus == 0
                 && blind.pendingBlindStimulus == 1
                 && blind.blindReveal.isEmpty(),
                 "requested blind source must not appear active before an audio callback receipt");
        audition.clear();
        require (controller.renderSelectedB (audition, 512, true),
                 "deterministic blind stimulus 1 must render B");
        blind = controller.snapshot();
        require (blind.activeBlindStimulus == 1
                 && blind.pendingBlindStimulus == 0
                 && blind.blindReveal.isEmpty(),
                 "audio callback receipt must confirm only the anonymous stimulus");
        controller.loseAudibleConfirmation();
        blind = controller.snapshot();
        require (blind.activeBlindStimulus == 0 && blind.pendingBlindStimulus == 1,
                 "lost audio heartbeat must withdraw the audible confirmation");
        audition.clear();
        require (controller.renderSelectedB (audition, 576, true)
                 && controller.snapshot().activeBlindStimulus == 1,
                 "a later real callback may confirm the requested source again");
        require (controller.selectBlindStimulus (2),
                 "blind stimulus 2 request must be accepted");
        blind = controller.snapshot();
        require (blind.activeBlindStimulus == 0 && blind.pendingBlindStimulus == 2,
                 "A request must also wait for the audio callback receipt");
        audition.clear();
        audition.setSample (0, 0, 0.5f);
        require (! controller.renderSelectedB (audition, 640, true)
                 && audition.getSample (0, 0) == 0.5f,
                 "blind A must preserve the live DAW buffer");
        blind = controller.snapshot();
        require (blind.activeBlindStimulus == 2 && blind.blindReveal.isEmpty(),
                 "confirmed blind A must still expose no source assignment");
        require (controller.revealBlind(), "Blind Compare must reveal only by explicit action");
        blind = controller.snapshot();
        require (blind.blindPhase == ref::BlindPhase::revealed
                 && blind.blindReveal == "1 = B  /  2 = A",
                 "reveal must expose the deterministic assignment together");
        controller.endBlind();
        blind = controller.snapshot();
        require (blind.blindPhase == ref::BlindPhase::inactive
                 && blind.blindReveal.isEmpty() && ! blind.bSelected,
                 "ending Blind Compare must clear assignment and return to A");

        require (controller.startBlind (-14.0, -2.0),
                 "a fresh Blind Compare must be restartable");
        require (controller.selectBlindStimulus (1),
                 "offline fail-close fixture must select its B stimulus");
        audition.clear();
        require (! controller.renderSelectedB (audition, 768, true, false),
                 "offline or bypass processing must never render Reference B");
        blind = controller.snapshot();
        require (blind.blindPhase == ref::BlindPhase::invalidated
                 && blind.blindReveal.isEmpty() && ! blind.bSelected,
                 "an unavailable audio route must invalidate Blind without revealing assignment");
        for (int attempt = 0; attempt < 100 && comparisonSuspended.load(); ++attempt)
            juce::Thread::sleep (10);
        require (! comparisonSuspended.load(),
                 "audio-thread fail-close must release PRE comparison from the worker thread");

        require (controller.startBlind (-14.0, -2.0),
                 "Blind Compare must restart after a fail-closed trial");
        controller.configure ({ runtimeId, workId, 42 }, 48'000.0, 2);
        blind = controller.snapshot();
        require (blind.blindPhase == ref::BlindPhase::invalidated
                 && blind.blindReveal.isEmpty() && ! blind.bSelected,
                 "route reconfiguration must invalidate without revealing assignment");
    }
    require (! files.capability.exists() && ! files.acknowledgement.exists(),
             "controller teardown must remove only its own runtime receipts");

    {
        ref::Controller deniedController (root, [] (bool bSelected)
        {
            return ! bSelected;
        }, [] { return true; });
        deniedController.observeTransport (128, true, true);
        deniedController.configure (identity, 48'000.0, 2);
        for (int attempt = 0; attempt < 300
             && deniedController.snapshot().state != ref::RuntimeState::ready; ++attempt)
            juce::Thread::sleep (10);
        require (deniedController.snapshot().state == ref::RuntimeState::ready,
                 "denied-switch fixture must still prepare its immutable source");
        require (deniedController.startBlind (-14.0, -2.0),
                 "Blind may start before an output-side switch is requested");
        require (! deniedController.selectBlindStimulus (1),
                 "a rejected output-side switch must fail the explicit Blind action");
        const auto denied = deniedController.snapshot();
        require (denied.activeBlindStimulus == 0 && denied.pendingBlindStimulus == 0
                 && ! denied.bSelected && denied.blindReveal.isEmpty(),
                 "failed switching must clear pending state without leaking the assignment");
    }
    require (! files.capability.exists() && ! files.acknowledgement.exists(),
             "failed-switch teardown must remove its runtime receipts");

    {
        std::atomic<bool> comparisonSuspended { false };
        ref::Controller staleController (root, [&comparisonSuspended] (bool bSelected)
        {
            comparisonSuspended.store (bSelected);
            return true;
        }, [] { return true; });
        staleController.observeTransport (128, true, true);
        staleController.configure (identity, 48'000.0, 2);
        for (int attempt = 0; attempt < 300
             && staleController.snapshot().state != ref::RuntimeState::ready; ++attempt)
            juce::Thread::sleep (10);
        require (staleController.startBlind (-14.0, -2.0)
                 && staleController.selectBlindStimulus (1),
                 "stale-callback fixture must begin a B-side anonymous request");
        for (int attempt = 0; attempt < 400
             && staleController.snapshot().blindPhase != ref::BlindPhase::invalidated; ++attempt)
            juce::Thread::sleep (10);
        for (int attempt = 0; attempt < 100 && comparisonSuspended.load(); ++attempt)
            juce::Thread::sleep (10);
        const auto stale = staleController.snapshot();
        require (stale.blindPhase == ref::BlindPhase::invalidated,
                 "missing audio callbacks must invalidate Blind");
        require (stale.blindReveal.isEmpty(),
                 "stale-callback invalidation must not disclose the assignment");
        require (! stale.bSelected && ! comparisonSuspended.load(),
                 "missing audio callbacks must return both output gates to A");
    }
    require (! files.capability.exists() && ! files.acknowledgement.exists(),
             "stale-callback teardown must remove its runtime receipts");

    testRuntimeV2Workspace (sandbox);

    require (sandbox.deleteRecursively(), "sandbox must be removed");
    std::cout << "Reference audition runtime contract tests passed\n";
    return 0;
}
