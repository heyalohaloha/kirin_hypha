#include "PluginProcessor.h"

#include "kirin_hypha_local_blind_capture_ffi.h"
#include "kirin_hypha_pair_snapshot_ffi.h"

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
    decoded.sampleRate = source.sample_rate;
    decoded.channels = static_cast<int> (source.channels);
    decoded.preStart = source.pre_start;
    decoded.postStart = source.post_start;
    decoded.frames = source.frames;
    decoded.expiresAtUnixMs = source.expires_at_unix_ms;
    if (! decoded.valid())
        return false;
    out = std::move (decoded);
    return true;
}
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
    std::uint64_t captureGeneration, std::uint64_t clockGeneration,
    std::int64_t preStart, std::int64_t postStart, std::int64_t frames,
    hypha::local_blind::ExactCaptureRequest& out) const
{
    const juce::ScopedLock lock (handleLock);
    if (role != Role::Post || hyphaHandle == nullptr)
        return false;
    KirinLocalBlindCaptureRequest request {};
    return kirin_hypha_issue_local_blind_capture_request (
               hyphaHandle, captureGeneration, clockGeneration,
               preStart, postStart, frames, &request)
        && decodeCaptureRequest (request, out);
}

bool KirinHyphaProcessorBase::pollLocalBlindCaptureRequest (
    hypha::local_blind::ExactCaptureRequest& out) const
{
    const juce::ScopedLock lock (handleLock);
    if (role != Role::Pre || hyphaHandle == nullptr)
        return false;
    KirinLocalBlindCaptureRequest request {};
    return kirin_hypha_poll_local_blind_capture_request (hyphaHandle, &request)
        && decodeCaptureRequest (request, out);
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
