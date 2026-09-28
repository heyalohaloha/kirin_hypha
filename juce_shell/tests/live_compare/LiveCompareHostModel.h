#pragma once

#include "../../src/live_compare/LiveCompareClock.h"
#include "../../src/live_compare/LiveCompareCorrespondence.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

// Offline host model for the live compare correspondence rules. It drives the product Publisher and
// Consumer with the clock behaviour measured in Studio Pro 8.1.2 (VST3, AU) and Pro Tools 2026.4
// (AAX): callbacks are not split at loop ends; Studio Pro keeps POST's project time at the loop start
// for the chain latency after a wrap, Pro Tools reports POST's compensated content position; VST3
// continuous time follows the play position; AU render time and AAX plug-in frames advance only
// while the instance is called; a sleeping instance is not called at all.
namespace live_compare_test
{
using namespace hypha::live_compare;

enum class Format { vst3, audioUnit, aax };
enum class LoopReport { clampToLoopStart, followContent };

struct HostModel
{
    Format format = Format::vst3;
    LoopReport loopReport = LoopReport::clampToLoopStart;
    std::int64_t latency = 4096;
    bool mislabel = false; // hypothetical: stale audio after a relocation labelled inside the new run
};

struct Tally
{
    int evaluated = 0, accepted = 0, falseAccepted = 0, calibrating = 0, beforeRun = 0;
    int notWritten = 0, overwritten = 0, writingOrTorn = 0, gapsPre = 0, gapsPost = 0;
    int disagreementInvalidations = 0;
};

constexpr int blockFrames = 2048;
constexpr double sampleRate = 48000.0;
constexpr std::uint64_t periodNanos = 42666667;
constexpr std::uint64_t pairKey = 0x5A17C0DEull;

inline std::uint64_t mix (std::uint64_t z) noexcept
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Identification content: unique per project position and channel, never exactly zero.
inline float content (std::int64_t position, int channel) noexcept
{
    const auto bits = mix (static_cast<std::uint64_t> (position) * 2 + static_cast<std::uint64_t> (channel)) >> 40;
    return 0.01f + static_cast<float> (bits) / 16777216.0f * 0.5f;
}

class Host
{
public:
    explicit Host (HostModel m, CalibrationProfile calibration = {})
        : model (m), ring (std::make_unique<Ring>()), consumer (calibration),
          delayLine (2 * lineCapacity, 0.0f), timeline (lineCapacity, 0)
    {
        ring->initialise (pairKey, static_cast<std::uint32_t> (sampleRate));
        delay = reported = model.latency;
        for (auto& channel : scratch) channel.assign (blockFrames, 0.0f);
    }

    void loopOn (std::int64_t start, std::int64_t end) { loop = true; loopStart = start; loopEnd = end; }
    void loopOff() { loop = false; }
    void play (std::int64_t from) { locate (from); playing = true; }
    void seek (std::int64_t to) { locate (to); }
    void stop() { playing = false; position = lastLocate; }
    std::int64_t located() const { return lastLocate; }
    void sleepPre (int callbacks) { preAsleep = callbacks; }
    void sleepPost (int callbacks) { postAsleep = callbacks; }
    void run (int callbacks) { for (int i = 0; i < callbacks; ++i) step(); }
    const Tally& tally() const { return counts; }
    Ring& sharedRing() { return *ring; }

    // G1-04: the plug-in between PRE and POST changes its audio delay at the next block; the host
    // applies the new compensation to POST's clocks hostLagBlocks blocks later.
    void setLatency (std::int64_t newDelay, int hostLagBlocks)
    {
        delay = newDelay;
        pendingReported = newDelay;
        reportLag = hostLagBlocks;
        if (reportLag == 0) reported = newDelay;
    }

private:
    static constexpr std::int64_t lineCapacity = 1 << 16;

    void locate (std::int64_t to)
    {
        position = to;
        lastLocate = to;
        vst3Continuous = to;
        if (model.mislabel) mislabelLeft = model.latency;
    }

    std::int64_t vst3Clock() const { return playing ? vst3Continuous : position; }

    // Where the audio the POST side hears came from on the timeline (Pro Tools style reporting).
    std::int64_t contentPosition (std::int64_t delayed) const
    {
        const auto index = sampleIndex - delayed;
        return index < 0 ? 0 : timeline[static_cast<std::size_t> (index & (lineCapacity - 1))];
    }

