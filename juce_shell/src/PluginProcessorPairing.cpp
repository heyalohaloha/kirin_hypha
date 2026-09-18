#include "PluginProcessor.h"
#include "HyphaPluginFormat.h"

#include "kirin_hypha_local_blind_capture_ffi.h"
#include "kirin_hypha_pair_snapshot_ffi.h"
#include "reference_audition/ReferenceBlindSession.h"

#include <cmath>
#include <limits>

namespace
{
bool decodeCaptureRequest (const KirinLocalBlindCaptureRequest& source,
                           hypha::local_blind::ExactCaptureRequest& out)
{
    hypha::local_blind::ExactCaptureRequest decoded;
    decoded.requestId = source.request_id;
    decoded.pair = { source.pair_generation, source.pre_project_hash, source.pre_instance_id };
    decoded.captureGeneration = source.capture_generation;
    decoded.clockGeneration = source.clock_generation;
    decoded.clockSource = source.clock_source;
    decoded.clockPositionAtIssue = source.clock_position_at_issue;
    decoded.sampleRate = source.sample_rate;
    decoded.channels = static_cast<int> (source.channels);
    decoded.nativeStart = source.native_start;
    decoded.frames = source.frames;
    decoded.expiresAtUnixMs = source.expires_at_unix_ms;
    if (! decoded.valid())
        return false;
    out = std::move (decoded);
    return true;
}
}

hypha::local_blind::CaptureSide
KirinHyphaProcessorBase::localBlindCaptureSide (Role selectedRole) noexcept
{
    return selectedRole == Role::Pre ? hypha::local_blind::CaptureSide::pre
                                     : hypha::local_blind::CaptureSide::post;
}

hypha::local_blind::CaptureServiceHooks
KirinHyphaProcessorBase::localBlindCaptureHooks (KirinHyphaProcessorBase& processor)
{
    return {
        [&processor] (hypha::local_blind::ExactCaptureRequest& request)
            { return processor.pollLocalBlindCaptureRequest (request); },
        [&processor] (const std::string& requestId)
            { return processor.acknowledgeLocalBlindCaptureRequest (requestId); },
        [&processor] (const std::string& requestId)
            { return processor.localBlindCaptureIsArmed (requestId); },
        [&processor] (hypha::local_blind::ExactPairBinding& pair)
            { return processor.localBlindPairBinding (pair); },
        [&processor] (const auto& request, const auto& receipt, const auto& pcm,
                      std::string& sha256)
            { return processor.publishLocalBlindPreCapture (request, receipt, pcm, sha256); },
        [&processor] (const auto& request, auto failure)
            { return processor.publishLocalBlindPreFailure (request, failure); },
        [&processor] (const auto& request, auto& imported)
            { return processor.readLocalBlindPreCapture (request, imported); },
        [&processor] (const auto& request, auto& failure)
            { return processor.readLocalBlindPreFailure (request, failure); },
        [&processor] (const auto& request, const std::string& sha256)
            { return processor.acknowledgeLocalBlindPreCapture (request, sha256); },
        [&processor] (const auto& request, const std::string& sha256)
            { return processor.localBlindPreCaptureWasConsumed (request, sha256); },
        [&processor] (const auto& request)
            { processor.retireLocalBlindPreCapture (request); },
        [&processor] (const auto& request, const auto& post, const auto& pre)
            { return processor.acceptLocalBlindProductPair (request, post, pre); }
    };
}

hypha::local_blind::ProductSessionView
KirinHyphaProcessorBase::localBlindProductView() const
{
    auto view = localBlindProductSession.view();
    view.canRecapture = view.canRecapture
        && localBlindCapture.view().phase == hypha::local_blind::CaptureOwnerPhase::idle;
    return view;
}

bool KirinHyphaProcessorBase::localBlindProductSupported() const noexcept
{
    return hypha::plugin_format::supportsLocalBlindProduct (wrapperType);
}

bool KirinHyphaProcessorBase::releaseLocalBlindProductScope (std::uint64_t epoch)
{
    bool released=false;
    { const juce::ScopedLock lock(handleLock);
        released=role==Role::Post && hyphaHandle && kirin_hypha_end_local_blind(hyphaHandle,epoch); }
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if(released && referenceAuditionController) referenceAuditionController->releaseLocalBlind(epoch);
   #endif
    return released;
}

bool KirinHyphaProcessorBase::acceptLocalBlindProductPair (
    const hypha::local_blind::ExactCaptureRequest& request,
    const hypha::local_blind::ExactRangeCapture& post,
    const hypha::local_blind::ExactRangeCapture& pre)
{
    return localBlindProductSession.acceptCapturedPair (
        request, post, pre, hypha::reference_audition::secureRandomBit);
}

