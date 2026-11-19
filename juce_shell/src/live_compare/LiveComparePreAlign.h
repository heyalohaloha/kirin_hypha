#pragma once
#include "LiveCompareAlign.h"

namespace hypha::live_compare
{
// INV-LC28: the one owner of the PRE path's alignment state outside the Audio Thread (message
// thread, in memory only). It holds the correction kept for later sessions together with the clock
// difference K it was measured against, the pair / format / PRE owner and the host conditions it
// belongs to, and what is told. Every change goes through transition(), fed by explicit events; the
// Audio Thread's part (RendererAlignment) owns the single budget of waiting and publishes it. Nothing
// here depends on which source is selected or on the address a source applied, so a Blind trial's
// two anonymous sources share every decision. A settled estimate and the address PRE plays at are
// two facts: a Blind trial moves PRE's read position only while the listener is away from PRE, so a
// correction settled during the trial is told as not applied yet, for both sources alike, until
// each source has been heard (BlindHeardBoth below) or the Audio Thread holds PRE back for it.
struct PreAlignHost
{
    bool candidate = false; // A/B, which keeps a correction only for the same candidate and conditions
    std::uint64_t peer = 0, conditions = 0, compensation = 0;
    bool operator== (const PreAlignHost& other) const noexcept
    {
        return candidate == other.candidate
            && (! candidate || (peer == other.peer && conditions == other.conditions && compensation == other.compensation));
    }
};

enum class PreAlignEvent : std::uint8_t
{
    contextChanged, // a new pair, format, PRE owner or automatic-alignment choice: nothing is told yet
    clockChanged,   // the Audio Thread's clock mapping moved on (a new session, a changed K): nothing is told yet
    transport,      // a seek, stop, loop or mode end: `active` says whether a comparison still runs
    measured,       // two agreeing strong windows settled: nothing unmeasured is left to tell
    doubted,        // a strong window found the content away from the settled position: unmeasured until two agree
    estimate,       // a settled correction to publish, measured at `measuredRevision` against `measuredK`
    heardBoth,      // BLIND: each source was heard since, so PRE's read position has followed the request
    tick            // the periodic service: the budget and the kept correction
};

struct PreAlignInput
{
    PreAlignEvent event = PreAlignEvent::tick;
    AlignContext context;
    PreAlignHost host;
    std::uint64_t revision = 0; // the Audio Thread's present clock mapping ...
    std::int64_t k = 0;         // ... and its K
    const AlignReadiness* clock = nullptr; // the Audio Thread's watch for the kept amount
    bool active = false;
    bool blind = false; // a Blind trial runs: PRE's address follows a new request only away from PRE
    // estimate: what was requested before it
    AlignValue requested;
    AlignValue value;
    std::uint64_t measuredRevision = 0;
    std::int64_t measuredK = 0;
    // tick
    std::uint64_t releasedAt = 0; // tick: spent unanswered budget left for a later proven clock mapping
    bool pending = false, spent = false; // a content check is awaited; the Audio Thread's budget is spent
    std::int64_t reach = std::numeric_limits<std::int64_t>::max(); // what PRE's run serves; the maximum while unknown
    std::int64_t agreement = 0; // the estimates' agreement in frames
    std::uint64_t now = 0, readRetryNanos = 0;
};

struct PreAlignOutput
{
    bool request = false; // publish `value` for `revision`
    AlignValue value;
    std::uint64_t revision = 0;
    bool resend = false;
};

class PreAlignState
{
public:
    PreAlignOutput transition (const PreAlignInput& in) noexcept
    {
        PreAlignOutput out;
        // Acquire completion before consuming receipts; otherwise a just-arrived hit could be missed.
        const bool checked = in.clock == nullptr || in.clock->takeoverCheckedFor (in.revision);
        observeClock (in);
        if (! in.blind) unapplied.store (false, std::memory_order_release); // LISTEN and A/B fade PRE to it
        switch (in.event)
        {
            case PreAlignEvent::contextChanged: unapplied.store (false, std::memory_order_release); tell (false, false); break;
            case PreAlignEvent::heardBoth: unapplied.store (false, std::memory_order_release); break;
            case PreAlignEvent::clockChanged:
            case PreAlignEvent::measured: tell (false, false); break; // settled; whether it plays is `unapplied`
            case PreAlignEvent::transport:
                if (! in.active) { unapplied.store (false, std::memory_order_release); tell (false, false); }
                break;
            case PreAlignEvent::doubted: tell (! in.host.candidate, false); break; // A/B says its A is unconfirmed
            case PreAlignEvent::estimate:
                // An estimate measured against an older mapping proves nothing about this one. The
                // Audio Thread refuses the old revision anyway; the kept K is the measured one, never
                // whatever K has become meanwhile.
                if (in.measuredRevision != in.revision || in.measuredK != in.k) break;
                out = { true, in.value, in.measuredRevision };
                if (in.value.verifySamples) break; // a repeated waveform proves no unique position: never kept
                if (in.clock != nullptr)
                {
                    // The Audio Thread watches for this amount too before the mapping is read again. Had
                    // K moved on since the snapshot, this estimate proves nothing: it is not kept and not
                    // published, and what was kept before stays, watched all the while.
                    in.clock->watchNext (in.value.frames, in.agreement);
                    if (in.clock->generation() != in.revision) { in.clock->watchNext (0, in.agreement); out = {}; break; }
                }
                kept = { in.value, in.measuredK, in.context, in.host }; keptMeasured = in.measuredRevision;
                if (in.clock != nullptr) in.clock->watchKept (watched = kept.value.frames, in.agreement);
                break;
            case PreAlignEvent::tick:
            {
                if (checked && usable (in) && in.releasedAt != consumedRelease)
                {
                    consumedRelease = in.releasedAt;
                    if (usable (in) && keptMeasured < in.releasedAt)
                    {
                        kept = { {}, in.k, in.context, in.host }; missSince = 0;
                        out = { true, {}, in.revision }; tell (!in.host.candidate, false); break;
                    }
                }
                const bool known = in.reach != std::numeric_limits<std::int64_t>::max();
                if (! (in.pending && in.spent))
                {
                    missSince = 0;
                    // PRE's new run has written as far as the kept correction: it plays from here on.
                    if (awaited && usable (in) && known && in.reach >= kept.value.frames) tell (false, true);
                    break;
                }
                if (!checked)
                {
                    in.clock->provisionalClockFor (in.revision);
                    break; // K only for this unchecked revision; no confirmation or retained-value change
                }
                // The budget ended without a confirmed position: the kept correction of this very
                // pair, format, owner and host conditions plays, told as not re-measured; else K.
                auto use = usable (in) ? kept.value : AlignValue {};
                if (use.frames != 0 && ! known && (missSince == 0 || in.now - missSince < in.readRetryNanos))
                {
                    if (missSince == 0) missSince = in.now;
                    break; // read PRE's run again on the next service, for a bounded time
                }
                missSince = 0;
                if (use.frames != 0 && ! known) use = {}; // its position could not be read in time: dropped
                // A new run that has not written that far yet ends nothing: the correction stays kept and
                // requested. The Audio Thread plays PRE at K until the run serves it, told as unmeasured.
                const bool waits = use.frames != 0 && in.reach < use.frames;
                kept = { use, in.k, in.context, in.host }; // played under this K from here on
                out = { true, use, in.revision, use.frames != 0 };
                tell (waits || (use.frames == 0 && ! in.host.candidate), use.frames != 0 && ! waits);
                awaited = waits;
                break;
            }
        }
        // BLIND: while the one budget still holds PRE back for this check, both sources are POST and
        // the Audio Thread takes the new address at once. Any later change of the request waits for the
        // listener to leave PRE, which only the hidden assignment knows: told for both sources.
        if (in.event == PreAlignEvent::estimate && out.request && in.blind)
        {
            if (in.pending && ! in.spent) unapplied.store (false, std::memory_order_release);
            else if (out.value != in.requested) unapplied.store (true, std::memory_order_release);
        }
        watch (in);
        return out;
    }
    // A/B: the same candidate started again under the same pair and format keeps its correction.
    void candidateRestarted (std::uint64_t formatBefore, std::uint64_t authority, std::uint64_t peer) noexcept
    {
        if (kept.host.candidate && kept.context.format == formatBefore && kept.context.authority == authority
            && kept.host.peer == peer) kept.context.format = formatBefore + 1;
    }
    AlignNotice notice (bool unserved) const noexcept
    {
        auto told = alignNotice (unserved, unmeasured.load (std::memory_order_acquire), notRemeasured.load (std::memory_order_acquire));
        told.unapplied = ! told.unmeasured && unapplied.load (std::memory_order_acquire);
        return told;
    }
    bool awaitsListener() const noexcept { return unapplied.load (std::memory_order_acquire); }
    // For tests and diagnostics: the kept correction and the K it is bound to.
    AlignValue keptValue() const noexcept { return kept.value; }
    std::int64_t keptK() const noexcept { return kept.k; }
private:
    struct Kept { AlignValue value; std::int64_t k = 0; AlignContext context; PreAlignHost host; };
    bool usable (const PreAlignInput& in) const noexcept
    { return kept.value.frames != 0 && kept.context == in.context && kept.host == in.host; }
    // K moved by the kept amount in one step the Audio Thread saw, whatever K did after it before
    // this service: the host compensates that delay now, so the kept correction is dropped (PRE at
    // K). Nothing else is compared: a sum of other changes of K is no takeover.
    void observeClock (const PreAlignInput& in) noexcept
    {
        const auto taken = in.clock != nullptr ? in.clock->takenOver() : 0;
        if (taken != 0 && taken == kept.value.frames && usable (in)) kept.value = {};
    }
    // The Audio Thread watches for the amount that is kept now.
    void watch (const PreAlignInput& in) noexcept
    {
        if (in.clock == nullptr || kept.value.frames == watched) return;
        in.clock->watchKept (watched = kept.value.frames, in.agreement);
    }
    void tell (bool unmeasuredNow, bool notRemeasuredNow) noexcept
    {
        unmeasured.store (unmeasuredNow, std::memory_order_release);
        notRemeasured.store (notRemeasuredNow, std::memory_order_release);
        awaited = false;
    }
    Kept kept;
    std::uint64_t keptMeasured = 0, consumedRelease = 0;
    std::int64_t watched = 0; // the amount handed to the Audio Thread
    std::uint64_t missSince = 0; // the first read of the kept position that missed
    bool awaited = false; // the kept correction is requested and waits for PRE's new run to reach it
    std::atomic<bool> unmeasured { false }, notRemeasured { false };
    std::atomic<bool> unapplied { false }; // BLIND: a settled request PRE's address may not have followed yet
};

// BLIND: which source is PRE stays hidden, so a new address counts as applied once each of the two
// sources has been selected, without another press, through `needed` frames of playback: one of
// them is not PRE, and PRE's read position moved while it played (the fade is 5 ms).
struct BlindHeardBoth
{
    void restart() noexcept { heard[0] = heard[1] = 0; }
    bool observe (std::uint64_t first, std::uint64_t second, std::uint64_t needed) noexcept
    {
        heard[0] = first; heard[1] = second;
        return heard[0] >= needed && heard[1] >= needed;
    }
private:
    std::uint64_t heard[2] { 0, 0 };
};
}
