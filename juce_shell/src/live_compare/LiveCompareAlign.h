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

// INV-LC28: K moved by the kept amount, within the estimates' agreement: the host now compensates
// that delay itself, so the kept correction has nothing left to add (it would play PRE late). Any
// other change of K (a buffer size, a re-calibrated clock origin) leaves the kept correction valid.
inline bool hostTookOver (AlignValue kept, std::int64_t keptK, std::int64_t k, std::int64_t agreement) noexcept
{
    std::int64_t moved = 0, rest = 0;
    return kept.frames > 0 && checkedClockSubtract (k, keptK, moved) && checkedClockSubtract (moved, kept.frames, rest)
        && rest >= -agreement && rest <= agreement;
}

// INV-LC28: what is told once a wait ended without a position. unserved: a short correction's hold
// ended and PRE (A) plays at K until its run serves it. One rule for LISTEN, BLIND and A/B.
struct AlignNotice { bool unmeasured = false, notRemeasured = false; };
inline AlignNotice alignNotice (bool unserved, bool unmeasured, bool notRemeasured) noexcept
{ return { unserved || unmeasured, ! unserved && notRemeasured }; }

// The clocks alone prove PRE's path at this block: K is valid and only the PCM range at one address
// (writing, before the run, not written, overwritten, torn) may have refused it.
template <typename Decision> bool alignClockProven (const Decision& decision) noexcept
{
    using V = decltype (decision.verdict);
    return decision.kValid && (decision.verdict == V::accepted || (decision.verdict >= V::writing && decision.verdict <= V::torn));
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
            const bool stepped = haveK && k != previousK;
            if (! haveK || k != previousK)
                revision.fetch_add (1);
            // After the revision: the owner reads the revision again once it has handed an amount down.
            if (const auto amount = keptFrames.load(); stepped && hostTookOver ({ amount }, previousK, k, keptAgreement.load()))
                takenFrames.store (amount);
            previousK = k;
            haveK = true;
        }
    }
    std::int64_t clockK() const noexcept { return observedK.load(); }
    // INV-LC28, the owner of PRE's alignment state (non-RT): the kept amount to watch for from now on
    // (0: none). The Audio Thread compares each step of K between two proven mappings of one session
    // with it, there and then, so a service slower than the changes still learns that the host took
    // that delay over, whatever K did afterwards. Nothing is logged and nothing waits.
    void watchKept (std::int64_t frames, std::int64_t agreement) const noexcept
    { keptAgreement.store (agreement); keptFrames.store (frames); takenFrames.store (0); }
    // The watched amount K stepped by since the last call; 0 when it did not.
    std::int64_t takenOver() const noexcept { return takenFrames.exchange (0); }
    std::uint64_t generation() const noexcept { return revision.load(); }
    bool pending() const noexcept { return confirmed.load() != revision.load(); }
    void recheck() noexcept { if (! pending()) revision.fetch_add (1); }
    void confirm (std::uint64_t measured) noexcept { confirmed.store (measured); }
    void ambiguous (std::uint64_t measured) noexcept { ambiguousRevision.store (measured); }
    bool ambiguous() const noexcept { return pending() && revision.load() != 0 && ambiguousRevision.load() == revision.load(); }
private:
    std::atomic<std::uint64_t> revision { 0 }, confirmed { 0 }, ambiguousRevision { 0 };
    std::atomic<std::int64_t> observedK { 0 };
    mutable std::atomic<std::int64_t> keptFrames { 0 }, keptAgreement { 0 }, takenFrames { 0 };
    bool started = false, haveK = false, wasEnabled = true; // Audio Thread only
    std::int64_t previousK = 0;
};