hypha::local_blind::CaptureAdmission KirinHyphaProcessorBase::localBlindCaptureAvailability() const
{
    using Admission = hypha::local_blind::CaptureAdmission;
    using Phase = hypha::local_blind::ProductSessionPhase;
    if (! localBlindProductSupported() || role != Role::Post) return Admission::unsupported;
    const auto session = localBlindProductView();
    if (session.phase != Phase::idle && session.phase != Phase::returned && session.phase != Phase::failed)
        return Admission::recovery;
    if (session.phase == Phase::failed && ! session.canRecapture) return Admission::releasePending;
    const auto reference=referenceAuditionSnapshot();
    if(reference.captureAccess && reference.captureAccess->busy()) return Admission::referenceBusy;
    if (reference.blindPhase != hypha::reference_audition::BlindPhase::inactive)
        return Admission::referenceBusy;
    if (isRecording() || keepPhase() != KIRIN_KEEP_PHASE_IDLE) return Admission::keepBusy;
    if (pairStatus() != KIRIN_PAIR_STATUS_PAIRED) return Admission::pairRequired;
    if (localBlindCapture.view().phase != hypha::local_blind::CaptureOwnerPhase::idle)
        return Admission::captureBusy;
    if (! isPlaying() || ! heartbeatLive()) return Admission::playbackRequired;
    hypha::local_blind::HostClockProbeSnapshot clock;
    if (! hostClockProbe.read (clock) || ! clock.playing || ! clock.hasPosition
        || ! std::isfinite (clock.rate) || clock.rate < 8'000.0 || clock.rate > 768'000.0)
        return Admission::clockUnavailable;
    return Admission::ready;
}

hypha::local_blind::CaptureAdmission KirinHyphaProcessorBase::requestLocalBlindProductCapture (hypha::meter_context::MeterContext context)
{
    using Admission = hypha::local_blind::CaptureAdmission;
    const auto available = localBlindCaptureAvailability();
    if (available != Admission::ready) return available;
    hypha::local_blind::HostClockProbeSnapshot clock;
    if (! hostClockProbe.read (clock) || ! clock.playing || ! clock.hasPosition
        || ! std::isfinite (clock.rate) || clock.rate < 8'000.0 || clock.rate > 768'000.0)
        return Admission::clockUnavailable;
    std::uint64_t scopeEpoch = 0;
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if(referenceAuditionController && !referenceAuditionController->reserveLocalBlind()) return Admission::referenceBusy;
   #endif
    Admission admission=Admission::ready;
    {
        const juce::ScopedLock lock(handleLock);
        if(!hyphaHandle) admission=Admission::engineUnavailable;
        else if(!kirin_hypha_begin_local_blind(hyphaHandle,&scopeEpoch)) admission=Admission::admissionFailed;
    }
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if(referenceAuditionController) {
        if(admission==Admission::ready) referenceAuditionController->bindLocalBlind(scopeEpoch);
        else referenceAuditionController->releaseLocalBlind(0);
    }
   #endif
    if(admission!=Admission::ready) return admission;
    const auto serial = localBlindProductSerial.fetch_add (1, std::memory_order_acq_rel) + 1;
    const auto now = static_cast<std::uint64_t> (juce::Time::currentTimeMillis());
    const auto generation = (now << 16u) | (serial & 0xffffu);
    const auto gainPolicy = context == hypha::meter_context::MeterContext::trackStem
        ? hypha::local_blind::GainMatchPolicy::exactTrackEventEnergyV1
        : hypha::local_blind::GainMatchPolicy::alignedActiveBlocksV1;
    if (generation == 0 || ! localBlindProductSession.beginCapture (
            scopeEpoch, generation, gainPolicy,
            { clock.source, clock.presentationSource, clock.inputLatency, clock.outputLatency,
              clock.hasInputLatency, clock.hasOutputLatency }))
    {
        releaseLocalBlindProductScope (scopeEpoch);
        return Admission::captureBusy;
    }
    hypha::local_blind::ExactCaptureRequest request;
    if (! issueLocalBlindCaptureRequest (
            generation, static_cast<std::int64_t> (std::llround (clock.rate)) * 4, request))
    {
        localBlindProductSession.failCaptureRequest();
        startTimer (50);
        return Admission::requestFailed;
    }
    startTimer (50);
    return Admission::ready;
}

bool KirinHyphaProcessorBase::startLocalBlindProductTrial (bool approveLowerPost)
{ return localBlindProductSupported() && localBlindProductSession.start (approveLowerPost); }

bool KirinHyphaProcessorBase::selectLocalBlindProductStimulus (int stimulus)
{ return localBlindProductSupported() && localBlindProductSession.select (stimulus); }

bool KirinHyphaProcessorBase::answerLocalBlindProductTrial (
    hypha::local_blind::TrialAnswer answer)
{ return localBlindProductSupported() && localBlindProductSession.answer (answer); }

