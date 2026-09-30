#include "PluginProcessor.h"
#include "kirin_hypha_local_blind_capture_ffi.h"
#include "reference_audition/ReferenceBlindSession.h"

#include <cmath>
#include <string>

// Live PRE/POST compare, stage 2 (INV-LC15): PIN fixes the latest four seconds of a live session
// and hands them to PRE / POST Blind as its captured pair. Admission, epochs, preparation, Gain
// Match, the trial and the return stay Local Blind's own; only the capture step differs: the pair
// comes from the live history and PRE's ring through the proven K instead of a future capture.
hypha::live_compare::LivePinResult KirinHyphaProcessorBase::pinLiveCompareForBlind (
    hypha::meter_context::MeterContext context)
{
    using Admission = hypha::local_blind::CaptureAdmission;
    hypha::live_compare::LivePinResult result;
    result.pin = hypha::live_compare::PinFailure::notProven;
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire)
        || ! liveCompare.authority.permitted()
        || liveCompareAdmission (false) != hypha::live_compare::StartResult::started)
        return result;
    const auto* mapping = liveCompare.ring.control();
    if (mapping == nullptr || mapping->ring() == nullptr)
        return result;
    const auto rate = mapping->rate();
    const int channels = static_cast<int> (preparedFormat.channelRoles.size());
    auto pin = hypha::live_compare::pinLatest (*mapping->ring(), liveCompare.renderer,
                                               static_cast<std::int64_t> (rate) * 4, channels);
    result.pin = pin.failure;
    if (! pin.ok())
        return result;

    result.admission = localBlindCaptureAvailability();
    hypha::local_blind::HostClockProbeSnapshot clock;
    if (result.admission == Admission::ready && ! hostClockProbe.read (clock))
        result.admission = Admission::clockUnavailable;
    if (result.admission != Admission::ready)
        return result;
    std::uint64_t scopeEpoch = 0;
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController && ! referenceAuditionController->reserveLocalBlind())
    {
        result.admission = Admission::referenceBusy;
        return result;
    }
   #endif
    bool admitted = false;
    {
        const juce::ScopedLock lock (handleLock);
        admitted = hyphaHandle != nullptr && kirin_hypha_begin_local_blind (hyphaHandle, &scopeEpoch);
    }
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (referenceAuditionController)
    {
        if (admitted) referenceAuditionController->bindLocalBlind (scopeEpoch);
        else referenceAuditionController->releaseLocalBlind (0);
    }
   #endif
    if (! admitted)
    {
        result.admission = Admission::admissionFailed;
        return result;
    }

    const auto serial = localBlindProductSerial.fetch_add (1, std::memory_order_acq_rel) + 1;
    const auto generation = (static_cast<std::uint64_t> (juce::Time::currentTimeMillis()) << 16u) | (serial & 0xffffu);
    const auto policy = context == hypha::meter_context::MeterContext::trackStem
        ? hypha::local_blind::GainMatchPolicy::exactTrackEventEnergyV1
        : hypha::local_blind::GainMatchPolicy::alignedActiveBlocksV1;
    hypha::local_blind::ExactCaptureRequest request;
    if (! liveCompare.authority.permitted() || generation == 0 || ! localBlindPairBinding (request.pair)
        || ! localBlindProductSession.beginCapture (
               scopeEpoch, generation, policy,
               { clock.source, clock.presentationSource, clock.inputLatency, clock.outputLatency,
                 clock.hasInputLatency, clock.hasOutputLatency }))
    {
        releaseLocalBlindProductScope (scopeEpoch);
        result.admission = Admission::captureBusy;
        return result;
    }
    request.requestId = juce::Uuid().toDashedString().toStdString();
    request.captureGeneration = generation;
    request.clockGeneration = generation;
    request.clockSource = clock.source;
    request.clockPositionAtIssue = pin.projectStart;
    request.sampleRate = rate;
    request.channels = channels;
    request.nativeStart = pin.projectStart;
    request.frames = pin.frames;
    const hypha::local_blind::CaptureRange range { generation, rate, channels, pin.projectStart, pin.frames };
    const auto budget = static_cast<std::size_t> (pin.frames) * static_cast<std::size_t> (channels) * sizeof (float) * 2u;
    const auto post = hypha::local_blind::ExactRangeCapture::fromCompletedInterleaved (range, std::move (pin.post), budget);
    const auto pre = hypha::local_blind::ExactRangeCapture::fromCompletedInterleaved (range, std::move (pin.pre), budget);
    if (post == nullptr || pre == nullptr
        || ! localBlindProductSession.acceptCapturedPair (request, *post, *pre, hypha::reference_audition::secureRandomBit))
    {
        localBlindProductSession.failCaptureRequest();
        startTimer (50);
        result.admission = Admission::requestFailed;
        return result;
    }
    startTimer (50);
    result.pinned = true;
    return result;
}
