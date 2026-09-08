#include "LocalBlindCaptureService.h"

#include <utility>

namespace hypha::local_blind
{
namespace
{
constexpr int activeServiceIntervalMs = 50;
constexpr int idlePreServiceIntervalMs = 500;
constexpr int idlePostServiceIntervalMs = 5'000;
constexpr unsigned int maximumPrePublishFailures = 20;
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

LocalBlindCaptureService::LocalBlindCaptureService (
    CaptureSide sideIn, CaptureServiceHooks hooksIn, std::int64_t finalizationTimeoutMsIn)
    : side (sideIn), hooks (std::move (hooksIn)),
      finalizationTimeoutMs (finalizationTimeoutMsIn > 0 ? finalizationTimeoutMsIn : 30'000),
      owner (sideIn)
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
    clearAttemptState();
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

#if JUCE_DEBUG
CapturePairComparison LocalBlindCaptureService::capturePairComparison() const
{
    const juce::ScopedLock lock (comparisonLock);
    return comparison;
}
#endif

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
        clearAttemptState();
        owner.reset();
        abandonPostRequest();
    }

    const auto now = juce::Time::currentTimeMillis();
    if (side == CaptureSide::pre)
        servicePre (now);
    else
        servicePost (now);
    const auto phase = owner.view().phase;
    if (phase == CaptureOwnerPhase::capturing || phase == CaptureOwnerPhase::complete
        || phase == CaptureOwnerPhase::awaitingPeer || phase == CaptureOwnerPhase::paired)
        return activeServiceIntervalMs;
    return side == CaptureSide::pre ? idlePreServiceIntervalMs : idlePostServiceIntervalMs;
}

void LocalBlindCaptureService::servicePre (std::int64_t now)
{
    ExactCaptureRequest live;
    const bool hasLive = hooks.pollPreRequest && hooks.pollPreRequest (live);
    const auto terminal = owner.view().phase;
    if ((terminal == CaptureOwnerPhase::failed || terminal == CaptureOwnerPhase::retired)
        && (! hasLive || ! owner.matches (live)))
    {
        clearAttemptState();
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
             || owner.view().phase == CaptureOwnerPhase::complete
             || owner.view().phase == CaptureOwnerPhase::retired)
    {
        owner.servicePre (hasLive ? &live : nullptr, now);
    }

    if (owner.view().phase == CaptureOwnerPhase::complete && ! prePublished)
    {
        CaptureReceipt receipt;
        const auto* capture = owner.completedCapture();
        const auto* pcm = capture != nullptr ? capture->completedPcm() : nullptr;
        std::string sha256;
        const bool published = pcm != nullptr && owner.receipt (receipt)
            && hooks.publishPreCapture
            && hooks.publishPreCapture (*owner.activeRequest(), receipt, *pcm, sha256)
            && ! sha256.empty();
        if (published)
        {
            prePublished = true;
            prePublishedSha256 = std::move (sha256);
        }
        else if (++prePublishFailures >= maximumPrePublishFailures)
            owner.rejectReceipt();
    }
    const auto* request = owner.activeRequest();
    if (owner.view().phase == CaptureOwnerPhase::failed && ! preFailurePublished
        && request != nullptr && hooks.publishPreFailure)
        preFailurePublished = hooks.publishPreFailure (*request, owner.view());
    if (prePublished && request != nullptr && hooks.preCaptureConsumed
        && hooks.preCaptureConsumed (*request, prePublishedSha256)
        && owner.retireCompletedPre())
    {
        if (hooks.retirePreCapture)
            hooks.retirePreCapture (*request);
        prePublished = false;
        prePublishedSha256.clear();
    }
}

void LocalBlindCaptureService::servicePost (std::int64_t now)
{
    if (owner.view().phase == CaptureOwnerPhase::idle)
    {
        std::optional<ExactCaptureRequest> next;
        {
            const juce::ScopedLock lock (submissionLock);
            next = std::move (submittedPostRequest);
            submittedPostRequest.reset();
        }
        if (next)
        {
            clearAttemptState();
            if (owner.beginPost (*next))
                pairBarrier.emplace (*next);
            else
            {
                owner.reset();
                postRequestOccupied.store (false, std::memory_order_release);
            }
        }
    }
    const auto* request = owner.activeRequest();
    if (request == nullptr)
        return;

    ExactPairBinding pair;
    const bool hasPair = hooks.currentPostPair && hooks.currentPostPair (pair);
    const bool peerArmed = owner.view().phase == CaptureOwnerPhase::paired
        || (hooks.postPeerArmed && hooks.postPeerArmed (request->requestId));
    owner.servicePost (peerArmed, hasPair ? &pair : nullptr,
                       preparedSampleRate, preparedChannels, now);

    if (owner.view().phase == CaptureOwnerPhase::complete && postCompletedAtUnixMs == 0)
        postCompletedAtUnixMs = now;

    if (owner.view().phase != CaptureOwnerPhase::paired && hooks.readPreFailure)
    {
        CaptureOwnerView peerFailure;
        if (hooks.readPreFailure (*request, peerFailure))
            owner.rejectPreFailure (peerFailure.failure, peerFailure.captureFailure);
    }

    if (owner.view().phase == CaptureOwnerPhase::complete && pairBarrier)
    {
        if (! postReceiptAccepted)
        {
            CaptureReceipt receipt;
            if (! owner.receipt (receipt) || ! pairBarrier->accept (receipt))
                owner.rejectReceipt();
            else
                postReceiptAccepted = true;
        }
        if (postReceiptAccepted && ! preReceiptAccepted && hooks.readPreCapture)
        {
            CaptureServiceHooks::ImportedPreCapture imported;
            if (hooks.readPreCapture (*request, imported))
            {
                const auto* pcm = imported.capture != nullptr
                    ? imported.capture->completedPcm() : nullptr;
                const auto& expected = pairBarrier->range (CaptureSide::pre);
                const auto& actual = imported.capture != nullptr
                    ? imported.capture->range() : expected;
                const bool exactCapture = pcm != nullptr
                    && actual.generation == expected.generation
                    && actual.sampleRate == expected.sampleRate
                    && actual.channels == expected.channels
                    && actual.start == expected.start && actual.frames == expected.frames;
                if (! exactCapture || ! pairBarrier->accept (imported.receipt))
                    owner.rejectReceipt();
                else
                {
                    importedPre = std::move (imported);
                    preReceiptAccepted = true;
                }
            }
        }
        if (preReceiptAccepted && pairBarrier->state() == PairCaptureState::complete
            && ! preAcknowledged && hooks.acknowledgePreCapture)
            preAcknowledged = hooks.acknowledgePreCapture (
                *request, importedPre.pcmSha256);
        if (preAcknowledged && owner.sealCompletedPost())
        {
#if JUCE_DEBUG
            CapturePairComparison next;
            const auto* postCapture = owner.completedCapture();
            const auto* prePcm = importedPre.capture != nullptr
                ? importedPre.capture->completedPcm() : nullptr;
            const auto* postPcm = postCapture != nullptr
                ? postCapture->completedPcm() : nullptr;
            if (postCapture != nullptr && prePcm != nullptr && postPcm != nullptr)
                next = compareCapturePair (postCapture->range(), *prePcm, *postPcm);
            const juce::ScopedLock lock (comparisonLock);
            comparison = next;
#endif
            pairReady.store (true, std::memory_order_release);
        }
    }
    // Admission expiry cannot discard PCM that already completed on the audio timeline. A
    // separate finalization deadline bounds a vanished PRE or unavailable result transport.
    if (owner.view().phase == CaptureOwnerPhase::complete && postCompletedAtUnixMs > 0
        && now >= postCompletedAtUnixMs
        && now - postCompletedAtUnixMs >= finalizationTimeoutMs)
        owner.rejectReceipt();
    if (owner.view().phase == CaptureOwnerPhase::failed)
    {
        clearAttemptState();
        postRequestOccupied.store (false, std::memory_order_release);
    }
}

void LocalBlindCaptureService::clearAttemptState()
{
    const auto* request = owner.activeRequest();
    if (side == CaptureSide::pre && (prePublished || preFailurePublished) && request != nullptr
        && hooks.retirePreCapture)
        hooks.retirePreCapture (*request);
    prePublished = false;
    preFailurePublished = false;
    prePublishFailures = 0;
    postCompletedAtUnixMs = 0;
    prePublishedSha256.clear();
    pairReady.store (false, std::memory_order_release);
    pairBarrier.reset();
    importedPre = {};
    postReceiptAccepted = false;
    preReceiptAccepted = false;
    preAcknowledged = false;
#if JUCE_DEBUG
    const juce::ScopedLock lock (comparisonLock);
    comparison = {};
#endif
}
}