bool KirinHyphaProcessorBase::revealLocalBlindProductTrial()
{ return localBlindProductSupported() && localBlindProductSession.reveal(); }

void KirinHyphaProcessorBase::stopLocalBlindProductTrial()
{ localBlindProductSession.stop(); }

void KirinHyphaProcessorBase::cancelLocalBlindProductSession()
{
    localBlindProductSession.invalidate();
    localBlindCapture.requestReset();
    startTimer (50);
}

hypha::local_blind::TrialReturnFacts KirinHyphaProcessorBase::requestLocalBlindNormalReturn()
{
    const auto facts = localBlindProductSession.requestNormalReturn();
    startTimer (50);
    return facts;
}

void KirinHyphaProcessorBase::serviceLocalBlindProductSession()
{
    if (! localBlindProductSupported())
    {
        if (localBlindProductSession.view().phase != hypha::local_blind::ProductSessionPhase::idle)
        {
            localBlindProductSession.invalidate();
            localBlindCapture.requestReset();
        }
        return;
    }
    const auto product = localBlindProductSession.view();
    if (product.phase == hypha::local_blind::ProductSessionPhase::capturing
        && localBlindCapture.view().phase == hypha::local_blind::CaptureOwnerPhase::failed)
    {
        localBlindProductSession.invalidate();
        localBlindCapture.requestReset();
    }
    else if (product.phase == hypha::local_blind::ProductSessionPhase::ready
             || product.phase == hypha::local_blind::ProductSessionPhase::armed
             || product.phase == hypha::local_blind::ProductSessionPhase::listening
             || product.phase == hypha::local_blind::ProductSessionPhase::revealed)
    {
        hypha::local_blind::ExactPairBinding current;
        localBlindProductSession.validatePair (localBlindPairBinding (current) ? &current : nullptr);
    }
    localBlindProductSession.service();
}

void KirinHyphaProcessorBase::stopLocalBlindCaptureForFormatChange (
    double sampleRate, const std::vector<uint8_t>& channelRoles)
{
    bool shouldStop = false;
    {
        const juce::ScopedLock lock (handleLock);
        // B-961: roles, not a count. Ten channels are 7.1.2 or 5.1.4, and a capture started under
        // one is not a capture of the other.
        shouldStop = hyphaHandle == nullptr
                  || ! preparedFormat.matches (sampleRate, channelRoles);
        if (shouldStop && hyphaHandle != nullptr && kirin_hypha_is_recording (hyphaHandle))
            shouldStop = false;
    }
    if (shouldStop)
    {
        localBlindProductSession.invalidate();
        localBlindCapture.stop();
    }
}

void KirinHyphaProcessorBase::startLocalBlindCaptureForPreparedFormat()
{
   #if ! JUCE_DEBUG
    if (! localBlindProductSupported())
        return;
   #endif
    if (! localBlindCapture.running() && hyphaHandle != nullptr)
        localBlindCapture.start (static_cast<std::uint32_t> (preparedFormat.sampleRate),
                                 static_cast<int> (preparedFormat.channelRoles.size()));
}

bool KirinHyphaProcessorBase::localBlindPairBinding (
    hypha::local_blind::ExactPairBinding& out) const
{
    const juce::ScopedLock lock (handleLock);
    if (role != Role::Post || hyphaHandle == nullptr)
        return false;

    KirinExactPairBinding binding {};
    if (! kirin_hypha_get_local_blind_pair_binding (hyphaHandle, &binding))
        return false;

    hypha::local_blind::ExactPairBinding decoded {
        binding.pair_generation,
        std::string (binding.project_hash),
        std::string (binding.pre_instance_id)
    };
    if (! decoded.valid())
        return false;
    out = std::move (decoded);
    return true;
}

bool KirinHyphaProcessorBase::issueLocalBlindCaptureRequest (
    std::uint64_t captureGeneration, std::int64_t frames,
    hypha::local_blind::ExactCaptureRequest& out)
{
    hypha::local_blind::HostClockProbeSnapshot clock;
    if (! hostClockProbe.read (clock) || ! clock.hasPosition
        || (clock.source != KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE
            && clock.source != KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE)
        || (! clock.playing && clock.source != KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE)
        || clock.callback == 0 || ! std::isfinite (clock.rate)
        || clock.rate < 8'000.0 || clock.rate > 768'000.0 || frames < 1)
        return false;
    const auto sampleRate = static_cast<std::int64_t> (std::llround (clock.rate));
    if (std::abs (clock.rate - static_cast<double> (sampleRate)) > 0.001
        || clock.position > std::numeric_limits<std::int64_t>::max() - sampleRate)
        return false;
    const auto nativeStart = clock.position + sampleRate;
    if (! localBlindCapture.reservePostRequest())
        return false;
    const juce::ScopedLock lock (handleLock);
    if (role != Role::Post || hyphaHandle == nullptr
        || std::abs (clock.rate - preparedFormat.sampleRate) > 0.001
        || clock.channels != static_cast<std::uint32_t> (preparedFormat.channelRoles.size()))
    {
        localBlindCapture.abandonPostRequest();
        return false;
    }
    KirinLocalBlindCaptureRequest request {};
    hypha::local_blind::ExactCaptureRequest decoded;
    if (! kirin_hypha_issue_local_blind_capture_request_v2 (
            hyphaHandle, captureGeneration, clock.callback, clock.source,
            clock.position, nativeStart, frames, &request)
        || ! decodeCaptureRequest (request, decoded)
        || ! localBlindCapture.commitPostRequest (decoded))
    {
        localBlindCapture.abandonPostRequest();
        return false;
    }
    out = std::move (decoded);
    return true;
}

