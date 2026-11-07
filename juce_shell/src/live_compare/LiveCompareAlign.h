#pragma once
#include "LiveCompareRing.h"

#include <cstdint>
#include <cmath>
#include <limits>

namespace hypha::live_compare
{
// INV-LC28. Content moves only the audition's PRE address. K remains the clocks' proof,
// including an arbitrary 64-bit difference between the instances' clock origins.
// A recalibrated origin changes K. Content delay is retained only within its alignment context.
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
        store (value, context, measuredRevision);
        sequence.fetch_add (1);
    }
    // The Audio Thread's display copy: the same snapshot, without the non-RT publication window.
    void publishDisplay (AlignValue value, AlignContext context) noexcept
    {
        sequence.fetch_add (1);
        store (value, context, 0);
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
    void store (AlignValue value, AlignContext context, std::uint64_t measuredRevision) noexcept
    {
        frames.store (value.frames); verifySamples.store (value.verifySamples); revision.store (measuredRevision);
        authority.store (context.authority); format.store (context.format);
        ownerA.store (context.ownerA); ownerB.store (context.ownerB);
    }
    std::atomic<std::uint64_t> sequence { 0 }, revision { 0 }, authority { 0 }, format { 0 }, ownerA { 0 }, ownerB { 0 };
    std::atomic<std::int64_t> frames { 0 };
    std::atomic<bool> verifySamples { false };
};

// A repeated pattern can be compared without claiming its absolute position. The same
// block that would sound must match POST, with one positive scale across both channels.
// A changed pattern fails before any fade/output; clocks, PCM bounds and ceiling still gate it.
// eitherPolarity is for PRE at its mapped address, where a polarity change has moved nothing.
inline bool matchingScaledBlock (float* const* post, float* const* pre, int channels, int frames,
                                 bool eitherPolarity = false) noexcept
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
    if (postEnergy <= 0.0 || preEnergy <= 0.0 || (eitherPolarity ? std::fabs (cross) : cross) <= 0.0) return false;
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

struct AlignUpdate
{
    bool fading = false; // a new address waits for PRE to fade out
    bool current = false; // the request answers the present clock mapping
    std::uint64_t revision = 0;
};

// Request publication belongs to non-RT; the applied value and its display belong to RT.
class RendererAlignment
{
public:
    void request (AlignValue value, AlignContext forContext, std::uint64_t measured = 0) noexcept
    { requested.publish (value, forContext, measured); readiness.confirm (measured); }
    AlignValue requestFor (AlignContext forContext) const noexcept { return requested.read (forContext, {}, readiness.generation()); }
    std::int64_t displayed (AlignContext forContext) const noexcept { return shown.read (forContext).frames; }
    // A new address may not jump under PRE: it waits for PRE to be silent, which LISTEN brings
    // about with the usual fades and a Blind trial leaves to the listener's own switching.
    AlignUpdate update (AlignPlayback playback, bool blind, bool preSelected, float& weight) noexcept
    {
        if (playback.context != context)
        {
            applied = {}; weight = 0.0f; context = playback.context;
            shown.publishDisplay ({}, context);
            readiness.newContext();
        }
        AlignUpdate result;
        result.revision = readiness.generation();
        const auto wanted = playback.enabled
            ? requested.read (context, applied, result.revision, &result.current,
                              appliedRevision == result.revision) : AlignValue {};
        if (wanted != applied && weight <= 0.0f
            && (! blind || ! preSelected || ! playback.enabled || applied.verifySamples)) adopt (wanted);
        result.fading = wanted != applied && ! blind;
        if (playback.verify && result.current && appliedRevision != result.revision && weight <= 0.0f)
            adopt (wanted); // only a freshly confirmed clock mapping can move BLIND here
        return result;
    }
    AlignValue value() const noexcept { return applied; }
    void publishApplied() noexcept { shown.publishDisplay (applied, context); }
    void confirmApplied (std::uint64_t revision) noexcept { appliedRevision = revision; }
    AlignReadiness readiness;
private:
    void adopt (AlignValue wanted) noexcept
    {
        if (wanted == applied) return;
        applied = wanted;
        shown.publishDisplay ({}, context); // nothing is claimed until this address is accepted
    }
    AlignRequest requested, shown;
    std::uint64_t appliedRevision = 0; // Audio Thread: contention never grants a new address
    AlignContext context; // Audio Thread only
    AlignValue applied; // Audio Thread only
};

// The last valid host identities survive gaps and repeated observations. This is only an
// invalidation witness, never a PCM or timing permission.
class AlignmentConditions
{
    struct Side
    {
        bool presentationKnown = false, chainKnown = false;
        std::uint8_t presentationSource = 0;
        std::uint32_t presentation = 0, delay = 0;
        std::uint64_t chainA = 0, chainB = 0;
        bool observe (const BlockClock& block) noexcept
        {
            bool changed = false;
            if (block.outputPresentationValid)
            {
                changed = changed || (presentationKnown && (presentationSource != block.presentationSource
                    || presentation != block.outputPresentationSamples));
                presentationKnown = true; presentationSource = block.presentationSource;
                presentation = block.outputPresentationSamples;
            }
            if ((block.runtimeChainA != 0 || block.runtimeChainB != 0) && !block.runtimeExpired)
            {
                changed = changed || (chainKnown && (chainA != block.runtimeChainA
                    || chainB != block.runtimeChainB || delay != block.maximumDelaySamples));
                chainKnown = true; chainA = block.runtimeChainA; chainB = block.runtimeChainB;
                delay = block.maximumDelaySamples;
            }
            return changed;
        }
    };
public:
    bool observe (std::uint64_t token, const Ring& ring, const BlockClock& post,
                  bool compensationChanged) noexcept
    {
        if (token != session) { *this = {}; session = token; }
        bool changed = compensationChanged;
        if (post.playing && post.clockValid) changed = postSide.observe (post) || changed;
        TimingSnapshot peer;
        if (readTiming (ring.header.timing, peer) && peer.active)
        {
            if (ownerKnown && (ownerA != peer.ownerA || ownerB != peer.ownerB)) changed = true;
            ownerKnown = true; ownerA = peer.ownerA; ownerB = peer.ownerB;
            changed = preSide.observe (peer.block) || changed;
        }
        return changed;
    }
private:
    std::uint64_t session = 0, ownerA = 0, ownerB = 0;
    bool ownerKnown = false;
    Side preSide, postSide;
};

#ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
inline void (*afterAlignFormatChangeForTest)() = nullptr;
#endif
}
