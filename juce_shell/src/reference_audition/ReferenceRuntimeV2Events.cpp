#include "ReferenceRuntimeV2Controller.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>

#include <juce_cryptography/juce_cryptography.h>

namespace hypha::reference_audition
{
    namespace
    {
        juce::String canonicalTimestamp (std::int64_t milliseconds)
        {
            const auto seconds = static_cast<std::time_t> (milliseconds / 1000);
            std::tm utc {};
           #if JUCE_WINDOWS
            if (::gmtime_s (&utc, &seconds) != 0) return {};
           #else
            if (::gmtime_r (&seconds, &utc) == nullptr) return {};
           #endif
            return juce::String::formatted (
                "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                utc.tm_hour, utc.tm_min, utc.tm_sec,
                static_cast<int> (milliseconds % 1000));
        }

        juce::var sampleRange (std::int64_t rate, std::int64_t start,
                               std::int64_t end)
        {
            auto object = new juce::DynamicObject();
            object->setProperty ("sample_rate_hz", rate);
            object->setProperty ("start_sample", start);
            object->setProperty ("end_sample", end);
            return juce::var (object);
        }

        juce::var alignmentAnchor (std::int64_t a, std::int64_t b)
        {
            auto object = new juce::DynamicObject();
            object->setProperty ("a_sample", a);
            object->setProperty ("b_sample", b);
            return juce::var (object);
        }

        juce::String runtimeFingerprint (const RuntimeEventContext& context,
                                         const RuntimeV2BlindSnapshot& facts)
        {
            juce::MemoryBlock input;
            constexpr char domain[] = "kirin_hypha_reference_runtime_v1";
            input.append (domain, sizeof (domain) - 1);
            const std::uint8_t separator = 0;
            input.append (&separator, 1);
            const auto identity = context.identity.runtimeInstanceId + ":"
                + juce::String (context.identity.hostProcessId) + ":"
                + juce::String (facts.aSampleRateHz) + ":"
                + juce::String (facts.channels);
            input.append (identity.toRawUTF8(), identity.getNumBytesAsUTF8());
            return juce::SHA256 (input).toHexString();
        }

