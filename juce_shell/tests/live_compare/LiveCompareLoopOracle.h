#pragma once
#include "../../src/live_compare/LiveCompareCorrespondence.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

// Feasibility fixture, NOT a host certification or a proposed shipping policy. The delay line
// owns the expected audio independently of Consumer's K. A different token is emitted on every
// lap; unlike repeated project-position noise, it detects a whole-lap correspondence error.
namespace loop_feasibility
{
using namespace hypha::live_compare;
constexpr std::uint64_t key = 0x4c4f4f50;
constexpr int rate = 48000, maximumBlock = 2048;
enum class Clock { vst3, renderCounter };
enum class Position { content, clamp };

inline std::int64_t folded (std::int64_t sample, std::int64_t length)
{
    const auto remainder = sample % length;
    return remainder < 0 ? remainder + length : remainder;
}

inline float token (std::int64_t emitted, int channel)
{
    // The stereo pair uniquely encodes up to 2^32 emitted frames. Every value is exactly
    // representable and <= 0.5, including the silent-looking but nonzero high-order channel.
    const auto value = static_cast<std::uint64_t> (emitted);
    const auto part = (value >> (channel == 0 ? 0 : 16)) & 0xffffu;
    return static_cast<float> (part + 1) / 131072.0f;
}

struct Observation
{
    BlockClock pre, post;
    // Include information not yet forwarded by the product, to test whether simply adding
    // optional PPQ/endpoints would disambiguate the initial-loop case. Presentation is absent.
    std::int64_t loopLength = 0;
    double ppq = 0.0, loopStartPpq = 0.0, loopEndPpq = 0.0;
    bool looping = true, presentationValid = false;
};

inline bool sameClock (const BlockClock& a, const BlockClock& b)
{
    return a.clock == b.clock && a.project == b.project && a.frames == b.frames
        && a.clockValid == b.clockValid && a.projectValid == b.projectValid
        && a.playing == b.playing && a.afterGap == b.afterGap;
}

inline bool sameObservation (const Observation& a, const Observation& b)
{
    return sameClock (a.pre, b.pre) && sameClock (a.post, b.post)
        && a.loopLength == b.loopLength && a.ppq == b.ppq
        && a.loopStartPpq == b.loopStartPpq && a.loopEndPpq == b.loopEndPpq
        && a.looping == b.looping && a.presentationValid == b.presentationValid;
}

struct Result
{
    std::uint64_t blocks = 0, accepted = 0, wrong = 0, acceptedFrames = 0, totalFrames = 0;
    std::int64_t firstWrongClock = -1, firstWrongOffset = 0;
};

class Oracle
{
public:
    Oracle (std::int64_t length, std::int64_t delaySamples, Clock clockKind,
            Position report, bool repeated = false, bool loopEnabled = true)
        : loopLength (checkedRange (length, 1, ringCapacityFrames)),
          delay (checkedRange (delaySamples, 0, 2 * ringCapacityFrames)),
          clock (clockKind), position (report), repeatProject (repeated), looping (loopEnabled),
          ring (std::make_unique<Ring>()),
          delayLine (static_cast<std::size_t> (delay + maximumBlock + 1) * 2, 0.0f)
    {
        ring->initialise (key, rate);
        // Start the comparison in an already-running loop. Populate the physical delay with
        // the earlier emitted audio, but do not pre-populate the product PRE ring or its K.
        emitted = 2 * length + length / 3;
        started = emitted;
        for (auto t = emitted - delay; t < emitted; ++t)
            for (int c = 0; c < 2; ++c) line (t, c) = source (t, c);
    }

    Observation observations (int frames) const
    {
        checkedRange (frames, 1, maximumBlock);
        Observation o;
        o.loopLength = looping ? loopLength : 0;
        o.looping = looping;
        o.pre.frames = o.post.frames = frames;
        o.pre.clockValid = o.post.clockValid = true;
        o.pre.projectValid = o.post.projectValid = true;
        o.pre.playing = o.post.playing = true;
        o.pre.project = projectAt (emitted);
        o.post.project = ! looping ? emitted - delay
            : position == Position::content || (enabledDuringRun && emitted < loopStart + loopLength)
                ? projectAt (emitted - delay)
            : std::max (loopStart, o.pre.project - delay);
        // Two explicit synthetic clock assumptions, not universal format guarantees. The
        // counter origins do not encode latency. The VST3-like clock includes compensation.
        o.pre.clock = emitted + 777000;
        o.post.clock = clock == Clock::vst3 ? o.pre.clock - delay : emitted + 1147688;
        o.ppq = static_cast<double> (o.post.project) / 24000.0; // constant 120 BPM fixture only
        o.loopStartPpq = looping ? static_cast<double> (loopStart) / 24000.0 : 0.0;
        o.loopEndPpq = looping ? static_cast<double> (loopStart + loopLength) / 24000.0 : 0.0;
        o.pre.loop = { looping, looping, static_cast<double> (o.pre.project) / 24000.0,
                       o.loopStartPpq, o.loopEndPpq, 120.0 };
        o.post.loop = { looping, looping, o.ppq, o.loopStartPpq, o.loopEndPpq, 120.0 };
        return o;
    }

