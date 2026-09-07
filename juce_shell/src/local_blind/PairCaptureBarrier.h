#pragma once

#include "ExactRangeCapture.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace hypha::local_blind
{
// One exact POST-selected PRE. Names and host-specific context never identify a capture source.
struct ExactPairBinding
{
    std::uint64_t generation = 0;
    std::string projectHash;
    std::string preInstanceId;

    bool valid() const noexcept
    {
        return generation != 0 && ! projectHash.empty() && projectHash.size() < 64
            && ! preInstanceId.empty() && preInstanceId.size() < 64;
    }

    friend bool operator== (const ExactPairBinding& a, const ExactPairBinding& b) noexcept
    {
        return a.generation == b.generation && a.projectHash == b.projectHash
            && a.preInstanceId == b.preInstanceId;
    }
    friend bool operator!= (const ExactPairBinding& a, const ExactPairBinding& b) noexcept
    {
        return ! (a == b);
    }
};

struct ExactCaptureRequest
{
    std::string requestId;
    ExactPairBinding pair;
    std::uint64_t captureGeneration = 0;
    std::uint64_t clockGeneration = 0;
    std::uint32_t sampleRate = 0;
    int channels = 0;
    std::int64_t preStart = 0;
    std::int64_t postStart = 0;
    std::int64_t frames = 0;
    std::int64_t expiresAtUnixMs = 0;

    static bool canonicalRequestId (const std::string& value) noexcept
    {
        if (value.size() != 36)
            return false;
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            if (index == 8 || index == 13 || index == 18 || index == 23)
            {
                if (value[index] != '-') return false;
            }
            else if (! ((value[index] >= '0' && value[index] <= '9')
                        || (value[index] >= 'a' && value[index] <= 'f')))
                return false;
        }
        return true;
    }

    bool valid() const noexcept
    {
        return canonicalRequestId (requestId) && pair.valid() && captureGeneration != 0
            && clockGeneration != 0 && sampleRate >= 8'000 && sampleRate <= 768'000
            && (channels == 1 || channels == 2) && frames > 0 && expiresAtUnixMs > 0
            && frames <= static_cast<std::int64_t> (sampleRate) * 4
            && preStart <= std::numeric_limits<std::int64_t>::max() - frames
            && postStart <= std::numeric_limits<std::int64_t>::max() - frames;
    }
};

enum class CaptureSide : unsigned char { pre, post };
enum class PairCaptureState : unsigned char { pending, complete, invalid };
enum class PairCaptureFailure : unsigned char { none, stalePair, receipt };

// Non-RT completion metadata. PCM stays in the separately owned ExactRangeCapture until both
// sides pass this barrier and their producers have acknowledged retirement.
struct CaptureReceipt
{
    ExactPairBinding pair;
    std::uint64_t clockGeneration = 0;
    CaptureSide side = CaptureSide::pre;
    CaptureRange range;
    CaptureState state = CaptureState::pending;
    CaptureFailure failure = CaptureFailure::none;
};

// Single non-RT owner. It binds one capture and clock generation to one explicit pair and two
// native ranges. Any pair change, partial/error receipt, guessed format or mismatched range
// invalidates the whole request; a caller must issue a fresh request rather than retargeting it.
class PairCaptureBarrier final
{
public:
    explicit PairCaptureBarrier (const ExactCaptureRequest& request)
        : PairCaptureBarrier (request.pair, request.captureGeneration, request.clockGeneration,
                              request.sampleRate, request.channels, request.preStart,
                              request.postStart, request.frames)
    {
        if (! request.valid())
            throw std::invalid_argument ("Invalid exact capture request envelope");
    }

    PairCaptureBarrier (ExactPairBinding exactPair, std::uint64_t captureGeneration,
                        std::uint64_t clockGeneration, std::uint32_t sampleRate, int channels,
                        std::int64_t preStart, std::int64_t postStart, std::int64_t frames)
        : pair (std::move (exactPair)),
          clock (clockGeneration),
          pre ({ captureGeneration, sampleRate, channels, preStart, frames }),
          post ({ captureGeneration, sampleRate, channels, postStart, frames })
    {
        if (! pair.valid() || captureGeneration == 0 || clockGeneration == 0
            || sampleRate < 8'000 || sampleRate > 768'000
            || (channels != 1 && channels != 2) || frames < 1
            || overflows (preStart, frames) || overflows (postStart, frames))
            throw std::invalid_argument ("Invalid exact pair capture request");
    }

    const ExactPairBinding& binding() const noexcept { return pair; }
    std::uint64_t clockGeneration() const noexcept { return clock; }
    const CaptureRange& range (CaptureSide side) const noexcept
    {
        return side == CaptureSide::pre ? pre : post;
    }
    PairCaptureState state() const noexcept { return current; }
    PairCaptureFailure failure() const noexcept { return reason; }

    bool accept (const CaptureReceipt& receipt) noexcept
    {
        if (current == PairCaptureState::invalid) return false;
        if (! matches (receipt) || receipt.state != CaptureState::complete
            || receipt.failure != CaptureFailure::none)
        {
            invalidate (PairCaptureFailure::receipt);
            return false;
        }
        (receipt.side == CaptureSide::pre ? preComplete : postComplete) = true;
        if (preComplete && postComplete) current = PairCaptureState::complete;
        return true;
    }

    void invalidateIfPairChanged (const ExactPairBinding& currentPair) noexcept
    {
        if (current != PairCaptureState::invalid && currentPair != pair)
            invalidate (PairCaptureFailure::stalePair);
    }

private:
    const ExactPairBinding pair;
    const std::uint64_t clock;
    const CaptureRange pre;
    const CaptureRange post;
    bool preComplete = false;
    bool postComplete = false;
    PairCaptureState current = PairCaptureState::pending;
    PairCaptureFailure reason = PairCaptureFailure::none;

    static bool overflows (std::int64_t start, std::int64_t frames) noexcept
    {
        return start > std::numeric_limits<std::int64_t>::max() - frames;
    }
    static bool sameRange (const CaptureRange& a, const CaptureRange& b) noexcept
    {
        return a.generation == b.generation && a.sampleRate == b.sampleRate
            && a.channels == b.channels && a.start == b.start && a.frames == b.frames;
    }
    bool matches (const CaptureReceipt& receipt) const noexcept
    {
        return receipt.pair == pair && receipt.clockGeneration == clock
            && sameRange (receipt.range, range (receipt.side));
    }
    void invalidate (PairCaptureFailure value) noexcept
    {
        reason = value;
        current = PairCaptureState::invalid;
    }
};
}
