#pragma once

#include "CaptureClockGuard.h"
#include "ExactRangeCapture.h"
#include "RtPublicationSlot.h"

namespace hypha::local_blind
{
// One non-RT owner publishes a preallocated capture to one Audio Thread producer. This slot
// carries role-local PCM only; it does not transfer PCM between PRE and POST, grant a lease, or
// authorize audition output. Destruction is allowed only after the host quiesces the callback.
class ExactRangeCaptureSlot final
{
public:
    bool publish (std::unique_ptr<ExactRangeCapture> capture,
                  std::uint8_t clockSource, std::int64_t clockPositionAtIssue,
                  std::int64_t nativeStart) noexcept
    {
        if (capture == nullptr || (clockSource != 1 && clockSource != 2)
            || clockPositionAtIssue > nativeStart
            || capture->range().start != nativeStart)
            return false;
        try
        {
            return storage.publish (std::make_unique<ClockBoundCapture> (
                std::move (capture), clockSource, clockPositionAtIssue, nativeStart));
        }
        catch (...)
        {
            return false;
        }
    }

    // Audio Thread only. The immutable published range supplies the control generation; transport
    // facts decide whether this exact attempt may continue before any PCM is accepted.
    bool process (const float* const* input, int channels,
                  const CaptureClockObservation& clock, std::uint32_t sampleRate) noexcept
    {
        return storage.withRealtime ([&] (ClockBoundCapture& value)
        {
            if (! clock.positionValid || ! clock.timelineActive || clock.bypassed)
                value.capture->invalidateFromProducer (CaptureFailure::transport);
            else if (! clock.realtime)
                value.capture->invalidateFromProducer (CaptureFailure::nonRealtime);
            else if (! value.clock.accept (clock))
                value.capture->invalidateFromProducer (CaptureFailure::clock);
            else
                value.capture->push (input, channels, clock.frames, clock.position,
                                     value.capture->range().generation,
                                     clock.realtime, sampleRate);
        });
    }

    ExactRangeCapture* control() noexcept
    {
        auto* value = storage.control();
        return value != nullptr ? value->capture.get() : nullptr;
    }
    const ExactRangeCapture* control() const noexcept
    {
        const auto* value = storage.control();
        return value != nullptr ? value->capture.get() : nullptr;
    }
    bool hasPublishedRealtime() const noexcept { return storage.hasPublishedRealtime(); }

    // The non-RT consumer may retain completed PCM through control() until preparation finishes.
    bool retireCompleted() noexcept
    {
        const auto* value = storage.control();
        return value != nullptr && value->capture->state() == CaptureState::complete
            && storage.retire();
    }

    bool cancelAndRetire() noexcept
    {
        auto* value = storage.control();
        if (value == nullptr)
            return false;
        value->capture->cancel();
        return storage.retire();
    }

    bool collect() noexcept { return storage.collect(); }
    bool hasStorage() const noexcept { return storage.hasStorage(); }

private:
    struct ClockBoundCapture
    {
        ClockBoundCapture (std::unique_ptr<ExactRangeCapture> captureIn,
                           std::uint8_t source, std::int64_t positionAtIssue,
                           std::int64_t start) noexcept
            : capture (std::move (captureIn)), clock (source, positionAtIssue, start) {}
        std::unique_ptr<ExactRangeCapture> capture;
        CaptureClockGuard clock;
    };
    RtPublicationSlot<ClockBoundCapture> storage;
};
}