        juce::var buildTrialStart (const RuntimeEventContext& context,
                                   const RuntimeCandidate& candidate,
                                   const RuntimeCue& cue,
                                   const RuntimeSource& source,
                                   const RuntimeV2BlindSnapshot& facts,
                                   const juce::String& fingerprint,
                                   std::int64_t startedAtMs)
        {
            auto a = new juce::DynamicObject();
            a->setProperty ("source_kind", "daw_revision");
            a->setProperty ("daw_revision_id", facts.dawRevisionId);
            a->setProperty ("cue_pcm_sha256", facts.aCuePcmSha256);
            auto b = new juce::DynamicObject();
            b->setProperty ("source_kind", "work_version");
            b->setProperty ("version_id", candidate.sourceVersionId);
            b->setProperty ("sha256_file", source.sourceFileSha256);
            b->setProperty ("sha256_pcm", source.sourcePcmSha256);
            b->setProperty ("cue_pcm_sha256", facts.bCuePcmSha256);
            auto sources = new juce::DynamicObject();
            sources->setProperty ("a", juce::var (a));
            sources->setProperty ("b", juce::var (b));

            auto trialCue = new juce::DynamicObject();
            trialCue->setProperty ("cue_id", cue.cueId);
            trialCue->setProperty ("loop_enabled", cue.loopEnabled);
            trialCue->setProperty ("a", sampleRange (
                facts.aSampleRateHz, facts.aStartSample, facts.aEndSample));
            trialCue->setProperty ("b", sampleRange (
                facts.bSampleRateHz, facts.bStartSample, facts.bEndSample));

            auto playback = new juce::DynamicObject();
            playback->setProperty ("engine", "kirin_hypha_reference_v1");
            playback->setProperty ("runtime_fingerprint", fingerprint);
            playback->setProperty ("sample_rate_hz", facts.aSampleRateHz);
            playback->setProperty ("channels", static_cast<juce::int64> (facts.channels));
            playback->setProperty ("switch_policy", "callback_boundary_no_crossfade");

            juce::Array<juce::var> anchors;
            anchors.add (alignmentAnchor (facts.aStartSample, facts.bStartSample));
            anchors.add (alignmentAnchor (facts.aEndSample, facts.bEndSample));
            auto alignment = new juce::DynamicObject();
            alignment->setProperty ("algorithm", "kirin_content_map_v1");
            alignment->setProperty ("method", "automatic");
            alignment->setProperty ("anchors", juce::var (anchors));

            const auto milli = [] (double value) {
                return static_cast<juce::int64> (std::llround (value * 1000.0));
            };
            const auto aPeak = milli (facts.aCueTruePeakDbtp);
            const auto bPeak = milli (facts.bCueTruePeakDbtp);
            auto gain = new juce::DynamicObject();
            gain->setProperty ("measurement_basis", "itu_r_bs_1770_5");
            gain->setProperty ("match_policy", "kirin_aligned_active_blocks_v1");
            gain->setProperty ("gain_strategy",
                               facts.aGainDb < -0.0005 ? "lower_a_approved" : "a_fixed");
            gain->setProperty ("paired_block_count",
                               static_cast<juce::int64> (facts.pairedBlockCount));
            gain->setProperty ("paired_loudness_delta_median_millilu",
                               milli (facts.pairedLoudnessDeltaDb));
            gain->setProperty ("a_cue_true_peak_millidbtp", aPeak);
            gain->setProperty ("a_gain_millidb", milli (facts.aGainDb));
            gain->setProperty ("b_gain_millidb", milli (facts.bGainDb));
            gain->setProperty ("b_cue_true_peak_millidbtp", bPeak);
            gain->setProperty ("ceiling_millidbtp",
                               std::max<juce::int64> ({ -1000, aPeak, bPeak }));

            auto conditions = new juce::DynamicObject();
            conditions->setProperty ("playback", juce::var (playback));
            conditions->setProperty ("alignment", juce::var (alignment));
            conditions->setProperty ("gain_match", juce::var (gain));

            auto commitment = new juce::DynamicObject();
            commitment->setProperty ("algorithm", "sha256");
            commitment->setProperty ("canonicalization", "rfc8785_jcs");
            commitment->setProperty ("domain", "kirin_reference_assignment_v1");
            commitment->setProperty ("value", facts.assignmentCommitmentSha256);

            auto start = new juce::DynamicObject();
            start->setProperty ("format", "kirin_reference_listening_trial_start");
            start->setProperty ("version", "1.0");
            start->setProperty ("trial_id", facts.trialId);
            start->setProperty ("work_id", context.identity.workId);
            start->setProperty ("recording_id", candidate.sourceRecordingId);
            start->setProperty ("created_at", canonicalTimestamp (startedAtMs));
            start->setProperty ("origin", "hypha");
            start->setProperty ("sources", juce::var (sources));
            start->setProperty ("relationship", "same_recording_different_revision");
            start->setProperty ("cue", juce::var (trialCue));
            start->setProperty ("conditions", juce::var (conditions));
            start->setProperty ("commitment", juce::var (commitment));
            return juce::var (start);
        }