    BlockClock side (std::int64_t project, std::int64_t hostContinuous, std::int64_t& counter,
                     GapDetector& gaps, int& gapCount)
    {
        BlockClock b;
        b.frames = blockFrames;
        b.playing = playing;
        b.projectValid = true;
        b.project = project;
        b.afterGap = gaps.observe (wall, blockFrames, sampleRate);
        if (b.afterGap) ++gapCount;
        if (model.format == Format::vst3)
        {
            b.clock = hostContinuous;
            b.clockValid = true;
        }
        else
        {
            b.clock = counter;
            b.clockValid = true;
            counter += blockFrames;
        }
        return b;
    }

    void step()
    {
        float in[2][blockFrames], out[2][blockFrames];
        auto next = position;
        for (int i = 0; i < blockFrames; ++i)
        {
            if (playing && loop && next == loopEnd) next = loopStart;
            in[0][i] = playing ? content (next, 0) : 0.0f;
            in[1][i] = playing ? content (next, 1) : 0.0f;
            timeline[static_cast<std::size_t> ((sampleIndex + i) & (lineCapacity - 1))] = playing ? next : position;
            if (playing) ++next;
        }

        if (preAsleep > 0)
            --preAsleep; // not called: the host passes the audio through unprocessed
        else
        {
            const auto b = side (position, vst3Clock(), preCounter, preGaps, counts.gapsPre);
            const float* channels[] = { in[0], in[1] };
            publisher.publish (*ring, b, channels, 2);
        }

        for (int i = 0; i < blockFrames; ++i) // the chain between PRE and POST: a delay line
            for (int c = 0; c < 2; ++c)
            {
                const auto t = sampleIndex + i;
                line (c, t) = in[c][i];
                out[c][i] = t - delay >= 0 ? line (c, t - delay) : 0.0f;
            }

        if (postAsleep > 0)
            --postAsleep;
        else
        {
            std::int64_t project = 0, hostContinuous = 0;
            if (model.loopReport == LoopReport::followContent)
                project = contentPosition (reported);
            else
                project = std::max (position - reported, loop ? loopStart : std::int64_t { 0 });
            hostContinuous = mislabelLeft > 0 ? vst3Clock() : vst3Clock() - reported;
            const auto b = side (project, hostContinuous, postCounter, postGaps, counts.gapsPost);
            float* dest[] = { scratch[0].data(), scratch[1].data() };
            const auto d = consumer.process (*ring, pairKey, static_cast<std::uint32_t> (sampleRate), b, dest, 2);
            count (d, out);
        }

        if (reportLag > 0 && --reportLag == 0) reported = pendingReported;
        if (playing) { position = next; vst3Continuous += blockFrames; }
        if (mislabelLeft > 0) mislabelLeft -= blockFrames;
        sampleIndex += blockFrames;
        wall += periodNanos;
    }

    float& line (int channel, std::int64_t t)
    {
        return delayLine[static_cast<std::size_t> (channel) * lineCapacity + static_cast<std::size_t> (t & (lineCapacity - 1))];
    }

    void count (const Decision& d, const float (&heard)[2][blockFrames])
    {
        if (d.invalidatedByDisagreement) ++counts.disagreementInvalidations;
        if (! playing || d.verdict == Verdict::stopped || d.verdict == Verdict::noClock) return;
        ++counts.evaluated;
        switch (d.verdict)
        {
            case Verdict::accepted:
            {
                ++counts.accepted;
                bool same = true;
                for (int c = 0; c < 2 && same; ++c)
                    for (int i = 0; i < blockFrames && same; ++i)
                        same = scratch[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] == heard[c][i];
                if (! same) ++counts.falseAccepted;
                break;
            }
            case Verdict::calibrating: ++counts.calibrating; break;
            case Verdict::beforeRun: ++counts.beforeRun; break;
            case Verdict::notWritten: ++counts.notWritten; break;
            case Verdict::overwritten: ++counts.overwritten; break;
            case Verdict::writing:
            case Verdict::torn: ++counts.writingOrTorn; break;
            default: break;
        }
    }

    HostModel model;
    std::unique_ptr<Ring> ring;
    Publisher publisher;
    Consumer consumer;
    GapDetector preGaps, postGaps;
    std::vector<float> delayLine;
    std::vector<std::int64_t> timeline;
    std::vector<float> scratch[2];
    Tally counts;
    bool playing = false, loop = false;
    std::int64_t loopStart = 0, loopEnd = 0, position = 0, lastLocate = 0, vst3Continuous = 0;
    std::int64_t preCounter = 777000, postCounter = 1147688, sampleIndex = 0, mislabelLeft = 0;
    std::int64_t delay = 0, reported = 0, pendingReported = 0;
    int reportLag = 0, preAsleep = 0, postAsleep = 0;
    std::uint64_t wall = 1000000000ull;
};
}
