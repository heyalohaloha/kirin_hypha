#pragma once

// A LISTEN, BLIND or A/B press that waits says what it waits for, all the time it waits (2026-11-11):
// POST returning to its normal level, the A/B tail clearing from B, or the DAW to play again.
namespace hypha::live_compare_ui
{
enum class QueuedStart { listen, blind, candidate };
enum class QueuedWait { returning, tail, stopped, other };

// What the press waits for now. Stopped comes first: neither the return nor the tail moves without audio.
constexpr QueuedWait queuedWait (bool playing, bool returning, bool tailClearing) noexcept
{
    return ! playing ? QueuedWait::stopped
         : returning ? QueuedWait::returning
         : tailClearing ? QueuedWait::tail : QueuedWait::other;
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