        juce::var buildTrialCompleted (const juce::var& trialStart,
                                       const juce::String& fingerprint,
                                       std::int64_t completedAtMs,
                                       const RuntimeV2BlindSnapshot& facts)
        {
            const auto canonicalStart = RuntimeEventTransport::canonicalJson (trialStart);
            juce::MemoryBlock bytes;
            bytes.append (canonicalStart.toRawUTF8(), canonicalStart.getNumBytesAsUTF8());
            auto startReceipt = new juce::DynamicObject();
            startReceipt->setProperty ("trial_id", facts.trialId);
            startReceipt->setProperty ("relative_path",
                "reference/listening_trials/" + facts.trialId + "/start.v1.json");
            startReceipt->setProperty ("sha256", juce::SHA256 (bytes).toHexString());
            startReceipt->setProperty ("bytes",
                static_cast<juce::int64> (canonicalStart.getNumBytesAsUTF8()));

            auto answer = new juce::DynamicObject();
            answer->setProperty ("selected_stimulus",
                                 facts.answeredStimulus == 1 ? "stimulus_1" : "stimulus_2");
            answer->setProperty ("note", juce::var());

            const bool oneIsB = facts.revealedStimulusOneSide == 1;
            auto reveal = new juce::DynamicObject();
            reveal->setProperty ("trial_id", facts.trialId);
            reveal->setProperty ("stimulus_1", oneIsB ? "b" : "a");
            reveal->setProperty ("stimulus_2", oneIsB ? "a" : "b");
            reveal->setProperty ("nonce", facts.revealedNonceHex);

            const auto stimulus = [] (std::uint64_t switches, std::uint64_t frames) {
                auto value = new juce::DynamicObject();
                value->setProperty ("confirmed_switches", static_cast<juce::int64> (switches));
                value->setProperty ("audible_frames", static_cast<juce::int64> (frames));
                return juce::var (value);
            };
            auto audible = new juce::DynamicObject();
            audible->setProperty ("basis", "audio_callback_frames_v1");
            audible->setProperty ("trial_id", facts.trialId);
            audible->setProperty ("runtime_fingerprint", fingerprint);
            audible->setProperty ("first_callback_sequence",
                                  juce::String (std::to_string (facts.firstCallbackSequence)));
            audible->setProperty ("last_callback_sequence",
                                  juce::String (std::to_string (facts.lastCallbackSequence)));
            audible->setProperty ("stimulus_1", stimulus (
                facts.stimulusOneConfirmedSwitches, facts.stimulusOneAudibleFrames));
            audible->setProperty ("stimulus_2", stimulus (
                facts.stimulusTwoConfirmedSwitches, facts.stimulusTwoAudibleFrames));

            auto completed = new juce::DynamicObject();
            completed->setProperty ("format", "kirin_reference_listening_trial_completed");
            completed->setProperty ("version", "1.0");
            completed->setProperty ("trial_id", facts.trialId);
            completed->setProperty ("completed_at", canonicalTimestamp (completedAtMs));
            completed->setProperty ("start_artifact", juce::var (startReceipt));
            completed->setProperty ("answer", juce::var (answer));
            completed->setProperty ("reveal", juce::var (reveal));
            completed->setProperty ("audible_receipt", juce::var (audible));
            return juce::var (completed);
        }
    }

    void RuntimeV2Controller::beginAuditionEventSession (
        std::uint64_t bBaseline) noexcept
    {
        try
        {
            const juce::ScopedLock lock (stateLock);
            if (! activeEventContext.valid()) return;
            while (auditionEventSessions.size() >= 64)
            {
                const auto removable = std::find_if (
                    auditionEventSessions.begin(), auditionEventSessions.end(),
                    [] (const auto& item) { return ! item.startWritten; });
                if (removable == auditionEventSessions.end()) return;
                auditionEventSessions.erase (removable);
            }
            AuditionEventSession session;
            session.context = activeEventContext;
            session.runId = RuntimeEventTransport::uuidV4();
            session.startedEventId = RuntimeEventTransport::uuidV4();
            session.completedEventId = RuntimeEventTransport::uuidV4();
            session.bConfirmationBaseline = bBaseline;
            auditionEventSessions.push_back (std::move (session));
            notify();
        }
        catch (...) {}
    }

    void RuntimeV2Controller::requestAuditionReturnEvent (
        std::uint64_t aBaseline) noexcept
    {
        try
        {
            const juce::ScopedLock lock (stateLock);
            const auto pending = std::find_if (
                auditionEventSessions.rbegin(), auditionEventSessions.rend(),
                [] (const auto& item) { return ! item.returnRequested; });
            if (pending == auditionEventSessions.rend()) return;
            pending->returnRequested = true;
            pending->aConfirmationBaseline = aBaseline;
            notify();
        }
        catch (...) {}
    }

