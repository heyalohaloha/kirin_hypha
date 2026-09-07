#pragma once

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
    bool publish (std::unique_ptr<ExactRangeCapture> capture) noexcept
    {
        return storage.publish (std::move (capture));
    }

    // Audio Thread only. Input is copied into preallocated storage and is never modified.
    bool push (const float* const* input, int channels, int frames, std::int64_t position,
               std::uint64_t captureGeneration, bool realtime,
               std::uint32_t sampleRate) noexcept
    {
        return storage.withRealtime ([&] (ExactRangeCapture& capture)
        {
            capture.push (input, channels, frames, position, captureGeneration,
                          realtime, sampleRate);
        });
    }

    // Audio Thread only. The immutable published range supplies the control generation; transport
    // facts decide whether this exact attempt may continue before any PCM is accepted.
    bool process (const float* const* input, int channels, int frames, std::int64_t position,
                  bool positionValid, bool timelineActive, bool bypassed, bool realtime,
                  std::uint32_t sampleRate) noexcept
    {
        return storage.withRealtime ([&] (ExactRangeCapture& capture)
        {
            if (! positionValid || ! timelineActive || bypassed)
                capture.invalidateFromProducer (CaptureFailure::transport);
            else
                capture.push (input, channels, frames, position, capture.range().generation,
                              realtime, sampleRate);
        });
    }

    ExactRangeCapture* control() noexcept { return storage.control(); }
    const ExactRangeCapture* control() const noexcept { return storage.control(); }
    bool hasPublishedRealtime() const noexcept { return storage.hasPublishedRealtime(); }

    // The non-RT consumer may retain completed PCM through control() until preparation finishes.
    bool retireCompleted() noexcept
    {
        auto* capture = storage.control();
        return capture != nullptr && capture->state() == CaptureState::complete
            && storage.retire();
    }

    bool cancelAndRetire() noexcept
    {
        auto* capture = storage.control();
        if (capture == nullptr)
            return false;
        capture->cancel();
        return storage.retire();
    }

    bool collect() noexcept { return storage.collect(); }
    bool hasStorage() const noexcept { return storage.hasStorage(); }

private:
    RtPublicationSlot<ExactRangeCapture> storage;
};
}
