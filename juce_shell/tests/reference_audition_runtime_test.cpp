#include "../src/reference_audition/ReferenceAuditionLease.h"
#include "../src/reference_audition/ReferenceAuditionController.h"
#include "../src/reference_audition/ReferenceAuditionProtocol.h"
#include "../src/reference_audition/ReferenceAuditionRepository.h"

#include <cstdlib>
#include <cmath>
#include <iostream>

#include <juce_cryptography/juce_cryptography.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace ref = hypha::reference_audition;

namespace
{
    const juce::String workId = "11111111-1111-4111-8111-111111111111";
    const juce::String preparationId = "33333333-3333-4333-8333-333333333333";
    const juce::String runtimeId = "runtime-post-a";

    void require (bool condition, const char* message)
    {
        if (! condition)
        {
            std::cerr << "Reference audition runtime test failed: " << message << '\n';
            std::exit (1);
        }
    }

    juce::var makeClock()
    {
        auto value = new juce::DynamicObject();
        value->setProperty ("sample_rate_hz", static_cast<juce::int64> (48'000));
        value->setProperty ("channels", static_cast<juce::int64> (2));
        value->setProperty ("sample_count", static_cast<juce::int64> (96'000));
        value->setProperty ("duration_seconds", 2.0);
        return juce::var (value);
    }

    juce::var makeObservation()
    {
        auto value = new juce::DynamicObject();
        value->setProperty ("kind", "loudness_lufs_m");
        value->setProperty ("source_hop_ms", 100.0);
        value->setProperty ("source_point_count", static_cast<juce::int64> (4));
        value->setProperty ("downsample_stride", static_cast<juce::int64> (1));
        value->setProperty ("signature_hop_ms", 100.0);
        value->setProperty ("quantization_db", 0.01);
        juce::Array<juce::var> values;
        values.add (-14.25);
        values.add (juce::var());
        values.add (-13.75);
        values.add (-14.0);
        value->setProperty ("values", juce::var (values));
        return juce::var (value);
    }

    juce::var makeSourceReceipt (const juce::File& source, const juce::String& sourceHash)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_ab_source_receipt");
        root->setProperty ("version", "1.0");

        auto identity = new juce::DynamicObject();
        identity->setProperty ("kind", "work_version");
        identity->setProperty ("source_id", "version-a");
        identity->setProperty ("source_work_id", workId);
        identity->setProperty ("version_id", "version-a");
        identity->setProperty ("catalog_reference_id", juce::var());
        identity->setProperty ("title", "Version A");
        identity->setProperty ("file_name", source.getFileName());
        identity->setProperty ("file_path", source.getFullPathName());
        identity->setProperty ("sha256_file", sourceHash);
        identity->setProperty ("measurement_receipt_sha256", juce::var());
        auto revision = new juce::DynamicObject();
        revision->setProperty ("dev", "1");
        revision->setProperty ("ino", "2");
        revision->setProperty ("size", juce::String (source.getSize()));
        revision->setProperty ("mtime_ms", "1000");
        revision->setProperty ("ctime_ms", "1000");
        identity->setProperty ("revision", juce::var (revision));
        root->setProperty ("source", juce::var (identity));

        auto measurement = new juce::DynamicObject();
        measurement->setProperty ("standard", "ITU-R BS.1770");
        measurement->setProperty ("measured_at", "2026-09-03T00:00:00.000Z");
        measurement->setProperty ("lufs_i", -14.0);
        measurement->setProperty ("max_true_peak_dbtp", -3.0);
        measurement->setProperty ("duration_seconds", 2.0);
        measurement->setProperty ("sample_rate_hz", static_cast<juce::int64> (48'000));
        root->setProperty ("measurement", juce::var (measurement));

        auto material = new juce::DynamicObject();
        material->setProperty ("clock", makeClock());
        auto content = new juce::DynamicObject();
        content->setProperty ("sha256_pcm", juce::var());
        material->setProperty ("content", juce::var (content));
        material->setProperty ("observation_signature", makeObservation());
        material->setProperty ("runtime_correlation_required", true);
        root->setProperty ("alignment_material", juce::var (material));
        return juce::var (root);
    }

    juce::var makePreparation (const juce::File& source, const juce::String& sourceHash,
                               const juce::String& receiptHash, std::int64_t receiptBytes)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_ab_preparation");
        root->setProperty ("version", "1.2");
        root->setProperty ("preparation_id", preparationId);
        auto target = new juce::DynamicObject();
        target->setProperty ("work_id", workId);
        root->setProperty ("target", juce::var (target));
        auto sourceSummary = new juce::DynamicObject();
        sourceSummary->setProperty ("kind", "work_version");
        sourceSummary->setProperty ("source_id", "version-a");
        sourceSummary->setProperty ("source_work_id", workId);
        sourceSummary->setProperty ("version_id", "version-a");
        sourceSummary->setProperty ("catalog_reference_id", juce::var());
        sourceSummary->setProperty ("title", "Version A");
        sourceSummary->setProperty ("file_name", source.getFileName());
        sourceSummary->setProperty ("sha256_file", sourceHash);
        root->setProperty ("source", juce::var (sourceSummary));
        auto receipt = new juce::DynamicObject();
        receipt->setProperty ("sha256", receiptHash);
        receipt->setProperty ("bytes", receiptBytes);
        root->setProperty ("source_receipt", juce::var (receipt));
        auto level = new juce::DynamicObject();
        level->setProperty ("policy", "b_matches_a");
        level->setProperty ("true_peak_ceiling_dbtp", -1.0);
        level->setProperty ("max_safe_positive_gain_db", 2.0);
        level->setProperty ("positive_gain_allowed", true);
        root->setProperty ("level_match", juce::var (level));
        auto alignment = new juce::DynamicObject();
        alignment->setProperty ("requested_mode", "sample_lock");
        alignment->setProperty ("runtime_confirmation_required", true);
        alignment->setProperty ("reference_cue_seconds", juce::var());
        root->setProperty ("alignment", juce::var (alignment));
        auto state = new juce::DynamicObject();
        state->setProperty ("audible_source_on_open", "a");
        state->setProperty ("prepared_side", "b");
        state->setProperty ("audio_modified_by_os", false);
        root->setProperty ("state", juce::var (state));
        root->setProperty ("prepared_at_ms", static_cast<juce::int64> (1'788'390'000'000));
        return juce::var (root);
    }

    bool writeJson (const juce::File& file, const juce::var& value)
    {
        return file.getParentDirectory().createDirectory()
            && file.replaceWithText (juce::JSON::toString (value, true) + "\n");
    }

    bool hasExactKeys (const juce::DynamicObject& object,
                       std::initializer_list<const char*> names)
    {
        if (object.getProperties().size() != static_cast<int> (names.size()))
            return false;
        for (const auto* name : names)
            if (! object.hasProperty (name))
                return false;
        return true;
    }

    bool writeStereoWav (const juce::File& file)
    {
        auto stream = file.createOutputStream();
        if (stream == nullptr)
            return false;
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
            stream.release(), 48'000.0, 2, 24, {}, 0));
        if (writer == nullptr)
            return false;
        juce::AudioBuffer<float> audio (2, 96'000);
        for (int frame = 0; frame < audio.getNumSamples(); ++frame)
        {
            const auto sample = static_cast<float> (
                0.25 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * frame / 48'000.0));
            audio.setSample (0, frame, sample);
            audio.setSample (1, frame, sample * 0.75f);
        }
        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }
}

int main()
{
    require (ref::safeId (workId), "Work UUID must be a safe ID");
    require (! ref::safeId ("../escape"), "path separators must be rejected");
    require (ref::safeUuid (preparationId), "preparation UUID must validate");

    const auto sandbox = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getNonexistentChildFile ("hypha-reference-audition", {}, false);
    require (sandbox.createDirectory(), "sandbox directory must be created");
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

    require (sandbox.deleteRecursively(), "sandbox must be removed");
    std::cout << "Reference audition runtime contract tests passed\n";
    return 0;
}
