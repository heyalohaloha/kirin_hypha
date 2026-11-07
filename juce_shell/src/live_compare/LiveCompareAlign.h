#pragma once
#include "LiveCompareRing.h"

#include <cstdint>
#include <cmath>
#include <limits>

namespace hypha::live_compare
{
// INV-LC28. Content moves only the audition's PRE address. K remains the clocks' proof,
// including an arbitrary 64-bit difference between the instances' clock origins.
// A recalibrated origin changes K, not the content delay retained for this pair/format/owner.
struct AlignValue
{
    std::int64_t frames = 0;
    bool verifySamples = false; // a repeated waveform has no unique address: check each output block
    bool operator== (const AlignValue& other) const noexcept
    { return frames == other.frames && verifySamples == other.verifySamples; }
    bool operator!= (const AlignValue& other) const noexcept { return ! (*this == other); }
};

inline AlignValue alignValue (std::int64_t lagFrames) noexcept
{
    // PRE late is not available yet. Actual address/run/write bounds are checked by the reader.
    if (lagFrames >= -1 || lagFrames <= -static_cast<std::int64_t> (ringCapacityFrames)) return {};
    return { -lagFrames };
}

inline bool alignedReadStart (std::int64_t clock, std::int64_t k, AlignValue value, std::int64_t& start) noexcept
{ return checkedClockSubtract (clock, k, start) && checkedClockSubtract (start, value.frames, start); }

struct AlignContext
{
    std::uint64_t authority = 0, format = 0, ownerA = 0, ownerB = 0;
    bool operator== (const AlignContext& other) const noexcept
    { return authority == other.authority && format == other.format && ownerA == other.ownerA && ownerB == other.ownerB; }
    bool operator!= (const AlignContext& other) const noexcept { return ! (*this == other); }
};

inline AlignContext alignOwner (AlignContext context, const Ring& ring) noexcept
{
    context.ownerA = ring.header.timing.ownerA.load (std::memory_order_acquire);
    context.ownerB = ring.header.timing.ownerB.load (std::memory_order_acquire);
    return context;
}

struct AlignPlayback
{
    AlignContext context;
    bool enabled = true, outputAllowed = true, verify = false;
};

#ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
inline void (*duringAlignPublicationForTest) (std::int64_t) = nullptr;
#endif

// One non-RT writer publishes a complete value and the context captured BEFORE measuring.
// RT takes one bounded snapshot, retaining its current value only on publication contention.
class AlignRequest
{
public:
    void publish (AlignValue value, AlignContext context, std::uint64_t measuredRevision = 0) noexcept
    {
        sequence.fetch_add (1);
       #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
        if (duringAlignPublicationForTest != nullptr) duringAlignPublicationForTest (value.frames);
       #endif
        frames.store (value.frames); verifySamples.store (value.verifySamples); revision.store (measuredRevision);
        authority.store (context.authority); format.store (context.format);
        ownerA.store (context.ownerA); ownerB.store (context.ownerB);
        sequence.fetch_add (1);
    }
    AlignValue read (AlignContext context, AlignValue unchanged = AlignValue(),
                     std::uint64_t expectedRevision = 0, bool* current = nullptr,
                     bool retainedCurrent = false) const noexcept
    {
        if (current != nullptr) *current = retainedCurrent;
        const auto before = sequence.load();
        if ((before & 1u) != 0) return unchanged;
        const AlignValue value { frames.load(), verifySamples.load() };
        const auto measuredRevision = revision.load();
        const AlignContext measured { authority.load(), format.load(), ownerA.load(), ownerB.load() };
        if (before != sequence.load()) return unchanged;
        if (current != nullptr) *current = measured == context && measuredRevision == expectedRevision;
        if (measured == context && measuredRevision != expectedRevision) return unchanged;
        return measured == context ? value : AlignValue {};
    }
private:
    std::atomic<std::uint64_t> sequence { 0 }, revision { 0 }, authority { 0 }, format { 0 }, ownerA { 0 }, ownerB { 0 };
    std::atomic<std::int64_t> frames { 0 };
    std::atomic<bool> verifySamples { false };
};

// A repeated pattern can be compared without claiming its absolute position. The same
// block that would sound must match POST, with one positive scale across both channels.
// A changed pattern fails before any fade/output; clocks, PCM bounds and ceiling still gate it.
inline bool matchingScaledBlock (float* const* post, float* const* pre, int channels, int frames) noexcept
{
    double postEnergy = 0.0, preEnergy = 0.0, cross = 0.0;
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < frames; ++i)
        {
            const double a = post[ch][i], b = pre[ch][i];
            if (! std::isfinite (a) || ! std::isfinite (b)) return false;
            postEnergy += a * a; preEnergy += b * b; cross += a * b;
        }
    if (postEnergy <= 0.0 && preEnergy <= 0.0) return true;
    if (postEnergy <= 0.0 || preEnergy <= 0.0 || cross <= 0.0) return false;
    const auto scale = cross / preEnergy;
    double error = 0.0;
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < frames; ++i)
        {
            const auto delta = post[ch][i] - scale * pre[ch][i];
            error += delta * delta;
        }
    constexpr double precision = 8.0 * std::numeric_limits<float>::epsilon();
    return error <= postEnergy * precision * precision;
}

// A separate audition check, never a clock proof. Changed clock mapping requests fresh
// content evidence before the retained correction can sound; the value itself is kept.
class AlignReadiness
{
public:
    void newContext() noexcept { started = haveK = false; }
    void observe (bool enabled, bool playing, bool validK, std::int64_t k) noexcept
    {
        if (! playing) return;
        // Monitoring uses K even when the listener has disabled automatic correction.
        if (validK) observedK.store (k); // publish the mapping before exposing its new revision
        if (! started || enabled != wasEnabled)
        {
            revision.fetch_add (1);
            started = true;
        }
        wasEnabled = enabled;
        if (validK)
        {
            if (! haveK || k != previousK)
                revision.fetch_add (1);
            previousK = k;
            haveK = true;
        }
    }
    std::int64_t clockK() const noexcept { return observedK.load(); }
    std::uint64_t generation() const noexcept { return revision.load(); }
    bool pending() const noexcept { return confirmed.load() != revision.load(); }
    void recheck() noexcept { if (! pending()) revision.fetch_add (1); }
    void confirm (std::uint64_t measured) noexcept { confirmed.store (measured); }
    void late (std::uint64_t measured) noexcept { lateRevision.store (measured); }
    void ambiguous (std::uint64_t measured) noexcept { ambiguousRevision.store (measured); }
    bool ambiguous() const noexcept { return pending() && revision.load() != 0 && ambiguousRevision.load() == revision.load(); }
    bool late() const noexcept { return pending() && revision.load() != 0 && lateRevision.load() == revision.load(); }
private:
    std::atomic<std::uint64_t> revision { 0 }, confirmed { 0 }, lateRevision { 0 }, ambiguousRevision { 0 };
    std::atomic<std::int64_t> observedK { 0 };
    bool started = false, haveK = false, wasEnabled = true; // Audio Thread only
    std::int64_t previousK = 0;
};

#ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
inline void (*afterAlignFormatChangeForTest)() = nullptr;
#endif
}
