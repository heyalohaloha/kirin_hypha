#include "LocalBlindProductSession.h"

#include <algorithm>
#include <limits>

namespace hypha::local_blind
{
bool LocalBlindProductSession::beginCapture (
    std::uint64_t nextScopeEpoch, std::uint64_t nextCaptureGeneration,
    GainMatchPolicy nextGainPolicy, TrialClockSignature nextClock) noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    output.collect();
    if (nextScopeEpoch == 0 || nextCaptureGeneration == 0 || scopeEpoch != 0
        || output.hasStorage() || releasePending)
        return false;
    scopeEpoch = nextScopeEpoch;
    expectedCaptureGeneration = nextCaptureGeneration;
    gainPolicy = nextGainPolicy;
    admittedClock = nextClock;
    capturedPair = {};
    failure = ProductSessionFailure::none;
    basePhase = ProductSessionPhase::capturing;
    sampleRate = 0;
    channels = 0;
    startSample = 0;
    frameCount = 0;
    fixedPreGainDb = 0.0;
    lowerPostGainDb = 0.0;
    matchedAnalysisUnits = 0;
    return true;
}

void LocalBlindProductSession::markFailed (ProductSessionFailure reason) noexcept
{
    failure = reason;
    basePhase = ProductSessionPhase::failed;
    releasePending = scopeEpoch != 0;
    expectedCaptureGeneration = 0;
}

void LocalBlindProductSession::failCaptureRequest() noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    if (basePhase == ProductSessionPhase::capturing && ! output.hasStorage())
        markFailed (ProductSessionFailure::captureRequest);
}

bool LocalBlindProductSession::acceptCapturedPair (
    const ExactCaptureRequest& request, const ExactRangeCapture& post,
    const ExactRangeCapture& pre, const std::function<bool()>& random) noexcept
{
    std::uint64_t admittedScope = 0;
    GainMatchPolicy admittedGainPolicy = GainMatchPolicy::alignedActiveBlocksV1;
    TrialClockSignature clock;
    {
        const std::lock_guard<std::mutex> lock (controlLock);
        if (basePhase != ProductSessionPhase::capturing || scopeEpoch == 0
            || request.captureGeneration != expectedCaptureGeneration)
            return false;
        basePhase = ProductSessionPhase::preparing;
        admittedScope = scopeEpoch;
        admittedGainPolicy = gainPolicy;
        clock = admittedClock;
    }

    TrialFormat format;
    format.epochs = { admittedScope, request.pair.generation,
                      request.captureGeneration, request.clockGeneration };
    format.sampleRate = request.sampleRate;
    format.channels = request.channels;
    format.start = request.nativeStart;
    format.frames = request.frames;
    format.clock = clock;
    format.transitionFrames = static_cast<std::uint32_t> (
        std::min<std::int64_t> (request.sampleRate / 200u, request.frames / 2));
    // Each hidden side must complete one native pass. Exact wraps are admitted only when the
    // observed sample positions close at this immutable range, never from a PPQ conversion.
    format.minimumHeardFrames = static_cast<std::uint64_t> (request.frames);
    format.exactLoopAllowed = true;

    const auto frames = static_cast<std::uint64_t> (request.frames);
    const auto chans = static_cast<std::uint64_t> (request.channels);
    const auto max = static_cast<std::uint64_t> ((std::numeric_limits<std::size_t>::max)());
    if (frames == 0 || chans == 0 || frames > max / chans / sizeof (float) / 2u)
    {
        const std::lock_guard<std::mutex> lock (controlLock);
        markFailed (ProductSessionFailure::preparation);
        return true;
    }
    const auto budget = static_cast<std::size_t> (frames * chans * sizeof (float) * 2u);
    auto prepared = prepareLocalBlindCandidate (
        post, pre, format, request.nativeStart, budget, admittedGainPolicy, random);

    const std::lock_guard<std::mutex> lock (controlLock);
    if (scopeEpoch != admittedScope || expectedCaptureGeneration != request.captureGeneration
        || basePhase != ProductSessionPhase::preparing)
        return false;
    if (! prepared.trial)
    {
        markFailed (ProductSessionFailure::preparation);
        return true;
    }

    epochs.publish (format.epochs);
    if (! output.publish (std::move (prepared.trial)))
    {
        epochs.publish ({});
        markFailed (ProductSessionFailure::publication);
        return true;
    }
    sampleRate = request.sampleRate;
    channels = request.channels;
    startSample = request.nativeStart;
    frameCount = request.frames;
    fixedPreGainDb = prepared.fixedPreGainDb;
    lowerPostGainDb = prepared.lowerPostGainDb;
    matchedAnalysisUnits = prepared.matchedAnalysisUnits;
    capturedPair = request.pair;
    expectedCaptureGeneration = 0;
    basePhase = ProductSessionPhase::ready;
    return true;
}