hypha::local_blind::CaptureRequestPoll KirinHyphaProcessorBase::pollLocalBlindCaptureRequest (
    hypha::local_blind::ExactCaptureRequest& out) const
{
    using hypha::local_blind::CaptureRequestPoll;
    const juce::ScopedLock lock (handleLock);
    if (role != Role::Pre || hyphaHandle == nullptr)
        return CaptureRequestPoll::unavailable;
    KirinLocalBlindCaptureRequest request {};
    const auto status = kirin_hypha_poll_local_blind_capture_request_v2 (
        hyphaHandle, &request);
    if (status == KIRIN_LOCAL_BLIND_CAPTURE_REQUEST_CONTENDED)
        return CaptureRequestPoll::contended;
    if (status != KIRIN_LOCAL_BLIND_CAPTURE_REQUEST_CURRENT
        || ! decodeCaptureRequest (request, out))
        return CaptureRequestPoll::unavailable;
    return CaptureRequestPoll::current;
}

bool KirinHyphaProcessorBase::acknowledgeLocalBlindCaptureRequest (
    const std::string& requestId) const
{
    const juce::ScopedLock lock (handleLock);
    return role == Role::Pre && hyphaHandle != nullptr
        && kirin_hypha_ack_local_blind_capture_request (hyphaHandle, requestId.c_str());
}

bool KirinHyphaProcessorBase::localBlindCaptureIsArmed (const std::string& requestId) const
{
    const juce::ScopedLock lock (handleLock);
    return role == Role::Post && hyphaHandle != nullptr
        && kirin_hypha_local_blind_capture_is_armed (hyphaHandle, requestId.c_str());
}

juce::String KirinHyphaProcessorBase::pairDisplayName() const
{
    const auto shortId = persistPairInstanceId.substring (0, 8);
    if (persistPairName.isEmpty())
        return shortId.isEmpty() ? juce::String() : "PRE " + shortId;
    return persistPairName;
}

bool KirinHyphaProcessorBase::setPairCandidate (const juce::String& instanceId,
                                                const juce::String& name)
{
    {
        const juce::ScopedLock lock (handleLock);
        if (hyphaHandle == nullptr
            || ! kirin_hypha_select_pair_candidate (hyphaHandle, instanceId.toRawUTF8()))
            return false;
    }
    juce::String projectHash, selectedInstanceId;
    if (! pairedPreLocator (projectHash, selectedInstanceId))
    {
        const juce::ScopedLock lock (handleLock);
        if (hyphaHandle != nullptr)
            kirin_hypha_set_pair_target (hyphaHandle, "");
        persistPairName.clear();
        persistPairProjectHash.clear();
        persistPairInstanceId.clear();
        return false;
    }
    persistPairName = name;
    persistPairProjectHash = projectHash;
    persistPairInstanceId = selectedInstanceId;
    return true;
}

void KirinHyphaProcessorBase::clearPairCandidate()
{
    {
        const juce::ScopedLock lock (handleLock);
        if (hyphaHandle != nullptr)
            kirin_hypha_set_pair_target (hyphaHandle, "");
    }
    persistPairName.clear();
    persistPairProjectHash.clear();
    persistPairInstanceId.clear();
}

void KirinHyphaProcessorBase::restorePersistedPairUnderHandleLock()
{
    const bool exact = persistPairProjectHash.isNotEmpty() && persistPairInstanceId.isNotEmpty();
    if (exact && kirin_hypha_restore_pair_candidate_v2 (
                     hyphaHandle,
                     persistPairProjectHash.toRawUTF8(),
                     persistPairInstanceId.toRawUTF8(),
                     persistPairName.toRawUTF8()))
        return;
    persistPairName.clear();
    persistPairProjectHash.clear();
    persistPairInstanceId.clear();
    kirin_hypha_set_pair_target (hyphaHandle, "");
}