    // Enable a range beginning at the current position. No seek or clock reset occurs at
    // activation; the first actual wrap is one full loop later. Earlier delayed content keeps
    // its original non-loop project coordinate. This tests carrying an already calibrated K.
    void enableLoopAtCurrentPosition()
    {
        if (looping) throw std::invalid_argument ("loop already enabled");
        looping = true;
        loopStart = emitted;
        enabledDuringRun = true;
    }

    Decision step (int frames, void (*alterObservation) (Observation&) = nullptr)
    {
        auto o = observations (frames);
        if (alterObservation != nullptr) alterObservation (o);
        lastStart = emitted;
        lastFrames = frames;
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < 2; ++c)
            {
                input[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] = source (emitted + i, c);
                line (emitted + i, c) = input[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)];
                expected[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] = line (emitted + i - delay, c);
            }
        const float* in[] { input[0].data(), input[1].data() };
        float* out[] { actual[0].data(), actual[1].data() };
        publisher.publish (*ring, o.pre, in, 2);
        const auto d = consumer.process (*ring, key, rate, o.post, out, 2);
        ++result.blocks;
        result.totalFrames += static_cast<unsigned> (frames);
        if (d.verdict == Verdict::accepted)
        {
            ++result.accepted;
            result.acceptedFrames += static_cast<unsigned> (frames);
            const float* received[] { actual[0].data(), actual[1].data() };
            if (! matchesExpected (received))
            {
                if (result.wrong == 0)
                {
                    result.firstWrongClock = emitted;
                    result.firstWrongOffset = d.preStart - (emitted + 777000 - delay);
                }
                ++result.wrong;
            }
        }
        emitted += frames;
        return d;
    }

    bool expectedDiffers (const Oracle& other, int frames) const
    {
        checkedRange (frames, 1, std::min (lastFrames, other.lastFrames));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < frames; ++i)
                if (expected[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)]
                    != other.expected[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)]) return true;
        return false;
    }

    // Fixture self-test boundary. The expected buffer is from the physical delay line; the
    // test supplies independently calculated source tokens (or mutations), never Consumer K.
    bool matchesExpected (const float* const* received) const
    {
        if (lastFrames <= 0 || received == nullptr || received[0] == nullptr || received[1] == nullptr)
            return false;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < lastFrames; ++i)
                if (received[c][i] != expected[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)]) return false;
        return true;
    }
    std::int64_t expectedStart() const { return lastStart - delay; }
    const Result& tally() const { return result; }
    std::int64_t elapsed() const { return emitted - started; }

private:
    std::int64_t projectAt (std::int64_t t) const
    {
        if (! looping || (enabledDuringRun && t < loopStart)) return t;
        return loopStart + folded (t - loopStart, loopLength);
    }
    static std::int64_t checkedRange (std::int64_t value, std::int64_t minimum, std::int64_t maximum)
    {
        if (value < minimum || value > maximum) throw std::invalid_argument ("loop fixture range");
        return value;
    }
    float source (std::int64_t t, int c) const { return token (repeatProject ? folded (t, loopLength) : t, c); }
    float& line (std::int64_t t, int c)
    {
        const auto capacity = static_cast<std::int64_t> (delayLine.size() / 2);
        return delayLine[static_cast<std::size_t> (folded (t, capacity)) * 2 + static_cast<std::size_t> (c)];
    }
    std::int64_t loopLength, delay, emitted = 0, started = 0, lastStart = 0, loopStart = 0;
    int lastFrames = 0;
    Clock clock;
    Position position;
    bool repeatProject, looping;
    bool enabledDuringRun = false;
    std::unique_ptr<Ring> ring;
    Publisher publisher;
    Consumer consumer;
    std::vector<float> delayLine;
    std::array<std::array<float, maximumBlock>, 2> input {}, expected {}, actual {};
    Result result;
};
}
