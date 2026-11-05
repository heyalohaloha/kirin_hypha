#pragma once
#include "AaxAnalysisProductFixture.h"

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
    explicit ChunkedPost (int channelsIn) : channels (channelsIn)
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

    void chunk()
    {
        // PRE first, one chunk ahead of POST's clock.
        const auto ahead = wrapped (transport + chunkFrames);
        preClock.engine = engine + chunkFrames;
        preClock.project = ahead;
        juce::AudioBuffer<float> preAudio (channels, chunkFrames);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < chunkFrames; ++i) preAudio.setSample (ch, i, noise (preClock.engine + i, ch));
        juce::MidiBuffer midi;
        pre->processBlock (preAudio, midi);
        for (int offset = 0; offset < chunkFrames; offset += postFrames)
        {
            postClock.engine = engine + offset;
            postClock.project = transport - k; // one compensated position for the whole chunk
            juce::AudioBuffer<float> audio (channels, postFrames);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < postFrames; ++i) audio.setSample (ch, i, input (postClock.engine + i, ch));
            post->processBlock (audio, midi);
            audit (audio, postClock.engine);
        }
        engine += chunkFrames;
        transport = ahead;
        wallClock.advanceBlock (chunkFrames, 48000);
        LiveTimingFixtureAccess::serviceTiming (*post);
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
    static constexpr std::int64_t contentK = 512;
    hypha::tests::ProductTestClock wallClock;
    int channels, exact = 0, withheld = 0;
    std::int64_t wrongFrames = 0;
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
    static float input (std::int64_t clock, int channel) { return 0.5f * noise (clock - contentK, channel); }

    std::int64_t wrapped (std::int64_t position) const noexcept
    { return loopFrames > 0 && position >= loopFrames ? position - loopFrames : position; }

    template <typename Expected>
    void compare (const juce::AudioBuffer<float>& out, Expected expected)
    {
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < postFrames; ++i)
            {
                const auto actual = out.getSample (ch, i), wanted = expected (i, ch);
                if (std::memcmp (&actual, &wanted, sizeof (float)) != 0) ++wrongFrames;
            }
    }

    void audit (const juce::AudioBuffer<float>& out, std::int64_t clock)
    {
        const auto status = post->liveCompareStatus();
        const auto asPost = [&] (int i, int ch) { return input (clock + i, ch); };
        const auto asPre = [&] (int i, int ch) { return noise (clock - k + i, ch) * status.gain; };
        const bool idle = ! status.active && ! status.finishing && weight <= 0.0f;
        // A block without proof is POST from its first frame, and so is an idle one.
        if (idle || status.verdict != hypha::live_compare::Verdict::accepted)
        {
            compare (out, asPost);
            if (status.active) ++withheld;
            weight = 0.0f;
            exact = 0;
            return;
        }
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
            for (int i = 0; i < postFrames; ++i)
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
};

