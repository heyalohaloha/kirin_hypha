#pragma once
#include "AaxAnalysisProductFixture.h"
#include <string_view>

// LISTEN and BLIND through the real processors with an I/O insert between PRE and POST, in the
// shape measured on Pro Tools 2026.4.1: PRE in 1024-frame blocks one chunk ahead on the DAE clock,
// POST at the hardware buffer with one position per 1024 frames. This is a synthetic host. The
// audio reaches POST 512 frames after PRE whatever the positions say, H/W Insert Delay is what
// moves the positions, and the loop follows the host's reported shape without an I/O insert.
//
// Every frame of every POST callback is audited, not just a final state: PRE settled must be the
// PRE at clock minus the current K at the approved gain, POST must be POST bit for bit, and each
// 5 ms crossfade, including END's, must be those two at the renderer's own weights. Audio is at two
// levels so that the two sources never coincide.
namespace aax_analysis_product
{
class ChunkedPost
{
public:
    explicit ChunkedPost (int channelsIn, int bufferFramesIn = 256) : channels (channelsIn), bufferFrames (bufferFramesIn)
    {
        for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AAX);
            auto processor = std::make_unique<Processor> (role);
            auto layout = processor->getBusesLayout();
            const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
            require (processor->setBusesLayout (layout), "mono/stereo negotiates");
            processor->setPlayHead (role == Processor::Role::Pre ? &preClock : &postClock);
            processor->kirinHostInstanceGroup (role == Processor::Role::Pre ? 1u : 2u, true);
            hypha::tests::prepareFixtureProcessor (*processor, 48000, chunkFrames);
            LiveTimingFixtureAccess::configureAaxClock (*processor);
            LiveTimingFixtureAccess::enableFixtureWrites (*processor);
            (role == Processor::Role::Pre ? pre : post) = std::move (processor);
        }
        require (LiveTimingFixtureAccess::timingRingPublished (*pre), "fixture requires the native PRE timing mapping");
    }
    ~ChunkedPost() { post->releaseResources(); pre->releaseResources(); }

    void chunk (bool service = true)
    {
        // PRE first, one chunk ahead of POST's clock.
        const auto ahead = wrapped (transport + chunkFrames);
        preClock.engine = engine + chunkFrames;
        preClock.project = ahead;
        juce::AudioBuffer<float> preAudio (channels, chunkFrames);
        for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < chunkFrames; ++i) preAudio.setSample (ch, i, source (preClock.engine + i, ch));
        juce::MidiBuffer midi;
        pre->processBlock (preAudio, midi);
        for (int offset = 0; offset < chunkFrames; offset += bufferFrames)
        {
            postClock.engine = engine + offset;
            postClock.project = compensated(); // one position for the whole chunk
            juce::AudioBuffer<float> audio (channels, bufferFrames);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < bufferFrames; ++i) audio.setSample (ch, i, input (postClock.engine + i, ch));
            post->processBlock (audio, midi);
            if (auditOutput) audit (audio, postClock.engine);
        }
        engine += chunkFrames;
        transport = ahead;
        wallClock.advanceBlock (chunkFrames, 48000);
        if (service) LiveTimingFixtureAccess::serviceProcessorTimer (*post); // the content monitor runs here
    }

    void replacePre()
    {
        juce::MemoryBlock state;
        pre->getStateInformation (state);
        pre->releaseResources(); pre.reset();
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AAX);
        pre = std::make_unique<Processor> (Processor::Role::Pre);
        auto layout = pre->getBusesLayout();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
        require (pre->setBusesLayout (layout), "replacement PRE negotiates its format");
        pre->setPlayHead (&preClock); pre->kirinHostInstanceGroup (1u, true);
        hypha::tests::prepareFixtureProcessor (*pre, 48000, chunkFrames);
        pre->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        LiveTimingFixtureAccess::configureAaxClock (*pre);
        LiveTimingFixtureAccess::enableFixtureWrites (*pre);
    }

    void run (int chunks) { for (int i = 0; i < chunks; ++i) chunk(); }

    template <typename Check> void until (int chunks, Check check, const char* why)
    {
        for (int i = 0; i < chunks; ++i)
        {
            chunk();
            if (check()) return;
        }
        const auto status = post->liveCompareStatus();
        std::cerr << why << " [verdict " << static_cast<int> (status.verdict) << ", observation "
                  << static_cast<int> (status.observation) << ", blind stage "
                  << static_cast<int> (post->liveBlindStatus().stage) << "]\n";
        require (false, why);
    }

    void settled (int chunks, const char* why) { until (chunks, [&] { return exact >= 8; }, why); }
    void clean (const char* why) const
    {
        if (wrongFrames != 0) std::cerr << wrongFrames << " wrong frames\n";
        require (wrongFrames == 0, why);
    }

    void setDelay (std::int64_t newK) { k = newK; exact = 0; }
    void setPlaying (bool on) { preClock.playing = postClock.playing = on; exact = 0; }
    void seek (std::int64_t to) { transport = to; exact = 0; }
    void enterLoop (std::int64_t frames)
    {
        loopFrames = frames;
        for (auto* clock : { &preClock, &postClock })
        {
            clock->loopPlayback = true;
            clock->loopStartPpq = 0.0;
            clock->loopEndPpq = static_cast<double> (frames) / 24000.0;
        }
    }
    void leaveLoop()
    {
        loopFrames = 0;
        preClock.loopPlayback = postClock.loopPlayback = false;
    }

    static constexpr int chunkFrames = 1024, postFrames = 256, fadeFrames = 240;
    std::int64_t contentK = 512, loudFrom = std::numeric_limits<std::int64_t>::max();
    float postScale = 0.5f;
    hypha::tests::ProductTestClock wallClock;
    int channels, bufferFrames, exact = 0, withheld = 0;
    bool requireContentPosition = false, tonal = false, oppositePolarity = false;
    bool chord = false, limited = false; // music-like PRE; POST through EQ, a limiter and dither
    bool bass = false; // a strong bass line under swelling noise: two thirds of the windows are narrow
    // Content changes by content clock, so PRE's one chunk written ahead already holds them.
    std::int64_t quietUntil = std::numeric_limits<std::int64_t>::min();
    std::int64_t chordFrom = std::numeric_limits<std::int64_t>::max(), chordUntil = std::numeric_limits<std::int64_t>::max();
    std::int64_t unwritten() const noexcept { return engine + 4 * chunkFrames; } // neither side has reached it
    bool swappedChannels = false;
    bool unrelatedPost = false;
    int silentChannel = -1;
    std::int64_t rightContentShift = 0;
    std::int64_t wrongFrames = 0;
    // The frame oracle models unity POST; a BLIND trial that moved to the quieter side lowers POST, so
    // that stretch is checked by its product state instead (the renderer test checks its samples).
    bool auditOutput = true;
    std::int64_t engine = 1068544, transport = -1764, k = -512, loopFrames = 0;
    Clock preClock, postClock;
    std::unique_ptr<Processor> pre, post;