    void RuntimeV2Controller::beginBlindEventSession (
        const RuntimeV2BlindSnapshot& facts) noexcept
    {
        try
        {
            const juce::ScopedLock lock (stateLock);
            if (! activeEventContext.valid() || activeEventSource == nullptr
                || activeEventCandidate.sourceKind != "work_version"
                || activeEventCandidate.sourceWorkId != activeEventContext.identity.workId
                || facts.trialId.isEmpty() || facts.dawRevisionId.isEmpty()
                || facts.aCuePcmSha256.isEmpty() || facts.bCuePcmSha256.isEmpty())
                return;
            BlindEventSession session;
            session.context = activeEventContext;
            session.startedEventId = RuntimeEventTransport::uuidV4();
            session.completedEventId = RuntimeEventTransport::uuidV4();
            session.startedAtMs = juce::Time::currentTimeMillis();
            session.runtimeFingerprint = runtimeFingerprint (activeEventContext, facts);
            session.trialStart = buildTrialStart (
                activeEventContext, activeEventCandidate, activeEventCue,
                *activeEventSource, facts, session.runtimeFingerprint,
                session.startedAtMs);
            blindEventSession = std::move (session);
            notify();
        }
        catch (...)
        {
            const juce::ScopedLock lock (stateLock);
            blindEventSession.reset();
        }
    }

    void RuntimeV2Controller::completeBlindEventSession (
        const RuntimeV2BlindSnapshot& facts) noexcept
    {
        try
        {
            const juce::ScopedLock lock (stateLock);
            if (! blindEventSession || facts.trialId.isEmpty()
                || blindEventSession->trialStart.getProperty ("trial_id", {}).toString()
                    != facts.trialId)
                return;
            blindEventSession->completedAtMs = juce::Time::currentTimeMillis();
            blindEventSession->trialCompleted = buildTrialCompleted (
                blindEventSession->trialStart,
                blindEventSession->runtimeFingerprint,
                blindEventSession->completedAtMs,
                facts);
            blindEventSession->completionPending = true;
            notify();
        }
        catch (...) {}
    }

