#pragma once
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

inline bool loopPositionMatches (const LoopContext& from, std::int64_t project,
                                 std::int64_t frames, const LoopContext& to,
                                 std::int64_t nextProject, double rate) noexcept
{
    if (! from.usable (rate) || ! to.usable (rate) || ! from.sameRange (to)) return false;
    const double tolerance = from.beats (1, rate) + 1.0e-9;
    const double expected = from.advance (frames, rate);
    if (std::abs (to.ppq - expected) > tolerance) return false;
    // This checks the sample *movement* against the musical movement. It does not turn a PPQ
    // boundary into a sample address. The actual read always uses the independently proven K.
    const double movement = (to.ppq - from.ppq) * (60.0 * rate / from.bpm);
    return std::abs (static_cast<double> (nextProject - project) - movement) <= 1.01;
}

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
            const bool linear = block.project == project + frames;
            result.broken = result.broken || block.clock != clock + frames;
            if (block.clock != clock + frames) result.cause = TimelineBreak::clockMoved;
            if (loop.active || block.loop.active)
            {
                if (loop.active && block.loop.active)
                {
                    const bool matches = loopPositionMatches (loop, project, frames,
                                                               block.loop, block.project, rate);
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
    LoopContext loop;
    TimelineBreak previousBreak = TimelineBreak::unknown;
};
}
