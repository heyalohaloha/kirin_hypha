#pragma once

#include "ExactRangeCaptureSlot.h"
#include "PairCaptureBarrier.h"

#include <memory>
#include <optional>
#include <utility>

namespace hypha::local_blind
{
// One role-local, single-owner capture lane. Control methods run on one non-RT thread; process()
// is the only Audio Thread entry. An accepted request remains bound to its original side, pair,
// clock generation, native range and prepared format until it is retired or cancelled.
class LocalBlindCaptureLane final
{
public:
    explicit LocalBlindCaptureLane (CaptureSide captureSide) noexcept : side (captureSide) {}

    bool arm (const ExactCaptureRequest& request, std::uint32_t preparedSampleRate,
              int preparedChannels, std::int64_t nowUnixMs,
              std::size_t byteBudget) noexcept
    {
        if (activeRequest || ! request.valid() || nowUnixMs <= 0
            || nowUnixMs > request.expiresAtUnixMs
            || request.sampleRate != preparedSampleRate
            || request.channels != preparedChannels)
            return false;
        try
        {
            auto requestCopy = request;
            auto capture = std::make_unique<ExactRangeCapture> (rangeFor (request), byteBudget);
            activeRequest.emplace (std::move (requestCopy));
            if (! slot.publish (std::move (capture), request.clockSource,
                                request.clockPositionAtIssue,
                                request.nativeStart))
            {
                activeRequest.reset();
                return false;
            }
            return true;
        }
        catch (...)
        {
            activeRequest.reset();
            return false;
        }
    }

    // Audio Thread only. Returns whether a capture object owned this callback observation.
    bool process (const float* const* input, int channels,
                  const CaptureClockObservation& clock,
                  std::uint32_t sampleRate) noexcept
    {
        return slot.process (input, channels, clock, sampleRate);
    }

    bool receipt (CaptureReceipt& out) const
    {
        const auto* capture = slot.control();
        if (! activeRequest || capture == nullptr)
            return false;
        out = { activeRequest->pair, activeRequest->clockGeneration, side, capture->range(),
                capture->state(), capture->failure() };
        return true;
    }

    const ExactRangeCapture* completedCapture() const noexcept
    {
        const auto* capture = slot.control();
        return capture != nullptr && capture->state() == CaptureState::complete ? capture : nullptr;
    }

    bool retireFinal() noexcept
    {
        const auto* capture = slot.control();
        if (capture == nullptr || capture->state() == CaptureState::pending)
            return false;
        const bool retired = capture->state() == CaptureState::complete
            ? slot.retireCompleted() : slot.cancelAndRetire();
        if (retired)
            activeRequest.reset();
        return retired;
    }

    bool cancel() noexcept
    {
        if (! slot.cancelAndRetire())
            return false;
        activeRequest.reset();
        return true;
    }

    bool collect() noexcept { return slot.collect(); }
    bool hasActiveRequest() const noexcept { return activeRequest.has_value(); }
    CaptureSide captureSide() const noexcept { return side; }

private:
    CaptureRange rangeFor (const ExactCaptureRequest& request) const noexcept
    {
        return { request.captureGeneration, request.sampleRate, request.channels,
                 request.nativeStart, request.frames };
    }

    const CaptureSide side;
    std::optional<ExactCaptureRequest> activeRequest;
    ExactRangeCaptureSlot slot;
};
}
