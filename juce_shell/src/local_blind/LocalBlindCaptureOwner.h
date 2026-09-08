#pragma once

#include "LocalBlindCaptureLane.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace hypha::local_blind
{
enum class CaptureOwnerPhase : unsigned char
{
    idle, awaitingPeer, capturing, complete, retired, paired, failed
};
enum class CaptureOwnerFailure : unsigned char
{
    none,
    invalidRequest,
    expired,
    peerRejected,
    stalePair,
    armRejected,
    captureFailed,
    receiptRejected
};

struct CaptureOwnerView
{
    CaptureOwnerPhase phase = CaptureOwnerPhase::idle;
    CaptureOwnerFailure failure = CaptureOwnerFailure::none;
    CaptureFailure captureFailure = CaptureFailure::none;
};

// One non-RT owner for one role-local lane. The PRE owner arms before acknowledging the exact
// request. The POST owner waits for that acknowledgement and rechecks the exact pair before it
// publishes its lane. Audio Thread access is limited to process().
class LocalBlindCaptureOwner final
{
public:
    explicit LocalBlindCaptureOwner (CaptureSide sideIn) noexcept : side (sideIn), lane (sideIn) {}

    bool beginPre (const ExactCaptureRequest& next, std::uint32_t sampleRate, int channels,
                   std::int64_t nowUnixMs) noexcept
    {
        if (side != CaptureSide::pre || currentPhase() != CaptureOwnerPhase::idle
            || ! install (next, sampleRate, channels, nowUnixMs))
            return false;
        phase.store (CaptureOwnerPhase::capturing, std::memory_order_release);
        return true;
    }

    void confirmPreAcknowledgement (bool acknowledged) noexcept
    {
        if (side == CaptureSide::pre && currentPhase() == CaptureOwnerPhase::capturing
            && ! acknowledged)
            fail (CaptureOwnerFailure::peerRejected);
    }

    bool beginPost (const ExactCaptureRequest& next) noexcept
    {
        if (side != CaptureSide::post || currentPhase() != CaptureOwnerPhase::idle
            || ! next.valid())
            return false;
        request = next;
        failure.store (CaptureOwnerFailure::none, std::memory_order_relaxed);
        captureFailure.store (CaptureFailure::none, std::memory_order_relaxed);
        phase.store (CaptureOwnerPhase::awaitingPeer, std::memory_order_release);
        return true;
    }

    void servicePre (const ExactCaptureRequest* liveRequest, std::int64_t nowUnixMs) noexcept
    {
        if (side != CaptureSide::pre || ! request
            || (currentPhase() != CaptureOwnerPhase::capturing
                && currentPhase() != CaptureOwnerPhase::complete
                && currentPhase() != CaptureOwnerPhase::retired))
            return;
        // Observe the Audio Thread's terminal fact before applying the admission deadline. A
        // completed exact range remains valid for non-RT finalization after that deadline.
        if (currentPhase() == CaptureOwnerPhase::capturing)
            observeCapture();
        if (currentPhase() == CaptureOwnerPhase::failed)
            return;
        if (currentPhase() == CaptureOwnerPhase::capturing && expired (nowUnixMs))
        { fail (CaptureOwnerFailure::expired); return; }
        if (liveRequest == nullptr || *liveRequest != *request)
        { fail (CaptureOwnerFailure::stalePair); return; }
    }

    void servicePost (bool peerArmed, const ExactPairBinding* livePair,
                      std::uint32_t sampleRate, int channels,
                      std::int64_t nowUnixMs) noexcept
    {
        if (side != CaptureSide::post || ! request)
            return;
        auto current = currentPhase();
        if (current != CaptureOwnerPhase::awaitingPeer
            && current != CaptureOwnerPhase::capturing
            && current != CaptureOwnerPhase::complete
            && current != CaptureOwnerPhase::paired)
            return;
        if (current == CaptureOwnerPhase::paired)
        {
            if (livePair == nullptr || *livePair != request->pair)
                fail (CaptureOwnerFailure::stalePair);
            return;
        }
        if (current == CaptureOwnerPhase::capturing)
        {
            observeCapture();
            current = currentPhase();
        }
        if (current == CaptureOwnerPhase::failed)
            return;
        if (livePair == nullptr || *livePair != request->pair)
        { fail (CaptureOwnerFailure::stalePair); return; }
        if (current == CaptureOwnerPhase::awaitingPeer)
        {
            if (expired (nowUnixMs)) { fail (CaptureOwnerFailure::expired); return; }
            if (! peerArmed)
                return;
            if (! lane.arm (*request, sampleRate, channels, nowUnixMs, byteBudget (*request)))
            { fail (CaptureOwnerFailure::armRejected); return; }
            phase.store (CaptureOwnerPhase::capturing, std::memory_order_release);
        }
        else if (current == CaptureOwnerPhase::capturing && expired (nowUnixMs))
        { fail (CaptureOwnerFailure::expired); return; }
        observeCapture();
    }

    bool process (const float* const* input, int channels,
                  const CaptureClockObservation& clock,
                  std::uint32_t sampleRate) noexcept
    {
        return lane.process (input, channels, clock, sampleRate);
    }

    CaptureOwnerView view() const noexcept
    {
        return { phase.load (std::memory_order_acquire),
                 failure.load (std::memory_order_acquire),
                 captureFailure.load (std::memory_order_acquire) };
    }
    bool matches (const ExactCaptureRequest& candidate) const noexcept
    { return request && *request == candidate; }
    const ExactCaptureRequest* activeRequest() const noexcept { return request ? &*request : nullptr; }
    const ExactRangeCapture* completedCapture() const noexcept { return lane.completedCapture(); }
    bool receipt (CaptureReceipt& out) const { return lane.receipt (out); }

    bool retireCompletedPre() noexcept
    {
        if (side != CaptureSide::pre || currentPhase() != CaptureOwnerPhase::complete
            || ! lane.retireFinal())
            return false;
        phase.store (CaptureOwnerPhase::retired, std::memory_order_release);
        return true;
    }

    bool sealCompletedPost() noexcept
    {
        if (side != CaptureSide::post || currentPhase() != CaptureOwnerPhase::complete)
            return false;
        phase.store (CaptureOwnerPhase::paired, std::memory_order_release);
        return true;
    }

    void rejectReceipt() noexcept { fail (CaptureOwnerFailure::receiptRejected); }

    void reset() noexcept
    {
        if (lane.hasActiveRequest())
            lane.cancel();
        lane.collect();
        request.reset();
        failure.store (CaptureOwnerFailure::none, std::memory_order_relaxed);
        captureFailure.store (CaptureFailure::none, std::memory_order_relaxed);
        phase.store (CaptureOwnerPhase::idle, std::memory_order_release);
    }

private:
    const CaptureSide side;
    LocalBlindCaptureLane lane;
    std::optional<ExactCaptureRequest> request;
    std::atomic<CaptureOwnerPhase> phase { CaptureOwnerPhase::idle };
    std::atomic<CaptureOwnerFailure> failure { CaptureOwnerFailure::none };
    std::atomic<CaptureFailure> captureFailure { CaptureFailure::none };
    static_assert (std::atomic<CaptureOwnerPhase>::is_always_lock_free);
    static_assert (std::atomic<CaptureOwnerFailure>::is_always_lock_free);
    static_assert (std::atomic<CaptureFailure>::is_always_lock_free);

    CaptureOwnerPhase currentPhase() const noexcept
    { return phase.load (std::memory_order_acquire); }

    static std::size_t byteBudget (const ExactCaptureRequest& value) noexcept
    {
        if (! value.valid())
            return 0;
        return static_cast<std::size_t> (value.frames)
            * static_cast<std::size_t> (value.channels) * sizeof (float);
    }

    bool install (const ExactCaptureRequest& next, std::uint32_t sampleRate, int channels,
                  std::int64_t nowUnixMs) noexcept
    {
        if (! next.valid())
        {
            failure.store (CaptureOwnerFailure::invalidRequest, std::memory_order_relaxed);
            phase.store (CaptureOwnerPhase::failed, std::memory_order_release);
            return false;
        }
        if (nowUnixMs <= 0 || nowUnixMs > next.expiresAtUnixMs)
        {
            failure.store (CaptureOwnerFailure::expired, std::memory_order_relaxed);
            phase.store (CaptureOwnerPhase::failed, std::memory_order_release);
            return false;
        }
        if (! lane.arm (next, sampleRate, channels, nowUnixMs, byteBudget (next)))
        {
            failure.store (CaptureOwnerFailure::armRejected, std::memory_order_relaxed);
            phase.store (CaptureOwnerPhase::failed, std::memory_order_release);
            return false;
        }
        request = next;
        failure.store (CaptureOwnerFailure::none, std::memory_order_relaxed);
        captureFailure.store (CaptureFailure::none, std::memory_order_relaxed);
        return true;
    }

    bool expired (std::int64_t nowUnixMs) const noexcept
    { return nowUnixMs <= 0 || nowUnixMs > request->expiresAtUnixMs; }

    void observeCapture() noexcept
    {
        if (currentPhase() != CaptureOwnerPhase::capturing)
            return;
        CaptureReceipt current;
        if (! lane.receipt (current) || current.state == CaptureState::pending)
            return;
        if (current.state == CaptureState::complete && current.failure == CaptureFailure::none)
            phase.store (CaptureOwnerPhase::complete, std::memory_order_release);
        else
        {
            captureFailure.store (current.failure, std::memory_order_relaxed);
            fail (CaptureOwnerFailure::captureFailed);
        }
    }

    void fail (CaptureOwnerFailure reason) noexcept
    {
        if (lane.hasActiveRequest())
            lane.cancel();
        lane.collect();
        failure.store (reason, std::memory_order_relaxed);
        phase.store (CaptureOwnerPhase::failed, std::memory_order_release);
    }
};
}