private:
    static float noise (std::int64_t index, int channel)
    {
        auto z = static_cast<std::uint64_t> (index * 2 + channel) + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return (static_cast<float> (z >> 40) / 16777216.0f - 0.5f) * 0.2f;
    }
    float signal (std::int64_t clock, int channel) const
    {
        if (clock < quietUntil || channel == silentChannel) return 0.0f;
        if (oppositePolarity) return channel == 0 ? noise (clock, 0) : -noise (clock, 0);
        if (tonal) return static_cast<float> (0.1 * std::sin (static_cast<double> (clock % 48) * (2.0 * 3.14159265358979323846 / 48.0) + channel));
        if (bass)
        {
            const double t = static_cast<double> (clock) / 48000.0, pi = 3.14159265358979323846;
            const double swell = std::max (0.0, std::sin (2.0 * pi * 0.4 * t));
            return static_cast<float> (0.2 * (std::sin (2.0 * pi * 55.0 * t + channel) + 0.5 * std::sin (2.0 * pi * 110.0 * t))
                                       + 6.0 * swell * swell * noise (clock, channel));
        }
        if (chord || (clock >= chordFrom && clock < chordUntil))
        {
            // A swelling chord: as in music, most of the spectrum holds almost nothing.
            const double t = static_cast<double> (clock) / 48000.0, pi = 3.14159265358979323846;
            double sum = 0.0; int partial = 0;
            for (const double hz : { 110.0, 164.81, 220.0, 277.18, 329.63, 440.0 })
                sum += std::sin (2.0 * pi * hz * t + partial++ + channel);
            return static_cast<float> (0.05 * sum * (0.75 + 0.25 * std::sin (2.0 * pi * 1.3 * t)));
        }
        return noise (clock, channel);
    }
    float source (std::int64_t clock, int channel) const { return signal (clock, channel) * (clock >= loudFrom ? 64.0f : 1.0f); }
    float input (std::int64_t clock, int channel) const
    {
        const auto at = clock - contentK + (unrelatedPost ? 123457 : 0) - (channel == 1 ? rightContentShift : 0);
        const int from = swappedChannels ? 1 - channel : channel;
        if (! limited) return postScale * signal (at, from);
        // A gentle high cut, a saturating limiter's gain reduction and -100 dBFS dither: the noise
        // fills the empty part of the spectrum, so the content check finds no position.
        const double eq = 0.6 * signal (at, from) + 0.4 * signal (at - 1, from);
        return static_cast<float> (postScale * std::tanh (4.0 * eq) / 4.0 + 1.0e-5 * noise (at + 7777777, from));
    }

    std::int64_t wrapped (std::int64_t position) const noexcept
    { return loopFrames > 0 && position >= loopFrames ? position - loopFrames : position; }
    // A delay before the insert puts POST's position behind the transport, even past a wrap; with
    // K below zero it runs ahead and wraps at the loop end like PRE's. Both as Pro Tools reported them.
    std::int64_t compensated() const noexcept { return wrapped (transport - k); }

    template <typename Expected>
    void compare (const juce::AudioBuffer<float>& out, Expected expected)
    {
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < bufferFrames; ++i)
            {
                const auto actual = out.getSample (ch, i), wanted = expected (i, ch);
                if (std::memcmp (&actual, &wanted, sizeof (float)) != 0) ++wrongFrames;
            }
    }

    void audit (const juce::AudioBuffer<float>& out, std::int64_t clock)
    {
        const auto status = post->liveCompareStatus();
        const auto alignment = post->liveCompareAlignFrames();
        if (alignment != previousAlignment && status.active && previousActive
            && status.verdict == hypha::live_compare::Verdict::accepted)
            require (weight <= 0.0f, "the real processor changes address only after old PRE is silent");
        previousAlignment = alignment;
        previousActive = status.active;
        const auto asPost = [&] (int i, int ch) { return input (clock + i, ch); };
        const auto read = requireContentPosition ? clock - contentK : clock - k - post->liveCompareAlignFrames();
        const auto asPre = [&] (int i, int ch) { return source (read + i, ch) * status.gain; };
        const bool idle = ! status.active && ! status.finishing && weight <= 0.0f;
        // A block without proof is POST from its first frame, and so is an idle one.
        if (idle || (! status.active && status.reason != hypha::live_compare::RecoveryReason::none)
            || status.verdict != hypha::live_compare::Verdict::accepted
            || status.observation == hypha::live_compare::RecoveryReason::ceiling
            || (status.observation == hypha::live_compare::RecoveryReason::calibrating
                || status.observation == hypha::live_compare::RecoveryReason::contentAmbiguous)
            || (LiveTimingFixtureAccess::gainRevision (*post).load() & 1u) != 0)
        {
            compare (out, asPost);
            if (status.active) ++withheld;
            weight = 0.0f;
            exact = 0;
            return;
        }
        if (requireContentPosition && status.preAudible && !tonal)
            require (k + alignment == contentK, "audible PRE always uses the current content position");
        // Every fade ends inside its block here: PRE's weight at the block's end names the target.
        const float target = status.preAudible ? 1.0f : 0.0f;
        const bool steady = target >= weight && target <= weight;
        exact = steady && target > 0.0f ? exact + 1 : 0;
        if (steady)
        {
            if (target > 0.0f) compare (out, asPre);
            else compare (out, asPost);
            return;
        }
        for (int ch = 0; ch < channels; ++ch)
        {
            auto faded = weight;
            for (int i = 0; i < bufferFrames; ++i)
            {
                faded = faded < target ? std::min (target, faded + 1.0f / fadeFrames)
                                       : std::max (target, faded - 1.0f / fadeFrames);
                const auto wanted = asPost (i, ch) * (1.0f - faded) + asPre (i, ch) * faded;
                if (std::abs (out.getSample (ch, i) - wanted) > 1.0e-6f) ++wrongFrames;
            }
        }
        weight = target;
    }

    float weight = 0.0f; // the renderer's PRE weight, as the audited output shows it
    std::int64_t previousAlignment = 0;
    bool previousActive = false;
};

}