struct AlignUpdate
{
    bool fading = false; // a new address waits for PRE to fade out
    bool current = false; // the request answers the present clock mapping
    bool deferred = false; // a short correction PRE's new run does not serve yet: POST until it does
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
    // readableFrames: the largest correction PRE's current run can serve at this block. A longer one
    // (after a seek, stop or new start) is kept but not used until PRE has written that far. One of up
    // to shortFrames (the 1 s bound) waits as POST. ONE budget bounds every wait for PRE's address:
    // a block of playback with PRE selected whose holdable correction PRE's run does not serve or
    // whose clock is not proven yet, and a clock-proven block that waits for the content check
    // (observed() below), count against the same shortFrames unless the block plays PRE (served()), however
    // often seeks, stops, loop wraps or a changed K restart the wait. Once spent it stays spent,
    // whatever the block size, until PRE plays at its address or a new session or context begins:
    // PRE plays at K meanwhile, as with alignment off, never POST for want of the correction, and the
    // owner (LiveComparePreAlign.h) answers a pending check at once. BLIND counts and serves by PRE's
    // path whichever source is selected, so both sources wait and are told alike. playedFrames: this
    // block's frames while playing.
    AlignUpdate update (AlignPlayback playback, bool blind, bool preSelected, float& weight,
                        std::int64_t readableFrames = std::numeric_limits<std::int64_t>::max(),
                        std::int64_t shortFrames = 0, std::int64_t playedFrames = 0) noexcept
    {
        if (playback.context != context)
        {
            applied = {}; weight = 0.0f; context = playback.context;
            shown.publishDisplay ({}, context);
            newSession();
        }
        reach.store (readableFrames, std::memory_order_relaxed); // for the service's kept correction
        AlignUpdate result;
        result.revision = readiness.generation();
        auto wanted = playback.enabled
            ? requested.read (context, applied, result.revision, &result.current,
                              appliedRevision == result.revision) : AlignValue {};
        // BLIND: the hold and what it tells follow the requested correction and PRE's path, never the
        // applied address or the selection, which differ between the two anonymous sources.
        if (blind) blindRequest = playback.enabled ? requested.read (context, blindRequest, result.revision) : AlignValue {};
        const auto path = blind ? blindRequest : wanted;
        const bool beyond = playback.enabled && wanted.frames > 0 && wanted.frames > readableFrames;
        const bool pathBeyond = playback.enabled && path.frames > 0 && path.frames > readableFrames;
        const bool holdable = playback.enabled && path.frames > 0 && path.frames <= shortFrames;
        blindPath = blind; played = playedFrames; bound = shortFrames; checks = playback.enabled && playback.verify;
        const bool unreached = pathBeyond || readableFrames == std::numeric_limits<std::int64_t>::max();
        // The playing block that calibrates: PRE's reach is not known yet, so nothing says whether a
        // correction's address can be read. Once the budget is spent it plays at K instead of trying.
        // In BLIND no source's own address may decide it: both wait (counted in observed()) until then.
        const bool unknown = playedFrames > 0 && readableFrames == std::numeric_limits<std::int64_t>::max();
        stake = holdable && unreached && (blind || (preSelected && playback.outputAllowed)) && ! expired ? playedFrames : 0;
        if (stake > 0 && held + stake > shortFrames) expired = true; // latched: see served()
        if (expired) stake = 0;
        held += stake; // counted unless this block plays PRE
        result.deferred = (((beyond || pathBeyond) && holdable) || (blind && unknown && path.frames > 0)) && ! expired;
        if (pathBeyond && holdable && expired) { holdEnded = true; unserved.store (true); } // told as unmeasured meanwhile
        const bool atK = unknown && (expired || (blind && path.frames == 0));
        if ((beyond && ! result.deferred) || atK) wanted = {};
        const bool unreadable = applied.frames > 0 && (applied.frames > readableFrames || (blind && atK)); // BLIND may leave it while PRE is silent
        if (wanted != applied && weight <= 0.0f
            && (! blind || ! preSelected || ! playback.enabled || applied.verifySamples || unreadable)) adopt (wanted);
        result.fading = wanted != applied && ! blind;
        if (playback.verify && result.current && appliedRevision != result.revision && weight <= 0.0f)
            adopt (wanted); // only a freshly confirmed clock mapping can move BLIND here
        // A spent budget the service has answered waits for no content check again: at a new clock
        // mapping PRE goes on at K at once (awaits() below) until PRE plays at its address.
        answered = expired && (answered || result.current);
        if (answered && checks && (readiness.pending() || ! result.current) && weight <= 0.0f) adopt ({});
        if (path.frames == 0) unserved.store (false);
        corrected = path.frames > 0;
        // BLIND: a correction is served only where PRE's run is known to reach it (not on the block that calibrates).
        serves = blind ? ! pathBeyond && (path.frames == 0 || readableFrames != std::numeric_limits<std::int64_t>::max())
                       : applied == path;
        return result;
    }
    // A new session or context: its first hold has the whole bound again.
    void newSession() noexcept
    {
        readiness.newContext(); held = stake = 0; expired = holdEnded = false; blindRequest = {}; unserved.store (false);
        spent.store (false); renew();
        reach.store (std::numeric_limits<std::int64_t>::max());
    }
    // The largest correction PRE's run served at POST's latest block; the maximum while unknown.
    std::int64_t servable() const noexcept { return reach.load (std::memory_order_relaxed); }
    // The hold ended before PRE's run served the correction: PRE plays at K until it does.
    bool unservedCorrection() const noexcept { return unserved.load(); }
    AlignValue value() const noexcept { return applied; }
    // The block's clock verdict. accepted: at the applied address; pathClock: the clocks alone prove
    // PRE's path (alignClockProven below), whatever address this assignment applied. BLIND takes
    // everything from the path, so both anonymous sources are proven, wait and spend alike. A proven
    // block that still waits for the content check spends the same budget as a held one, from the
    // moment the content service has seen this budget waiting (arm() below): time in which nothing
    // could measure is not a wait for the measurement. The service is awaited once for each budget,
    // not for each clock mapping: mappings that change faster than the service runs add up in the
    // one budget. A proven block whose correction's address was refused, and BLIND's block that
    // calibrates, spend it at once.
    void observed (bool accepted, bool pathClock, const AlignUpdate& update) noexcept
    {
        if (accepted) shown.publishDisplay (applied, context);
        const bool path = blindPath ? pathClock : accepted;
        proven.store (path && ! update.deferred, std::memory_order_relaxed);
        const bool waiting = checks && (readiness.pending() || ! update.current || update.revision != readiness.generation());
        const bool refused = ! blindPath && pathClock && ! accepted && applied.frames > 0;
        const bool seen = armed.load (std::memory_order_relaxed) == budget.load (std::memory_order_relaxed);
        if (((path && ((waiting && seen) || update.deferred)) || refused) && stake == 0 && ! expired)
        {
            if (held + played > bound) expired = true; else held += (stake = played);
        }
        else if (blindPath && path && ! waiting && ! update.deferred) served(); // by PRE's path, whatever this source applied
        spent.store (expired, std::memory_order_relaxed);
    }
    bool pathProven() const noexcept { return proven.load (std::memory_order_relaxed); }
    // After observed(): the block waits as POST for PRE's run or for the content check of the present
    // clock mapping. Once the budget is spent and the service has answered that (the kept correction
    // or K), only the first can wait: a later unchecked mapping plays PRE at K, which is not PRE at a
    // confirmed address, so the budget stays spent.
    bool awaits (const AlignUpdate& update) noexcept
    {
        if (! (readiness.pending() || ! update.current || update.revision != readiness.generation())) return update.deferred;
        serves = false;
        return update.deferred || ! (answered && applied == AlignValue {});
    }
    // The content service (non-RT) has seen the present budget waiting for a content check.
    void arm() const noexcept { armed.store (budget.load (std::memory_order_relaxed), std::memory_order_relaxed); }
    // The one budget is spent: a pending content check is answered at once (PRE at K or the kept correction).
    bool budgetSpent() const noexcept { return spent.load (std::memory_order_relaxed); }
    // This block plays PRE at the applied address (or could: in BLIND whichever source is selected,
    // elsewhere with POST selected): it used none of the budget. At its address the budget is whole
    // again; a hold that ended needs PRE's run to serve the correction first.
    void served() noexcept
    {
        held -= stake; stake = 0;
        if (serves && (corrected || ! holdEnded)) { held = 0; expired = holdEnded = false; unserved.store (false); renew(); }
    }
    void confirmApplied (std::uint64_t revision) noexcept { appliedRevision = revision; }
    AlignReadiness readiness;
private:
    void renew() noexcept { budget.store (budget.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed); } // whole again
    void adopt (AlignValue wanted) noexcept
    {
        if (wanted == applied) return;
        applied = wanted;
        shown.publishDisplay ({}, context); // nothing is claimed until this address is accepted
    }
    AlignRequest requested, shown;
    std::uint64_t appliedRevision = 0; // Audio Thread: contention never grants a new address
    AlignContext context; // Audio Thread only
    AlignValue applied, blindRequest; // Audio Thread only
    // Audio Thread only. held: frames of counted playback without PRE since PRE last played at its
    // address; stake: this block's share of it; expired: the budget is spent (latched); holdEnded: it
    // was spent while PRE's run did not serve a correction; serves: PRE playing this block is PRE at
    // its address (corrected: a correction); answered: the service has answered the spent budget.
    std::int64_t held = 0, stake = 0, played = 0, bound = 0;
    bool expired = false, answered = false, holdEnded = false, serves = false, corrected = false, blindPath = false, checks = false;
    std::atomic<bool> unserved { false }, proven { false }, spent { false };
    std::atomic<std::uint64_t> budget { 1 }; // written by the Audio Thread: which budget is running
    mutable std::atomic<std::uint64_t> armed { 0 }; // written by the content service only
    std::atomic<std::int64_t> reach { std::numeric_limits<std::int64_t>::max() };
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
    // The conditions a kept A/B correction was confirmed under (INV-LC28): equal or not, nothing more.
    std::uint64_t fingerprint() const noexcept
    {
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash] (std::uint64_t value) noexcept { hash = (hash ^ value) * 1099511628211ull; };
        for (const auto* side : { &preSide, &postSide })
        {
            mix (side->presentationKnown); mix (side->presentationSource); mix (side->presentation);
            mix (side->chainKnown); mix (side->chainA); mix (side->chainB); mix (side->delay);
        }
        return hash;
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
