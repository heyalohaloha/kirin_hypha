#pragma once

#include "LocalBlindEpochSnapshot.h"
#include "LocalBlindPreparation.h"
#include "LocalBlindSlot.h"
#include "PairCaptureBarrier.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

namespace hypha::local_blind
{
enum class ProductSessionPhase : unsigned char
{
    idle,
    capturing,
    preparing,
    ready,
    armed,
    listening,
    revealed,
    returnPending,
    returned,
    failed
};

enum class ProductSessionFailure : unsigned char
{
    none,
    captureRequest,
    captureResult,
    preparation,
    publication,
    pairChanged
};

struct ProductSessionView
{
    ProductSessionPhase phase = ProductSessionPhase::idle;
    ProductSessionFailure failure = ProductSessionFailure::none;
    TrialView trial;
    std::uint32_t sampleRate = 0;
    int channels = 0;
    std::int64_t start = 0;
    std::int64_t frames = 0;
    double fixedPreGainDb = 0.0;
    double lowerPostGainDb = 0.0;
    std::uint64_t matchedBlocks = 0;
};

// Owns one admitted product trial from exact capture through an audio-confirmed return to A.
// Capture preparation and UI commands are serialized off RT. The Audio Thread only reads the
// immutable publication slot and epoch snapshot. The release callback is never called on RT.
class LocalBlindProductSession final
{
public:
    using ReleaseScope = std::function<bool (std::uint64_t)>;

    explicit LocalBlindProductSession (ReleaseScope releaseScopeIn)
        : releaseScope (std::move (releaseScopeIn)) {}

    bool beginCapture (std::uint64_t scopeEpoch, std::uint64_t captureGeneration) noexcept;
    void failCaptureRequest() noexcept;
    bool acceptCapturedPair (const ExactCaptureRequest&, const ExactRangeCapture& post,
                             const ExactRangeCapture& pre,
                             const std::function<bool()>& secureAssignmentBit) noexcept;

    bool start (bool approveLowerPost = false) noexcept;
    bool select (int stimulus) noexcept;
    bool answer (TrialAnswer) noexcept;
    bool reveal() noexcept;
    void stop() noexcept;
    void requestNormalReturn() noexcept;
    void invalidate() noexcept;
    void validatePair (const ExactPairBinding*) noexcept;

    ProductSessionView view() const noexcept;
    bool needsService() const noexcept;
    void service() noexcept;

    bool hasPublishedRealtime() const noexcept { return output.hasPublishedRealtime(); }
    bool render (float* const* data, int outputChannels, int frames, TrialBlock block) noexcept
    {
        block.epochs = epochs.read();
        return output.render (data, outputChannels, frames, block);
    }

private:
    ProductSessionView viewUnderLock() const noexcept;
    void markFailed (ProductSessionFailure) noexcept;
    static ProductSessionPhase trialPhase (TrialPhase) noexcept;

    ReleaseScope releaseScope;
    mutable std::mutex controlLock;
    LocalBlindSlot output;
    LocalBlindEpochSnapshot epochs;
    ProductSessionPhase basePhase = ProductSessionPhase::idle;
    ProductSessionFailure failure = ProductSessionFailure::none;
    std::uint64_t scopeEpoch = 0;
    std::uint64_t expectedCaptureGeneration = 0;
    ExactPairBinding capturedPair;
    bool releasePending = false;
    std::uint32_t sampleRate = 0;
    int channels = 0;
    std::int64_t startSample = 0;
    std::int64_t frameCount = 0;
    double fixedPreGainDb = 0.0;
    double lowerPostGainDb = 0.0;
    std::uint64_t matchedBlocks = 0;
};
}
