#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace hypha::local_blind
{
// These epochs must come from the admission/pair/clock owners, not from UI preferences.
// Matching them proves consistency only; this renderer cannot establish DAW participant scope.
struct TrialEpochs
{
    std::uint64_t scope = 0, pair = 0, capture = 0, clock = 0;
    bool valid() const noexcept { return scope && pair && capture && clock; }
    bool operator== (const TrialEpochs& b) const noexcept
    { return scope == b.scope && pair == b.pair && capture == b.capture && clock == b.clock; }
};

struct TrialFormat
{
    TrialEpochs epochs;
    std::uint32_t sampleRate = 0;
    int channels = 0;
    std::int64_t start = 0, frames = 0; // POST project-native half-open playback range
    std::uint64_t minimumHeardFrames = 0; // explicit policy, never padded by fake playback
    bool exactLoopAllowed = false;
};

struct TrialBlock
{
    TrialEpochs epochs;
    std::uint32_t sampleRate = 0;
    std::int64_t position = 0;
    bool positionValid = false, playing = false, realtime = false, bypassed = false;
    bool exactLoopRangeValid = false;
    std::int64_t loopStart = 0, loopEnd = 0;
};

// Facts are prepared off RT. The factory verifies gain match/headroom before constructing a trial.
// Lower POST is an explicit alternative: never attenuate both sources or silently clamp the match.
struct TrialGain
{
    float fixedPre = 1.0f;
    float lowerPost = 1.0f;
    bool requiresLowerPostApproval = false;
};

enum class TrialPhase { ready, listening, revealed, returnPending, returned };
enum class TrialAnswer : unsigned char { none, one, two, noPreference, cannotDistinguish };
enum class TrialOutput { untouched, copy, heldAttenuation };
enum class TrialFailure : unsigned char { none, format, transport, epochs, discontinuity, range };

// Intentionally contains no source labels, hashes, gain, waveform, or participant identity.
struct TrialView
{
    TrialPhase phase = TrialPhase::ready;
    int activeStimulus = 0, pendingStimulus = 0, revealedOneSide = -1; // 0=POST, 1=PRE
    TrialAnswer answer = TrialAnswer::none;
    bool canAnswer = false, lowerPostApprovalRequired = false;
    TrialFailure failure = TrialFailure::none;
};

class LocalBlindTrial final
{
public:
    // Off RT only. Copies are already aligned once and immutable; same PCM is a valid control.
    // firstIsPre must be an OS-CSPRNG result in production, not inferred from source or gain.
    LocalBlindTrial (TrialFormat, TrialGain, std::vector<float> post, std::vector<float> pre,
                     bool firstIsPre, std::size_t byteBudget);
    LocalBlindTrial (const LocalBlindTrial&) = delete;
    LocalBlindTrial& operator= (const LocalBlindTrial&) = delete;

    // Single non-RT control owner; RT only consumes atomic commands and publishes receipts.
    bool start (bool approveLowerPost = false) noexcept;
    bool select (int stimulus) noexcept;
    bool answer (TrialAnswer) noexcept;
    bool reveal() noexcept;
    void stop() noexcept;
    void requestNormalReturn() noexcept;
    TrialView view() const noexcept;
    bool normalReturnConfirmed() const noexcept;
    std::size_t pcmBytes() const noexcept;

    // Single producer, bounded callback. No allocation, lock, I/O, modulo, or PCM destruction.
    TrialOutput render (float* const* output, int channels, int frames, const TrialBlock&) noexcept;

private:
    enum Command : std::uint64_t { ready = 0, one = 1, two = 2, stopRequested = 3, normalRequested = 4 };
    const TrialFormat format;
    const TrialGain gain;
    const std::vector<float> frozenPost, frozenPre;
    const bool oneIsPre;
    std::atomic<std::uint64_t> command { ready }, receipt { 0 }, returnReceipt { 0 };
    std::atomic<std::uint64_t> heardOne { 0 }, heardTwo { 0 };
    std::atomic<TrialFailure> failed { TrialFailure::none };
    std::atomic<bool> lowerApproved { false }, revealed { false };
    std::atomic<TrialAnswer> answered { TrialAnswer::none };
    std::int64_t previousEnd = 0; // RT-owned
    bool hasPrevious = false; // RT-owned, capture and playback epochs remain separate

    static Command kind (std::uint64_t value) noexcept { return static_cast<Command> (value & 7u); }
    bool issue (Command) noexcept;
    void invalidate (TrialFailure) noexcept;
    bool inputLayout (float* const*, int channels, int frames) const noexcept;
    TrialOutput hold (float* const*, int channels, int frames, const TrialBlock&) const noexcept;
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert (std::atomic<TrialFailure>::is_always_lock_free);
    static_assert (std::atomic<TrialAnswer>::is_always_lock_free);
    static_assert (std::atomic<bool>::is_always_lock_free);
};
}
