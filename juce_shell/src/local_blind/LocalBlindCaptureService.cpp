#include "LocalBlindCaptureService.h"

#include <utility>

namespace hypha::local_blind
{
namespace
{
constexpr int activeServiceIntervalMs = 50;
constexpr int idlePreServiceIntervalMs = 500;
constexpr int idlePostServiceIntervalMs = 5'000;
}

struct LocalBlindCaptureService::Scheduler
{
    Scheduler() : thread ("Kirin Local Blind capture")
    { thread.startThread (juce::Thread::Priority::low); }
    ~Scheduler()
    {
        thread.removeAllClients();
        if (! thread.stopThread (-1))
            jassertfalse;
    }
    juce::TimeSliceThread thread;
};

LocalBlindCaptureService::LocalBlindCaptureService (CaptureSide sideIn, CaptureServiceHooks hooksIn)
    : side (sideIn), hooks (std::move (hooksIn)), owner (sideIn)
{
}

LocalBlindCaptureService::~LocalBlindCaptureService()
{
    stop();
}

void LocalBlindCaptureService::start (std::uint32_t sampleRate, int channels)
{
    stop();
    if (sampleRate < 8'000 || sampleRate > 768'000 || (channels != 1 && channels != 2))
        return;
    preparedSampleRate = sampleRate;
    preparedChannels = channels;
    const juce::ScopedLock lock (schedulerLock);
    scheduler = std::make_unique<juce::SharedResourcePointer<Scheduler>>();
    registered.store (true, std::memory_order_release);
    (*scheduler)->thread.addTimeSliceClient (this);
}

void LocalBlindCaptureService::stop()
{
    registered.store (false, std::memory_order_release);
    std::unique_ptr<juce::SharedResourcePointer<Scheduler>> stoppedScheduler;
    {
        const juce::ScopedLock lock (schedulerLock);
        stoppedScheduler = std::move (scheduler);
    }
    if (stoppedScheduler)
        (*stoppedScheduler)->thread.removeTimeSliceClient (this);
    owner.reset();
    {
        const juce::ScopedLock lock (submissionLock);
        submittedPostRequest.reset();
    }
    postRequestOccupied.store (false, std::memory_order_release);
    resetRequested.store (false, std::memory_order_release);
    preparedSampleRate = 0;
    preparedChannels = 0;
}

bool LocalBlindCaptureService::reservePostRequest() noexcept
{
    if (side != CaptureSide::post || ! running())
        return false;
    bool available = false;
    return postRequestOccupied.compare_exchange_strong (
        available, true, std::memory_order_acq_rel, std::memory_order_acquire);
}

bool LocalBlindCaptureService::commitPostRequest (ExactCaptureRequest request)
{
    if (side != CaptureSide::post || ! request.valid()
        || ! running() || ! postRequestOccupied.load (std::memory_order_acquire))
        return false;
    {
        const juce::ScopedLock lock (submissionLock);
        if (! running() || submittedPostRequest)
            return false;
        submittedPostRequest = std::move (request);
    }
    wake();
    return true;
}

void LocalBlindCaptureService::abandonPostRequest() noexcept
{
    {
        const juce::ScopedLock lock (submissionLock);
        submittedPostRequest.reset();
    }
    postRequestOccupied.store (false, std::memory_order_release);
}

void LocalBlindCaptureService::requestReset() noexcept
{
    resetRequested.store (true, std::memory_order_release);
    wake();
}

void LocalBlindCaptureService::wake()
{
    const juce::ScopedLock lock (schedulerLock);
    if (scheduler && running())
        (*scheduler)->thread.moveToFrontOfQueue (this);
}

int LocalBlindCaptureService::useTimeSlice()
{
    if (! running())
        return -1;
    if (resetRequested.exchange (false, std::memory_order_acq_rel))
    {
        owner.reset();
        abandonPostRequest();
    }

    const auto now = juce::Time::currentTimeMillis();
    if (side == CaptureSide::pre)
    {
        ExactCaptureRequest live;
        const bool hasLive = hooks.pollPreRequest && hooks.pollPreRequest (live);
        const auto state = owner.view().phase;
        if (state == CaptureOwnerPhase::failed)
        {
            if (! hasLive || ! owner.matches (live))
                owner.reset();
        }
        if (owner.view().phase == CaptureOwnerPhase::idle && hasLive
            && owner.beginPre (live, preparedSampleRate, preparedChannels, now))
        {
            const bool acknowledged = hooks.acknowledgePreRequest
                && hooks.acknowledgePreRequest (live.requestId);
            owner.confirmPreAcknowledgement (acknowledged);
        }
        else if (owner.view().phase == CaptureOwnerPhase::capturing
                 || owner.view().phase == CaptureOwnerPhase::complete)
        {
            owner.servicePre (hasLive ? &live : nullptr, now);
        }
    }
    else
    {
        if (owner.view().phase == CaptureOwnerPhase::idle)
        {
            std::optional<ExactCaptureRequest> next;
            {
                const juce::ScopedLock lock (submissionLock);
                next = std::move (submittedPostRequest);
                submittedPostRequest.reset();
            }
            if (next && ! owner.beginPost (*next))
            {
                owner.reset();
                postRequestOccupied.store (false, std::memory_order_release);
            }
        }
        const auto* request = owner.activeRequest();
        if (request != nullptr)
        {
            ExactPairBinding pair;
            const bool hasPair = hooks.currentPostPair && hooks.currentPostPair (pair);
            const bool peerArmed = hooks.postPeerArmed
                && hooks.postPeerArmed (request->requestId);
            owner.servicePost (peerArmed, hasPair ? &pair : nullptr,
                               preparedSampleRate, preparedChannels, now);
        }
        if (owner.view().phase == CaptureOwnerPhase::failed)
        {
            owner.reset();
            postRequestOccupied.store (false, std::memory_order_release);
        }
    }
    const auto phase = owner.view().phase;
    if (phase == CaptureOwnerPhase::capturing || phase == CaptureOwnerPhase::complete
        || phase == CaptureOwnerPhase::awaitingPeer)
        return activeServiceIntervalMs;
    return side == CaptureSide::pre ? idlePreServiceIntervalMs : idlePostServiceIntervalMs;
}
}
