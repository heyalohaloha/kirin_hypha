#pragma once
#include "AaxAnalysisClock.h"

#include <cmath>
#include <cstdint>

namespace hypha::live_compare
{
// With an I/O insert in the chain, Pro Tools runs the inserts after it at the hardware buffer but
// still names one transport position per engine chunk. At a 256-frame buffer and a 1024-frame
// chunk POST sees the same position for four callbacks and then a step of 1024, while the DAE clock
// advances by exactly 256 in every callback. Measured on Pro Tools 2026.4.1 at 48 kHz.
//
// Those repeats are not seeks, but nothing but the host's own numbers may say so. A chunk is
// accepted only when, while the position stood still, the DAE clock advanced by the same block
// length in every callback, and the next position steps by exactly the frames of those callbacks.
// Until one has closed that way this class changes no bit of any block: the repeats go on to the
// ordinary rules, which refuse them as they always did, and a loop as short as a callback, which
// repeats its position for good, stays what those rules say it is. After a proof, anything else
// (another step, a moved clock, another block length, a stop) ends it, and the host's own numbers
// go back to the ordinary rules. The proof belongs to one engine: a new rate, clock certificate or
// prepared format starts again from a closed chunk. A chunk's one report names one PPQ too: inside
// a proven chunk a repeated position whose PPQ moved or is missing ends the proof and refuses that
// block.
//
// Named positions fill the callbacks the host did not report. They are not observations, so they
// never count towards K's calibration on either side (positionInterpolated), and a loop's sample
// length is never derived from PPQ. The callbacks past a first wrap stay unnamed, the host's next
// report measures the cycle (chunkCycleSamples) and later wraps use that measurement. Only those
// callbacks wait: the measured cycle lets K be proven again at once, without another lap. The
// measurement belongs to one loop: leaving it, or a new range or tempo, discards it. A wrap keeps a
// proven chunk but never proves one, and only a reported PPQ that crossed the loop end is a wrap.
//
// Certified Pro Tools AddClock only; every other clock, and every linear block, passes through
// unchanged. Audio Thread, one per side, fixed size. Nothing survives a break.
class AaxChunkPosition
{
public:
    void apply (BlockClock& block, double rate) noexcept
    {
        if (state != State::idle && (! sameClockProof (proof, block) || ! LoopContext::identical (rate, proofRate)))
            reset();
        if (! certifiedAaxClock (block) || block.transportRestart) { reset(); return; }
        const bool continuing = state != State::idle && block.clock == nextClock && block.frames == frames;
        const bool sameLoop = block.loop.active == proof.loop.active
            && (! block.loop.active || block.loop.sameRange (proof.loop));
        const bool repeats = continuing && sameLoop && block.project == head;
        const bool samePpq = ! proof.loop.active || ppqRepeats (block, rate);
        if (repeats && state == State::proven && ! samePpq) { refuse (block); return; }
        if (repeats && samePpq && (state != State::proven || elapsed < chunk))
        {
            if (state != State::proven) state = State::open;
            else if (! name (block, rate)) { refuse (block); return; }
            if (! checkedClockAdd (elapsed, frames, elapsed)) { refuse (block); return; }
        }
        else
        {
            const bool closed = continuing && closes (block, rate);
            if (closed) { chunk = elapsed; state = State::proven; }
            else state = State::anchored;
            if (! closed || ! block.loop.active || ! block.loop.sameRange (proof.loop)) cycle = 0;
            head = block.project; proof = block; proofRate = rate; frames = block.frames; elapsed = frames;
        }
        if (! checkedClockAdd (block.clock, block.frames, nextClock)) { refuse (block); return; }
        block.chunkCycleSamples = state == State::proven ? cycle : 0;
    }

    void reset() noexcept { *this = {}; }
    bool proven() const noexcept { return state == State::proven; }

private:
    enum class State : std::uint8_t { idle, anchored, open, proven };

    // Only a proof may withhold a block. Without one the block passes as the host reported it.
    void refuse (BlockClock& block) noexcept
    {
        if (state == State::proven) block.projectValid = false;
        reset();
    }

    // The same report, within the one sample any host may round. A missing value compares false.
    bool ppqRepeats (const BlockClock& block, double rate) const noexcept
    { return std::abs (block.loop.ppq - proof.loop.ppq) <= proof.loop.beats (1, rate) + 1.0e-9; }

    // The report after a chunk. Only an exact step proves one. A proven chunk also survives the
    // host's own wrap, the one native measurement of the cycle: samples decide the length, and the
    // reported PPQ must have crossed the loop end. PPQ's rounding allowance never passes a step.
    bool closes (const BlockClock& block, double rate) noexcept
    {
        std::int64_t step = 0, measured = 0;
        if (state == State::anchored || (state == State::proven && elapsed != chunk)
            || ! checkedClockSubtract (block.project, head, step)
            || ! checkedClockSubtract (elapsed, step, measured)) return false;
        const bool looping = proof.loop.active && block.loop.active;
        if (looping && ! loopPositionMatches (proof.loop, head, elapsed, block.loop, block.project, rate)) return false;
        if (measured == 0) return true;
        const bool wrapped = state == State::proven && looping && block.loop.ppq < proof.loop.ppq
            && proof.loop.advance (elapsed, rate) < proof.loop.ppq;
        if (! wrapped || (cycle != 0 && measured != cycle)) return false;
        cycle = measured;
        return true;
    }

    // False only when the arithmetic ends the proof. Past an unmeasured wrap the block stays
    // unnamed and the proof waits for the host's next report.
    bool name (BlockClock& block, double rate) noexcept
    {
        std::int64_t position = 0;
        if (! checkedClockAdd (head, elapsed, position)) return false;
        auto ppq = block.loop.ppq;
        if (proof.loop.usable (rate))
        {
            ppq = proof.loop.advance (elapsed, rate);
            if (ppq < proof.loop.ppq) // this callback lies past the wrap
            {
                if (cycle <= chunk) { block.projectValid = false; return true; } // one wrap per chunk, measured
                if (! checkedClockSubtract (position, cycle, position)) return false;
            }
        }
        block.project = position;
        block.loop.ppq = ppq;
        block.positionInterpolated = true;
        return true;
    }

    State state = State::idle;
    std::int64_t head = 0, nextClock = 0, elapsed = 0, chunk = 0, cycle = 0;
    std::int32_t frames = 0;
    double proofRate = 0.0;
    BlockClock proof; // the chunk's last host report: its position, loop and clock certificate
};
}