    void RuntimeV2Controller::serviceRuntimeEvents()
    {
        const auto now = juce::Time::currentTimeMillis();
        const auto bConfirmed = bAudibleConfirmations.load (std::memory_order_acquire);
        const auto aConfirmed = aAudibleConfirmations.load (std::memory_order_acquire);

        std::vector<AuditionEventSession> starts;
        std::vector<AuditionEventSession> completions;
        {
            const juce::ScopedLock lock (stateLock);
            for (auto current = auditionEventSessions.begin();
                 current != auditionEventSessions.end();)
            {
                if (! current->startWritten && current->returnRequested
                    && aConfirmed > current->aConfirmationBaseline
                    && bConfirmed <= current->bConfirmationBaseline)
                {
                    current = auditionEventSessions.erase (current);
                    continue;
                }
                if (! current->startWritten && bConfirmed > current->bConfirmationBaseline)
                    starts.push_back (*current);
                else if (current->startWritten && current->returnRequested
                         && aConfirmed > current->aConfirmationBaseline)
                    completions.push_back (*current);
                ++current;
            }
        }

        for (const auto& session : starts)
        {
            if (! eventTransport.writeAuditionStarted (
                    session.context, session.startedEventId, session.runId, now).written)
                continue;
            const juce::ScopedLock lock (stateLock);
            const auto current = std::find_if (
                auditionEventSessions.begin(), auditionEventSessions.end(),
                [&session] (const auto& item) {
                    return item.startedEventId == session.startedEventId;
                });
            if (current != auditionEventSessions.end()) current->startWritten = true;
        }
        for (const auto& session : completions)
        {
            if (! eventTransport.writeAuditionCompleted (
                    session.context, session.completedEventId, session.runId,
                    session.startedEventId, now, 1, 1).written)
                continue;
            const juce::ScopedLock lock (stateLock);
            const auto current = std::find_if (
                auditionEventSessions.begin(), auditionEventSessions.end(),
                [&session] (const auto& item) {
                    return item.completedEventId == session.completedEventId;
                });
            if (current != auditionEventSessions.end()) auditionEventSessions.erase (current);
        }

        std::optional<BlindEventSession> blindStart;
        std::optional<BlindEventSession> blindCompletion;
        {
            const juce::ScopedLock lock (stateLock);
            if (blindEventSession && ! blindEventSession->startWritten)
                blindStart = blindEventSession;
            else if (blindEventSession && blindEventSession->completionPending)
                blindCompletion = blindEventSession;
        }
        if (blindStart)
        {
            const auto runId = blindStart->trialStart.getProperty ("trial_id", {}).toString();
            if (eventTransport.writeBlindStarted (
                    blindStart->context, blindStart->startedEventId, runId,
                    blindStart->startedAtMs, blindStart->trialStart).written)
            {
                const juce::ScopedLock lock (stateLock);
                if (blindEventSession
                    && blindEventSession->startedEventId == blindStart->startedEventId)
                    blindEventSession->startWritten = true;
            }
        }
        if (blindCompletion)
        {
            const auto runId = blindCompletion->trialStart.getProperty (
                "trial_id", {}).toString();
            if (eventTransport.writeBlindCompleted (
                    blindCompletion->context, blindCompletion->completedEventId, runId,
                    blindCompletion->completedAtMs, blindCompletion->trialCompleted).written)
            {
                const juce::ScopedLock lock (stateLock);
                if (blindEventSession
                    && blindEventSession->completedEventId == blindCompletion->completedEventId)
                    blindEventSession.reset();
            }
        }
    }

    void RuntimeV2Controller::serviceRecoveryAcknowledgement()
    {
        constexpr std::int64_t acknowledgementTimeoutMs = 10'000;
        constexpr std::int64_t outcomeDisplayMs = 5'000;
        const auto now = juce::Time::currentTimeMillis();
        std::optional<RecoveryRequest> request;
        {
            const juce::ScopedLock lock (stateLock);
            if (recoveryStatusExpiresAtMs > 0 && now >= recoveryStatusExpiresAtMs)
            {
                currentSnapshot.recoveryStatus.clear();
                recoveryStatusExpiresAtMs = 0;
            }
            request = pendingRecoveryRequest;
        }
        if (! request) return;
        const auto acknowledgement = recoveryTransport.loadAcknowledgement (*request);
        if (! acknowledgement)
        {
            if (now - request->requestedAtMs < acknowledgementTimeoutMs) return;
            const juce::ScopedLock lock (stateLock);
            if (pendingRecoveryRequest
                && pendingRecoveryRequest->requestId == request->requestId)
            {
                pendingRecoveryRequest.reset();
                currentSnapshot.recoveryStatus = "timed_out";
                recoveryStatusExpiresAtMs = now + outcomeDisplayMs;
            }
            return;
        }
        const juce::ScopedLock lock (stateLock);
        if (! pendingRecoveryRequest
            || pendingRecoveryRequest->requestId != acknowledgement->requestId)
            return;
        switch (acknowledgement->outcome)
        {
            case RecoveryOutcome::exactOpened:
                currentSnapshot.recoveryStatus = "exact_opened";
                break;
            case RecoveryOutcome::safeFallbackOpened:
                currentSnapshot.recoveryStatus = "safe_fallback_opened";
                break;
            case RecoveryOutcome::rejected:
                currentSnapshot.recoveryStatus = "rejected";
                break;
        }
        pendingRecoveryRequest.reset();
        recoveryStatusExpiresAtMs = now + outcomeDisplayMs;
    }
}
