#include "PluginProcessor.h"

#include "kirin_hypha_local_blind_result_ffi.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace
{
using namespace hypha::local_blind;

template <std::size_t Capacity>
std::string fixedString (const char (&source)[Capacity])
{
    const auto* end = std::find (std::begin (source), std::end (source), '\0');
    return end == std::end (source) ? std::string {} : std::string (source, end);
}

bool canonicalSha256 (const std::string& value)
{
    return value.size() == 64
        && std::all_of (value.begin(), value.end(), [] (char byte)
        {
            return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
        });
}

bool sampleCount (const ExactCaptureRequest& request, std::size_t& out)
{
    if (! request.valid())
        return false;
    const auto frames = static_cast<std::uint64_t> (request.frames);
    const auto count = frames * static_cast<std::uint64_t> (request.channels);
    if (count > std::numeric_limits<std::size_t>::max())
        return false;
    out = static_cast<std::size_t> (count);
    return true;
}

bool receiptMatchesRequest (const CaptureReceipt& receipt,
                            const ExactCaptureRequest& request) noexcept
{
    const auto& range = receipt.range;
    return receipt.pair == request.pair
        && receipt.clockGeneration == request.clockGeneration
        && receipt.side == CaptureSide::pre
        && range.generation == request.captureGeneration
        && range.sampleRate == request.sampleRate
        && range.channels == request.channels
        && range.start == request.preStart
        && range.frames == request.frames
        && receipt.state == CaptureState::complete
        && receipt.failure == CaptureFailure::none;
}

bool decodeReceipt (const KirinLocalBlindPreCaptureReceipt& source,
                    const ExactCaptureRequest& request,
                    CaptureReceipt& out, std::string& pcmSha256)
{
    std::size_t expectedSamples = 0;
    const auto requestId = fixedString (source.request_id);
    const auto sha256 = fixedString (source.pcm_sha256);
    if (! sampleCount (request, expectedSamples)
        || requestId != request.requestId || ! canonicalSha256 (sha256)
        || source.pair_generation != request.pair.generation
        || source.capture_generation != request.captureGeneration
        || source.clock_generation != request.clockGeneration
        || source.sample_rate != request.sampleRate
        || source.channels != static_cast<std::uint32_t> (request.channels)
        || source.start != request.preStart || source.frames != request.frames
        || source.sample_count != expectedSamples)
        return false;
    out = { request.pair, request.clockGeneration, CaptureSide::pre,
            { request.captureGeneration, request.sampleRate, request.channels,
              request.preStart, request.frames },
            CaptureState::complete, CaptureFailure::none };
    pcmSha256 = sha256;
    return true;
}

KirinLocalBlindPreCaptureReceipt encodeReceipt (
    const ExactCaptureRequest& request, const std::string& pcmSha256)
{
    KirinLocalBlindPreCaptureReceipt receipt {};
    std::size_t samples = 0;
    if (! sampleCount (request, samples) || ! canonicalSha256 (pcmSha256))
        return receipt;
    std::copy (request.requestId.begin(), request.requestId.end(), receipt.request_id);
    std::copy (pcmSha256.begin(), pcmSha256.end(), receipt.pcm_sha256);
    receipt.pair_generation = request.pair.generation;
    receipt.capture_generation = request.captureGeneration;
    receipt.clock_generation = request.clockGeneration;
    receipt.sample_rate = request.sampleRate;
    receipt.channels = static_cast<std::uint32_t> (request.channels);
    receipt.start = request.preStart;
    receipt.frames = request.frames;
    receipt.sample_count = samples;
    return receipt;
}
}

bool KirinHyphaProcessorBase::publishLocalBlindPreCapture (
    const hypha::local_blind::ExactCaptureRequest& request,
    const hypha::local_blind::CaptureReceipt& localReceipt,
    const std::vector<float>& pcm, std::string& pcmSha256) const
{
    if (! receiptMatchesRequest (localReceipt, request))
        return false;
    std::size_t samples = 0;
    if (! sampleCount (request, samples) || pcm.size() != samples)
        return false;
    const juce::ScopedLock lock (handleLock);
    if (role != Role::Pre || hyphaHandle == nullptr)
        return false;
    KirinLocalBlindPreCaptureReceipt receipt {};
    hypha::local_blind::CaptureReceipt decoded;
    std::string sha256;
    if (! kirin_hypha_publish_local_blind_pre_capture (
            hyphaHandle, request.requestId.c_str(), pcm.data(), pcm.size(), &receipt)
        || ! decodeReceipt (receipt, request, decoded, sha256))
        return false;
    pcmSha256 = std::move (sha256);
    return true;
}

bool KirinHyphaProcessorBase::readLocalBlindPreCapture (
    const hypha::local_blind::ExactCaptureRequest& request,
    hypha::local_blind::CaptureServiceHooks::ImportedPreCapture& imported) const
{
    std::size_t samples = 0;
    if (! sampleCount (request, samples))
        return false;
    std::vector<float> pcm (samples);
    KirinLocalBlindPreCaptureReceipt receipt {};
    {
        const juce::ScopedLock lock (handleLock);
        if (role != Role::Post || hyphaHandle == nullptr
            || ! kirin_hypha_read_local_blind_pre_capture (
                hyphaHandle, request.requestId.c_str(), pcm.data(), pcm.size(), &receipt))
            return false;
    }
    hypha::local_blind::CaptureReceipt decoded;
    std::string sha256;
    if (! decodeReceipt (receipt, request, decoded, sha256))
        return false;
    auto capture = hypha::local_blind::ExactRangeCapture::fromCompletedInterleaved (
        decoded.range, std::move (pcm), samples * sizeof (float));
    if (capture == nullptr)
        return false;
    imported.receipt = decoded;
    imported.capture = std::move (capture);
    imported.pcmSha256 = std::move (sha256);
    return true;
}

bool KirinHyphaProcessorBase::acknowledgeLocalBlindPreCapture (
    const hypha::local_blind::ExactCaptureRequest& request,
    const std::string& pcmSha256) const
{
    const auto receipt = encodeReceipt (request, pcmSha256);
    const juce::ScopedLock lock (handleLock);
    return role == Role::Post && hyphaHandle != nullptr
        && kirin_hypha_ack_local_blind_pre_capture (hyphaHandle, &receipt);
}

bool KirinHyphaProcessorBase::localBlindPreCaptureWasConsumed (
    const hypha::local_blind::ExactCaptureRequest& request,
    const std::string& pcmSha256) const
{
    const auto receipt = encodeReceipt (request, pcmSha256);
    const juce::ScopedLock lock (handleLock);
    return role == Role::Pre && hyphaHandle != nullptr
        && kirin_hypha_local_blind_pre_capture_was_consumed (hyphaHandle, &receipt);
}

void KirinHyphaProcessorBase::retireLocalBlindPreCapture (
    const hypha::local_blind::ExactCaptureRequest& request) const
{
    const juce::ScopedLock lock (handleLock);
    if (role == Role::Pre && hyphaHandle != nullptr)
        kirin_hypha_retire_local_blind_pre_capture (
            hyphaHandle, request.requestId.c_str());
}