inline void chunkedPost (int channels)
{
    using hypha::live_compare::BlindStage;
    using hypha::live_compare::Verdict;
    ChunkedPost chain (channels);
    auto& post = *chain.post;
    constexpr int lap = 47; // chunks in a 48000-frame loop
    chain.until (200, [&] { return post.setPairCandidate (chain.pre->instanceId(), {}); }, "exact PRE is selected");
    require (post.startLiveCompare() == hypha::live_compare::StartResult::started, "LISTEN starts with POST at the hardware buffer");
    post.selectLiveComparePre (true);
    chain.settled (40, "LISTEN plays PRE through the position the host did not report");
    chain.run (20);
    require (chain.exact >= 80, "every POST callback plays the PRE at clock minus K");
    chain.until (400, [&] { return post.measureLiveCompareOffset().determined; }, "the estimate sees the audio arrive late");
    require (post.measureLiveCompareOffset().lagFrames == -1024, "the footer reports PRE 21.33 ms early");
    chain.clean ("LISTEN never played a wrong frame");

    // H/W Insert Delay 21.33 ms from zero: the host moves the position, K follows, the offset goes.
    const auto delayed = chain.withheld;
    chain.setDelay (512);
    chain.settled (40, "PRE returns on the new K");
    require (chain.withheld > delayed, "the old K is not used across the delay change");
    chain.until (400, [&] { return post.measureLiveCompareOffset().determined
                                   && post.measureLiveCompareOffset().lagFrames == 0; }, "the delay removes the offset");
    chain.clean ("the delay change never played the old K");

    // A seek: the selection stays, K proves itself again. The position is chosen so that the
    // last callback of the chunk that crosses the loop end below starts past it.
    const auto seeked = chain.withheld;
    chain.seek (20200 - 16 * ChunkedPost::chunkFrames);
    chain.settled (40, "PRE returns after a seek without another press");
    require (chain.withheld > seeked && post.liveCompareStatus().preSelected, "the seek withheld PRE and kept the selection");
    chain.clean ("the seek never played a wrong frame");

    // Stop and play: the first chunk after play passes as the host reported it, then K proves
    // itself again and the same selection returns.
    chain.setPlaying (false);
    chain.run (6);
    require (chain.exact == 0 && post.liveCompareStatus().preSelected, "a stop is POST with the selection kept");
    chain.setPlaying (true);
    chain.seek (20200 - 16 * ChunkedPost::chunkFrames);
    chain.settled (40, "PRE returns after play without another press");
    chain.clean ("stop and play never played a wrong frame");

    // Into a loop with K proven: PRE stays until the callbacks past the first wrap, then returns.
    chain.enterLoop (48000);
    const auto entered = chain.withheld;
    chain.run (6);
    require (chain.withheld == entered && chain.exact > 0, "a known K carries into the loop");
    chain.until (lap, [&] { return chain.withheld > entered; }, "the first wrap withholds what POST cannot name");
    chain.settled (14, "the measured cycle returns PRE without another lap");
    const auto looping = chain.withheld;
    chain.run (lap * 3);
    require (chain.withheld == looping, "later wraps keep PRE on");
    chain.clean ("the loop never played a wrong frame");

    // Below zero K has no proof past a wrap: POST, the selection kept, until something proves it.
    chain.setDelay (-512);
    chain.run (lap * 2);
    require (chain.exact == 0 && post.liveCompareStatus().preSelected, "a negative K stays on POST in a loop");
    chain.leaveLoop();
    chain.seek (20000 - 16 * ChunkedPost::chunkFrames);
    chain.settled (40, "linear playback proves the negative K again without another press");
    chain.enterLoop (48000);
    chain.until (lap, [&] { return chain.exact == 0; }, "the first wrap withholds PRE");
    chain.run (lap);
    require (chain.exact == 0, "still no loop proof below zero");
    chain.setDelay (512);
    chain.settled (lap * 3, "a positive K proves itself inside the loop without another press");
    chain.clean ("re-acquiring never played a wrong frame");
    chain.leaveLoop();
    chain.seek (0);
    chain.settled (40, "linear playback again");

    post.finishLiveCompare();
    chain.until (200, [&] { post.serviceLiveBlind(); const auto s = post.liveCompareStatus(); return ! s.active && ! s.finishing; },
                 "END returns to POST");

    require (post.beginLiveBlind() == hypha::live_compare::StartResult::started, "BLIND starts with POST at the hardware buffer");
    chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; }, "BLIND prepares and starts");
    const auto trial = post.liveBlindStatus().trial.epoch;
    const auto gain = post.liveCompareStatus().gain;
    require (gain < 0.75f, "MATCH lowered PRE to POST's level");
    require (post.selectLiveBlind (1), "the first source is selected");
    chain.run (3);
    const bool firstIsPre = post.liveCompareStatus().preAudible;
    require (post.selectLiveBlind (2), "the second source is selected");
    chain.run (3);
    require (post.liveCompareStatus().preAudible != firstIsPre, "the two sources are PRE and POST");
    require (post.selectLiveBlind (firstIsPre ? 1 : 2), "PRE's source is selected");
    chain.settled (10, "BLIND plays PRE");

    // A seek in the trial: POST for the unproven blocks, then the same source, gain and assignment.
    const auto interrupted = chain.withheld;
    chain.seek (chain.transport + 96000);
    chain.settled (40, "BLIND returns to the same source after a seek");
    const auto resumed = post.liveBlindStatus();
    require (chain.withheld > interrupted && resumed.stage == BlindStage::active && resumed.trial.epoch == trial,
             "BLIND keeps its trial across the seek");
    const auto after = post.liveCompareStatus();
    require (after.preAudible && after.gain == gain && after.verdict == Verdict::accepted, "the same source at the same gain");
    require (post.revealLiveBlind() && post.liveBlindStatus().trial.usedPostFallback, "REVEAL says POST stood in");
    chain.clean ("BLIND never played a wrong frame");
    post.closeLiveBlind();
    chain.until (200, [&] { post.serviceLiveBlind(); const auto s = post.liveCompareStatus(); return ! s.active && ! s.finishing; },
                 "BLIND closes");

    // A prepare ends the chunk proof on the Audio Thread; only a reported chunk proves it again.
    require (LiveTimingFixtureAccess::chunkProven (post), "the fixture holds a proven chunk");
    hypha::tests::prepareFixtureProcessor (post, 48000, ChunkedPost::chunkFrames);
    LiveTimingFixtureAccess::configureAaxClock (post); // the same certificate: only the generation moved
    chain.chunk();
    require (! LiveTimingFixtureAccess::chunkProven (post), "a prepare ends the proof");
    chain.chunk();
    require (LiveTimingFixtureAccess::chunkProven (post), "the next closed chunk proves it");
    chain.clean ("nothing wrong sounded");
}
}
