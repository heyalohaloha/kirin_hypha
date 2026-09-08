#pragma once

#include "LocalBlindCaptureOwner.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include <juce_core/juce_core.h>

namespace hypha::local_blind
{
struct CaptureServiceHooks
{
    std::function<bool (ExactCaptureRequest&)> pollPreRequest;
    std::function<bool (const std::string&)> acknowledgePreRequest;
    std::function<bool (const std::string&)> postPeerArmed;
    std::function<bool (ExactPairBinding&)> currentPostPair;
    std::function<bool (const ExactCaptureRequest&, const CaptureReceipt&,
                        const std::vector<float>&, std::string&)> publishPreCapture;
    struct ImportedPreCapture
    {
        CaptureReceipt receipt;
        std::unique_ptr<ExactRangeCapture> capture;
        std::string pcmSha256;
    };
    std::function<bool (const ExactCaptureRequest&, ImportedPreCapture&)> readPreCapture;
    std::function<bool (const ExactCaptureRequest&, const std::string&)> acknowledgePreCapture;
    std::function<bool (const ExactCaptureRequest&, const std::string&)> preCaptureConsumed;
    std::function<void (const ExactCaptureRequest&)> retirePreCapture;
};

// All instances in one plugin module share one sleeping scheduler thread. Idle PRE discovery is
// bounded and slow; an explicit POST request wakes its client and active handshakes poll faster.
// Only this callback changes its capture owner.
class LocalBlindCaptureService final : private juce::TimeSliceClient
{
public:
    LocalBlindCaptureService (CaptureSide, CaptureServiceHooks);
    ~LocalBlindCaptureService() override;

    void start (std::uint32_t sampleRate, int channels);
    void stop();
    bool running() const noexcept { return registered.load (std::memory_order_acquire); }

    bool reservePostRequest() noexcept;
    bool commitPostRequest (ExactCaptureRequest);
    void abandonPostRequest() noexcept;
    void requestReset() noexcept;

    bool process (const float* const* input, int channels,
                  const CaptureClockObservation& clock,
                  std::uint32_t sampleRate) noexcept
    {
        return owner.process (input, channels, clock, sampleRate);
    }

    CaptureOwnerView view() const noexcept { return owner.view(); }
    bool capturePairReady() const noexcept
    { return pairReady.load (std::memory_order_acquire); }

private:
    struct Scheduler;
    int useTimeSlice() override;
    void wake();
    void servicePre (std::int64_t nowUnixMs);
    void servicePost (std::int64_t nowUnixMs);
    void clearAttemptState();

    const CaptureSide side;
    const CaptureServiceHooks hooks;
    LocalBlindCaptureOwner owner;
    std::uint32_t preparedSampleRate = 0;
    int preparedChannels = 0;
    juce::CriticalSection schedulerLock;
    juce::CriticalSection submissionLock;
    std::optional<ExactCaptureRequest> submittedPostRequest;
    std::atomic<bool> postRequestOccupied { false };
    std::atomic<bool> resetRequested { false };
    std::atomic<bool> registered { false };
    std::atomic<bool> pairReady { false };
    std::unique_ptr<juce::SharedResourcePointer<Scheduler>> scheduler;
    std::optional<PairCaptureBarrier> pairBarrier;
    CaptureServiceHooks::ImportedPreCapture importedPre;
    bool postReceiptAccepted = false;
    bool preReceiptAccepted = false;
    bool preAcknowledged = false;
    bool prePublished = false;
    std::string prePublishedSha256;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LocalBlindCaptureService)
};
}
