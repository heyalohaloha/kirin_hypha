#pragma once
#include "LiveCompareClock.h"
#include <cmath>
#include <cstdint>

namespace hypha::live_compare
{
// Musical coordinates are corroborating observations, NEVER PCM indices or an exact export
// range. Unknown/off are not distinguished by JUCE's bool; only positive, complete observations
// can authorise a wrap. Variable tempo, missing fields and range edits fail closed.
struct LoopContext
{
    bool active = false, valid = false;
    double ppq = 0.0, start = 0.0, end = 0.0, bpm = 0.0;

    bool usable (double rate) const noexcept
    {
        return active && valid && std::isfinite (rate) && rate > 0.0
            && std::isfinite (ppq) && std::isfinite (start) && std::isfinite (end)
            && std::isfinite (bpm) && bpm > 0.0 && end > start;
    }
    // A host may report its loop-playback setting with an empty range (no timeline selection):
    // nothing can wrap, so such a block is a linear one. A real wrap still breaks linearity.
    bool hasRange() const noexcept
    {
        return valid && std::isfinite (start) && std::isfinite (end) && std::isfinite (bpm)
            && bpm > 0.0 && end > start;
    }
    bool sameRange (const LoopContext& other) const noexcept
    { return active == other.active && valid == other.valid && identical (start, other.start)
          && identical (end, other.end) && identical (bpm, other.bpm); }
    static bool identical (double a, double b) noexcept { return a <= b && a >= b; }
    double beats (std::int64_t frames, double rate) const noexcept
    { return static_cast<double> (frames) * bpm / (60.0 * rate); }
    double advance (std::int64_t frames, double rate) const noexcept
    {
        const auto next = ppq + beats (frames, rate);
        // A delayed block may still precede a loop newly enabled further along the timeline.
        if (next < end - 1.0e-10) return next;
        const auto remainder = std::fmod (next - start, end - start);
        return remainder < 1.0e-10 || end - start - remainder < 1.0e-10 ? start : start + remainder;
    }
};

// How exactly a host's position report names an instant is a property of its clock profile.
// Most hosts derive PPQ from their sample position (within one sample). Logic Pro's render
// profile reports PPQ on a grid of 1/15360 quarter note (measured on Logic Pro 12.2: every
// reading lies on the grid, within one grid step of the exact position), while its project
// sample advances exactly and its Cycle moves project back by exactly one cycle length.
struct PositionPrecision
{
    double ppqGrid = 0.0;      // quarter notes per reported PPQ step; 0 = follows the sample position
    bool exactProject = false; // project, not PPQ, decides continuity inside a cycle
};
constexpr double logicPpqGrid = 1.0 / 15360.0;

inline PositionPrecision positionPrecision (std::uint8_t basis, std::uint8_t authority) noexcept
{
    return basis == static_cast<std::uint8_t> (ClockBasis::audioUnitRender)
            && authority == static_cast<std::uint8_t> (ClockAuthority::logicRender)
        ? PositionPrecision { logicPpqGrid, true } : PositionPrecision {};
}

inline bool onPpqGrid (double ppq, double grid) noexcept
{
    const double steps = ppq / grid;
    return grid > 0.0 && std::isfinite (steps) && std::abs (steps - std::round (steps)) <= 1.0e-6;
}

// Exact-project profile, both readings in the same constant-tempo cycle. The step is either
// linear (frames, or one sample at Logic's pre-roll crossing) or exactly one cycle length back,
// and PPQ must agree with that step. PPQ readings are never turned into a sample position.
inline bool exactProjectLoopStep (const LoopContext& from, std::int64_t project, std::int64_t frames,
                                  const LoopContext& to, std::int64_t nextProject, double rate,
                                  double grid) noexcept
{
    // The grid allowance needs both readings to show the grid; another reading keeps one sample.
    const double reading = onPpqGrid (from.ppq, grid) && onPpqGrid (to.ppq, grid) ? 2.0 * grid : 0.0;
    const double tolerance = from.beats (1, rate) + reading + 1.0e-9;
    const double length = (from.end - from.start) * (60.0 * rate / from.bpm);
    const double step = static_cast<double> (nextProject) - static_cast<double> (project);
    const double linear = from.ppq + from.beats (frames, rate);
    if (! std::isfinite (length) || ! std::isfinite (step) || ! std::isfinite (linear)) return false;
    if (linear < from.end + tolerance && std::abs (step - static_cast<double> (frames)) <= 1.0
        && std::abs (to.ppq - linear) <= tolerance) return true;
    // One wrap per callback; a cycle shorter than the callback fails closed.
    return linear >= from.end - tolerance && static_cast<double> (frames) < length
        && std::abs (step - (static_cast<double> (frames) - length)) <= 1.01
        && std::abs (to.ppq - (linear - (from.end - from.start))) <= tolerance;
}

inline bool loopPositionMatches (const LoopContext& from, std::int64_t project,
                                 std::int64_t frames, const LoopContext& to,
                                 std::int64_t nextProject, double rate,
                                 PositionPrecision precision = {}) noexcept
{
    if (! from.usable (rate) || ! to.usable (rate) || ! from.sameRange (to)) return false;
    if (precision.exactProject)
        return exactProjectLoopStep (from, project, frames, to, nextProject, rate, precision.ppqGrid);
    const double tolerance = from.beats (1, rate) + 1.0e-9;
    const double expected = from.advance (frames, rate);
    if (std::abs (to.ppq - expected) > tolerance) return false;
    // This checks the sample *movement* against the musical movement. It does not turn a PPQ
    // boundary into a sample address. The actual read always uses the independently proven K.
    const double movement = (to.ppq - from.ppq) * (60.0 * rate / from.bpm);
    return std::abs (static_cast<double> (nextProject - project) - movement) <= 1.01;
}

// Whether a captured callback begins with the host's own LOOP wrap: the previous callback and
// this one report the same usable loop, and the position moved back exactly as that loop plays
// (within Logic's PPQ grid when both readings show it). A seek, a stop and restart, or an edited
// loop is never a wrap. A Keep names its later pass from this fact (INV-R10). Plain values only.
class RecordLoopWrapWitness
{
public:
    bool observe (const LoopContext& loop, bool playing, bool hasPosition, std::int64_t position,
                  std::int64_t frames, double rate) noexcept
    {
        const bool valid = playing && hasPosition && frames > 0;
        const bool movedBack = position + 1 < previousPosition + previousFrames;
        const bool wrap = valid && previousValid && movedBack
            && (loopPositionMatches (previousLoop, previousPosition, previousFrames, loop, position, rate)
                || loopPositionMatches (previousLoop, previousPosition, previousFrames, loop, position,
                                        rate, PositionPrecision { logicPpqGrid, true }));
        previousValid = valid;
        previousLoop = loop;
        previousPosition = position;
        previousFrames = frames;
        return wrap;
    }

private:
    LoopContext previousLoop;
    std::int64_t previousPosition = 0, previousFrames = 0;
    bool previousValid = false;
};

// A project move with a continuous clock is a transport fact. A missing/changed clock,
// callback hole or foreign source is not silently reclassified as a seek.
enum class TimelineBreak : std::uint8_t
{
    none, stopped, projectMoved, loopEdited, callbackGap, clockMissing, projectMissing,
    clockMoved, clockProofChanged, sourceChanged, pcmRunChanged, unknown, compensationChanged, metadataPending
};
inline bool namedTransportRestart (TimelineBreak cause) noexcept
{ return cause == TimelineBreak::stopped || cause == TimelineBreak::projectMoved
      || cause == TimelineBreak::compensationChanged; }
struct TimelineStep
{
    bool broken = false, waiting = false;
    TimelineBreak cause = TimelineBreak::none;
};

class LoopTimeline
{
public:
    template <typename Block>
    TimelineStep observe (const Block& block, double rate, std::int64_t clampBudget = 0) noexcept
    {
        TimelineStep result;
        if (block.transportRestart)
        { havePrevious = false; observed = true; previousBreak = TimelineBreak::stopped; }
        const bool valid = block.playing && block.clockValid && block.projectValid && block.frames > 0;
        // The first valid callback after a stop/missing observation starts a fresh anchor,
        // even when the independent render clock kept advancing during the stopped callbacks.
        result.broken = block.afterGap || ! valid || (observed && ! havePrevious);
        const bool stoppedGap = block.afterGap && (! block.playing
            || (observed && ! havePrevious && previousBreak == TimelineBreak::stopped));
        result.cause = stoppedGap ? TimelineBreak::stopped : block.afterGap ? TimelineBreak::callbackGap
            : ! block.playing ? TimelineBreak::stopped
            : ! block.clockValid || block.frames <= 0 ? TimelineBreak::clockMissing
            : ! block.projectValid ? TimelineBreak::projectMissing
            : observed && ! havePrevious ? previousBreak : TimelineBreak::none;
        if (havePrevious && valid)
        {
            const bool logic = block.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::logicRender)
                && block.clockBasis == static_cast<std::uint8_t> (ClockBasis::audioUnitRender);
            // Logic's pre-roll crossing can move project by one sample. Render continuity
            // remains exact; project is only a transport sanity check in this profile.
            const bool linear = block.project == project + frames
                || (logic && std::abs (static_cast<double> (block.project) - project - frames) <= 1.0);
            result.broken = result.broken || block.clock != clock + frames;
            if (block.clock != clock + frames) result.cause = TimelineBreak::clockMoved;
            if (loop.active || block.loop.active)
            {
                if (loop.active && block.loop.active)
                {
                    // A profile change between the two readings grants no profile precision.
                    const auto precision = positionPrecision (block.clockBasis, block.clockAuthority);
                    const bool sameProfile = block.clockBasis == basis && block.clockAuthority == authority;
                    const bool matches = loopPositionMatches (loop, project, frames, block.loop,
                        block.project, rate, sameProfile ? precision : PositionPrecision {});
                    // Some hosts clamp POST to the loop start while delayed PRE still belongs
                    // to the tail. This is WAIT, never evidence for a new K or audible PRE.
                    const bool clamp = clampBudget > 0 && loop.usable (rate) && block.loop.usable (rate)
                        && loop.sameRange (block.loop)
                        && (waiting || (LoopContext::identical (block.loop.ppq, block.loop.start)
                            && loop.ppq + loop.beats (frames + clampBudget, rate) >= loop.end))
                        && (! waiting || block.clock - waitStart <= clampBudget + frames);
                    result.waiting = ! matches && clamp;
                    result.broken = result.broken || (! matches && ! clamp);
                    if (! matches && ! clamp && result.cause == TimelineBreak::none)
                        result.cause = ! loop.usable (rate) || ! block.loop.usable (rate)
                            ? TimelineBreak::unknown : ! loop.sameRange (block.loop)
                                ? TimelineBreak::loopEdited : TimelineBreak::projectMoved;
                }
                else
                {
                    result.broken = result.broken || ! linear
                        || (block.loop.active && ! block.loop.usable (rate));
                    if (block.loop.active && ! block.loop.usable (rate))
                        result.cause = TimelineBreak::unknown;
                }
            }
            else result.broken = result.broken || ! linear;
            if (result.broken && result.cause == TimelineBreak::none)
                result.cause = TimelineBreak::projectMoved;
        }
        if (result.waiting && ! waiting) waitStart = block.clock;
        waiting = result.waiting;
        clock = block.clock; project = block.project; frames = block.frames; loop = block.loop;
        basis = block.clockBasis; authority = block.clockAuthority;
        havePrevious = valid;
        observed = true;
        if (result.broken) previousBreak = result.cause;
        return result;
    }
    void reset() noexcept { *this = {}; }
private:
    bool havePrevious = false, waiting = false, observed = false;
    std::int64_t clock = 0, project = 0, waitStart = 0;
    std::int32_t frames = 0;
    std::uint8_t basis = 0, authority = 0;
    LoopContext loop;
    TimelineBreak previousBreak = TimelineBreak::unknown;
};
}
