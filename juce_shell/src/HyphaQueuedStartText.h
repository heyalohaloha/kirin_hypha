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

// What the press waits for now. Stopped comes first: neither the return nor the tail moves without audio.
constexpr QueuedWait queuedWait (bool playing, bool returning, bool tailClearing) noexcept
{
    return ! playing ? QueuedWait::stopped
         : returning ? QueuedWait::returning
         : tailClearing ? QueuedWait::tail : QueuedWait::other;
}

// V, C, B or Keep pressed while B plays out the A/B tail after END: it starts once the tail has cleared.
enum class AfterTail : unsigned char { none, referenceV, referenceC, referenceB, keepPair, keepAll };

constexpr const char* afterTailNotice (AfterTail action, bool playing) noexcept
{
    switch (action)
    {
        case AfterTail::referenceV: return playing ? "Clearing the A/B tail, then V" : "Play to continue, then V";
        case AfterTail::referenceC: return playing ? "Clearing the A/B tail, then C" : "Play to continue, then C";
        case AfterTail::referenceB: return playing ? "Clearing the A/B tail, then B" : "Play to continue, then B";
        case AfterTail::keepPair:
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
            return start == QueuedStart::blind ? "Play to continue, then BLIND"
                 : start == QueuedStart::candidate ? "Play to continue, then A/B"
                 : "Play to continue, then LISTEN";
        case QueuedWait::other:
            break;
    }
    return start == QueuedStart::blind ? "Waiting to start BLIND"
         : start == QueuedStart::candidate ? "Waiting to start A/B"
         : "Waiting to start LISTEN";
}
}
