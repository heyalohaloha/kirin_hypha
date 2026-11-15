#pragma once

// A LISTEN, BLIND or A/B press that waits says what it waits for, all the time it waits (2026-11-11):
// POST returning to its normal level, the A/B tail clearing from B, or the DAW to play again.
namespace hypha::live_compare_ui
{
enum class QueuedStart { listen, blind, candidate };

// A realtime bounce cannot be told from playback (only an offline render is reported), so it records what
// the comparison plays. The first LISTEN, BLIND or A/B that starts in a plug-in says so once; the buttons'
// help says it too.
inline constexpr const char* realtimeBounceNotice = "Realtime bounce records what you hear; END first";
enum class QueuedWait { returning, tail, stopped, other };
// The host's bypass holds POST's return past the press's 10 s: the press ends; the return finishes once
// bypass is off (INV-LC14).
inline constexpr const char* bypassHoldsReturnNotice = "Bypass is on: turn it off, then press again";
inline constexpr const char* offlineHoldsReturnNotice = "Rendering offline: press again after it finishes";

// What the press waits for now. Stopped comes first; the bounded return can also finish without playback.
constexpr QueuedWait queuedWait (bool playing, bool returning, bool tailClearing) noexcept
{
    return ! playing ? QueuedWait::stopped
         : returning ? QueuedWait::returning
         : tailClearing ? QueuedWait::tail : QueuedWait::other;
}

// V, C, B or Keep pressed while B plays out the A/B tail after END: it starts once the tail has cleared.
enum class AfterTail : unsigned char { none, referenceV, referenceC, referenceB, keepPair, keepAll };

// INV-AB6: a Keep that waited its two minutes while B never proved the ordinary path (playback kept
// stopping or seeking): it ends and says why.
inline constexpr const char* keepTailEndedNotice = "A/B tail not cleared: Keep not started";

constexpr const char* afterTailNotice (AfterTail action, bool playing) noexcept
{
    switch (action)
    {
        case AfterTail::referenceV: return playing ? "Clearing the A/B tail, then V" : "Waiting up to 10 s, then V";
        case AfterTail::referenceC: return playing ? "Clearing the A/B tail, then C" : "Waiting up to 10 s, then C";
        case AfterTail::referenceB: return playing ? "Clearing the A/B tail, then B" : "Waiting up to 10 s, then B";
        case AfterTail::keepPair:
        // Keep records only after ordinary audio has passed through B (INV-AB6): stopped, it waits for playback.
        case AfterTail::keepAll:    return playing ? "Clearing the A/B tail, then Keep" : "Play to continue, then Keep";
        case AfterTail::none:       break;
    }
    return "";
}

constexpr const char* queuedStartNotice (QueuedStart start, QueuedWait wait) noexcept
{
    switch (wait)
    {
        case QueuedWait::returning:
            return start == QueuedStart::blind ? "Returning POST to normal, then BLIND"
                 : start == QueuedStart::candidate ? "Returning POST to normal, then A/B"
                 : "Returning POST to normal, then LISTEN";
        case QueuedWait::tail:
            return start == QueuedStart::blind ? "Clearing the A/B tail, then BLIND"
                 : start == QueuedStart::candidate ? "Clearing the A/B tail, then A/B"
                 : "Clearing the A/B tail, then LISTEN";
        case QueuedWait::stopped:
            return start == QueuedStart::blind ? "Waiting up to 10 s, then BLIND"
                 : start == QueuedStart::candidate ? "Waiting up to 10 s, then A/B"
                 : "Waiting up to 10 s, then LISTEN";
        case QueuedWait::other:
            break;
    }
    return start == QueuedStart::blind ? "Waiting to start BLIND"
         : start == QueuedStart::candidate ? "Waiting to start A/B"
         : "Waiting to start LISTEN";
}
}