bool LocalBlindProductSession::start (bool approve) noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    auto* trial = output.control();
    return trial != nullptr && trial->start (approve);
}

bool LocalBlindProductSession::select (int stimulus) noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    auto* trial = output.control();
    return trial != nullptr && trial->select (stimulus);
}

bool LocalBlindProductSession::answer (TrialAnswer value) noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    auto* trial = output.control();
    return trial != nullptr && trial->answer (value);
}

bool LocalBlindProductSession::reveal() noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    auto* trial = output.control();
    return trial != nullptr && trial->reveal();
}

void LocalBlindProductSession::stop() noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    if (auto* trial = output.control()) trial->stop();
}

void LocalBlindProductSession::requestNormalReturn() noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    if (auto* trial = output.control()) trial->requestNormalReturn();
}

void LocalBlindProductSession::invalidate() noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    if (auto* trial = output.control()) trial->stop();
    else if (scopeEpoch != 0) markFailed (ProductSessionFailure::captureResult);
}

void LocalBlindProductSession::validatePair (const ExactPairBinding* current) noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    if (! capturedPair.valid() || (current != nullptr && *current == capturedPair))
        return;
    failure = ProductSessionFailure::pairChanged;
    if (auto* trial = output.control()) trial->stop();
}

ProductSessionPhase LocalBlindProductSession::trialPhase (TrialPhase phase) noexcept
{
    switch (phase)
    {
        case TrialPhase::ready: return ProductSessionPhase::ready;
        case TrialPhase::armed: return ProductSessionPhase::armed;
        case TrialPhase::listening: return ProductSessionPhase::listening;
        case TrialPhase::revealed: return ProductSessionPhase::revealed;
        case TrialPhase::returnPending: return ProductSessionPhase::returnPending;
        case TrialPhase::returned: return ProductSessionPhase::returned;
    }
    return ProductSessionPhase::failed;
}

ProductSessionView LocalBlindProductSession::viewUnderLock() const noexcept
{
    ProductSessionView result;
    result.phase = basePhase;
    result.failure = failure;
    result.gainPolicy = gainPolicy;
    result.sampleRate = sampleRate;
    result.channels = channels;
    result.start = startSample;
    result.frames = frameCount;
    result.fixedPreGainDb = fixedPreGainDb;
    result.lowerPostGainDb = lowerPostGainDb;
    result.matchedAnalysisUnits = matchedAnalysisUnits;
    if (auto* trial = output.control())
    {
        result.trial = trial->view();
        result.phase = trialPhase (result.trial.phase);
    }
    return result;
}

ProductSessionView LocalBlindProductSession::view() const noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    return viewUnderLock();
}

bool LocalBlindProductSession::needsService() const noexcept
{
    const std::lock_guard<std::mutex> lock (controlLock);
    if (scopeEpoch != 0 || releasePending || basePhase == ProductSessionPhase::capturing
        || basePhase == ProductSessionPhase::preparing
        || (output.control() == nullptr && output.hasStorage()))
        return true;
    const auto* trial = output.control();
    return trial != nullptr && trial->normalReturnConfirmed();
}

void LocalBlindProductSession::service() noexcept
{
    std::uint64_t epochToRelease = 0;
    {
        const std::lock_guard<std::mutex> lock (controlLock);
        if (auto* trial = output.control(); trial != nullptr && trial->normalReturnConfirmed())
        {
            if (output.retireAfterNormalReceipt())
            {
                epochs.publish ({});
                basePhase = ProductSessionPhase::returned;
                releasePending = scopeEpoch != 0;
            }
        }
        output.collect();
        if (releasePending && scopeEpoch != 0 && releaseScope)
            epochToRelease = scopeEpoch;
    }

    bool released = false;
    try
    {
        released = epochToRelease != 0 && releaseScope (epochToRelease);
    }
    catch (...)
    {
        released = false;
    }
    if (released)
    {
        const std::lock_guard<std::mutex> lock (controlLock);
        if (releasePending && scopeEpoch == epochToRelease)
        {
            scopeEpoch = 0;
            releasePending = false;
        }
    }
}
}
