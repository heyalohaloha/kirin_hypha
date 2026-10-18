#pragma once
#include "LiveCompareLoop.h"

namespace hypha::live_compare
{
// Whether a captured callback begins with the host's own LOOP wrap: the previous callback and
// this one report the same usable loop, and the position moved back as that loop plays. A Keep
// names its later pass from this fact (INV-R10); it is a label, never a PCM or K proof.
//
// A plain wrap moves exactly one lap (within Logic's PPQ grid when both readings show it). A node
// with presentation delay on Studio One / Studio Pro shows the measured boundaries described in
// LiveCompareBlockClock.h: on VST3 its sample clamps to the loop start for the delayed tail while
// its PPQ still counts that tail before the start, then releases linearly; on AU its PPQ clamps to
// the start while its sample stays one lap before. Each keeps PPQ continuous and bounds the tail
// by the node's own reported presentation delay. A seek, a stop and restart, or an edited loop is
// never a wrap. Plain values only (Audio Thread).
class RecordLoopWrapWitness
{
public:
    bool observe (const LoopContext& loop, bool playing, bool hasPosition, std::int64_t position,
                  std::int64_t frames, double rate, std::int64_t presentationDelay) noexcept
    {
        const bool valid = playing && hasPosition && frames > 0;
        const bool wrap = valid && previousValid && position + 1 < previousPosition + previousFrames
            && previousLoop.usable (rate) && loop.usable (rate) && previousLoop.sameRange (loop)
            && (loopPositionMatches (previousLoop, previousPosition, previousFrames, loop, position, rate)
                || loopPositionMatches (previousLoop, previousPosition, previousFrames, loop, position,
                                        rate, PositionPrecision { logicPpqGrid, true })
                || delayedBoundary (loop, position, rate, presentationDelay));
        previousValid = valid;
        previousLoop = loop;
        previousPosition = position;
        previousFrames = frames;
        return wrap;
    }

private:
    bool delayedBoundary (const LoopContext& to, std::int64_t position, double rate,
                          std::int64_t delay) const noexcept
    {
        const auto& from = previousLoop;
        const double perBeat = 60.0 * rate / from.bpm;
        const double lap = (from.end - from.start) * perBeat;
        const double tolerance = from.beats (1, rate) + 1.0e-9;
        const double linearPpq = from.ppq + from.beats (previousFrames, rate);
        const double here = static_cast<double> (position);
        const double tail = (to.start - to.ppq) * perBeat;
        const bool boundedTail = delay > 0 && lap > static_cast<double> (delay)
            && tail <= static_cast<double> (delay) + 1.01;
        if (from.ppq >= from.start && to.ppq < to.start)
        {
            // VST3 clamp begins: the sample jumps to the loop start, PPQ counts the tail.
            const double loopStart = static_cast<double> (previousPosition)
                + (from.start - from.ppq) * perBeat;
            return boundedTail && std::abs (here - loopStart) <= 1.01
                && std::abs (to.ppq + (to.end - to.start) - from.advance (previousFrames, rate))
                       <= tolerance;
        }
        if (from.ppq < from.start)
        {
            // VST3 clamp holds while the tail plays, then releases linearly from the loop start.
            if (std::abs (to.ppq - linearPpq) > tolerance) return false;
            if (to.ppq < to.start) return boundedTail && position == previousPosition;
            return std::abs (here - (static_cast<double> (previousPosition)
                                     + (to.ppq - to.start) * perBeat)) <= 1.01;
        }
        // AU: PPQ clamps to the start while the sample stays one lap before the plain wrap.
        const double wrapped = static_cast<double> (previousPosition)
            + (from.advance (previousFrames, rate) - from.ppq) * perBeat;
        return LoopContext::identical (to.ppq, to.start) && linearPpq >= from.end - tolerance
            && std::abs (here - (wrapped - lap)) <= 1.01;
    }

    LoopContext previousLoop;
    std::int64_t previousPosition = 0, previousFrames = 0;
    bool previousValid = false;
};
}
