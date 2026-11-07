#pragma once
#include "ContentAnalysisProductFixture.h"

namespace reaper_align_product
{
using namespace content_analysis_product;
using namespace hypha::live_compare;

// A long-lived instance's local counter has a different origin. PCM/project positions stay
// identical when that origin moves; neither the origin nor K measures ring occupancy.
inline void check (int channels, std::int64_t origin)
{
    hypha::tests::ProductTestClock wall;
    Clock preClock, postClock;
    preClock.reaper = postClock.reaper = true;
    preClock.reported = postClock.reported = false;
    std::unique_ptr<Processor> pre, post;
    constexpr int frames = 1024;
    for (const auto role : { Processor::Role::Pre, Processor::Role::Post })
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        auto processor = std::make_unique<Processor> (role);
        auto layout = processor->getBusesLayout();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
        require (processor->setBusesLayout (layout), "REAPER alignment layout");
        processor->setPlayHead (role == Processor::Role::Pre ? &preClock : &postClock);
        hypha::tests::prepareFixtureProcessor (*processor, 48000, frames);
        require (LiveTimingFixtureAccess::configureVst3Clock (*processor, "REAPER", "7.71.0_ba637e1u"),
                 "REAPER alignment clock policy");
        LiveTimingFixtureAccess::advanceReaperOrigin (*processor,
            role == Processor::Role::Pre ? std::max (std::int64_t (0), -origin) : std::max (std::int64_t (0), origin));
        LiveTimingFixtureAccess::enableFixtureWrites (*processor);
        (role == Processor::Role::Pre ? pre : post) = std::move (processor);
    }
    const auto noise = [] (std::int64_t position, int channel) {
        auto z = static_cast<std::uint64_t> (position) * 2 + static_cast<std::uint64_t> (channel) + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; z ^= z >> 31;
        return (static_cast<float> (z >> 40) / 16777216.0f - 0.5f) * 0.2f;
    };
    int delay = 1024, exact = 0;
    float weight = 0.0f;
    bool retainAlignment = false, ended = false;
    const auto block = [&] {
        juce::AudioBuffer<float> audio (channels, frames);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < frames; ++i) audio.setSample (ch, i, noise (preClock.content + i, ch));
        pre->processBlock (audio, midi);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < frames; ++i)
            {
                const auto actual = audio.getSample (ch, i), expected = noise (preClock.content + i, ch);
                require (std::memcmp (&actual, &expected, sizeof (float)) == 0, "PRE remains bit identical");
                audio.setSample (ch, i, noise (postClock.content + i - delay, ch) * 0.5f);
            }
        post->processBlock (audio, midi);
        const auto state = post->liveCompareStatus();
        const auto shown = post->liveCompareAlignFrames();
        if (retainAlignment) require (shown == delay, "recalibrating K never drops the retained correction");
        // END's fade still reads the sounding address; a closed session only stops showing it.
        const auto alignment = ended ? delay : shown;
        const auto target = state.preAudible ? 1.0f : 0.0f;
        if (state.verdict != Verdict::accepted) weight = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto faded = weight;
            for (int i = 0; i < frames; ++i)
            {
                faded = faded < target ? std::min (target, faded + 1.0f / 240.0f) : std::max (target, faded - 1.0f / 240.0f);
                const auto a = noise (postClock.content + i - delay, ch) * 0.5f;
                const auto b = noise (postClock.content + i - alignment, ch) * state.gain;
                const auto expected = faded == 0.0f ? a : faded == 1.0f ? b : a * (1.0f - faded) + b * faded;
                require (std::abs (audio.getSample (ch, i) - expected) < 1.0e-6f,
                         "every REAPER sample uses the current PRE address or unchanged POST, including fades");
            }
        }
        exact = weight == 1.0f && target == 1.0f && alignment == delay ? exact + 1 : 0;
        weight = target;
        require (pre->getLatencySamples() == 0 && post->getLatencySamples() == 0, "alignment adds no host latency");
        if (preClock.playing) preClock.content += frames;
        if (postClock.playing) postClock.content += frames;
        wall.advanceBlock (frames, 48000);
        LiveTimingFixtureAccess::serviceProcessorTimer (*post);
    };
    const auto until = [&] (auto ready, const char* why) {
        for (int i = 0; i < 450; ++i) { block(); if (ready()) return; }
        require (false, why);
    };
    until ([&] { return post->setPairCandidate (pre->instanceId(), {}); }, "REAPER origin fixture selects PRE");
    require (post->startLiveCompare() == StartResult::started, "REAPER origin LISTEN starts");
    post->selectLiveComparePre (true);
    until ([&] { return exact >= 8; }, "large origin still auto-aligns PRE");
    const auto estimate = post->measureLiveCompareOffset();
    require (offsetCanVote (estimate) && estimate.clockK == origin && estimate.lagFrames == -delay,
             "full 64-bit origin is proven while content estimates only the unreported delay");
    delay = 2048;
    until ([&] { return exact >= 8; }, "delay changes align again during continuous playback");
    for (int i = 0; i < 32; ++i) block();
    require (post->liveCompareStatus().preSelected && post->liveCompareStatus().preAudible
        && ! post->liveCompareStatus().contentHeld && post->liveCompareAlignFrames() == delay,
             "large origin never requires stop or reselection after a correctable jump");
    retainAlignment = true;
    auto expectedK = origin;
    const auto resumeAfterUnequalStop = [&] (bool extraPre, bool blind) {
        const auto gain = post->liveCompareStatus().gain;
        const auto trial = post->liveBlindStatus().trial;
        preClock.playing = postClock.playing = false;
        block();
        require (! post->liveCompareStatus().preAudible, "stopped transport sounds only POST");
        // Real callbacks, with one host instance alone continuing while transport is stopped.
        auto& processor = extraPre ? *pre : *post;
        const auto position = extraPre ? preClock.content : postClock.content - delay;
        const auto scale = extraPre ? 1.0f : 0.5f;
        for (int n = 0; n < 3; ++n)
        {
            juce::AudioBuffer<float> audio (channels, frames);
            juce::MidiBuffer midi;
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < frames; ++i) audio.setSample (ch, i, noise (position + i, ch) * scale);
            processor.processBlock (audio, midi);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < frames; ++i)
                {
                    const auto expected = noise (position + i, ch) * scale, actual = audio.getSample (ch, i);
                    require (std::memcmp (&actual, &expected, sizeof (float)) == 0,
                             "one-sided stopped callbacks remain bit identical");
                }
            wall.advanceBlock (frames, 48000);
            LiveTimingFixtureAccess::serviceProcessorTimer (*post);
        }
        expectedK += (extraPre ? -3 : 3) * frames;
        preClock.playing = postClock.playing = true;
        exact = 0;
        until ([&] { return exact >= 8; }, "the same selected PRE returns aligned on fresh proof");
        until ([&] {
            const auto fresh = post->measureLiveCompareOffset();
            return offsetCanVote (fresh) && fresh.clockK == expectedK && fresh.lagFrames == -delay;
        }, "the real callbacks recalibrate K independently of content delay");
        require (post->liveCompareStatus().gain == gain, "recalibration retains the approved gain");
        if (blind)
        {
            const auto current = post->liveBlindStatus();
            require (current.stage == BlindStage::active && current.trial.epoch == trial.epoch
                && current.trial.selected == trial.selected && current.trial.firstPre == trial.firstPre,
                     "BLIND retains the trial and anonymous PRE without a source switch");
        }
        else require (post->liveCompareStatus().preSelected, "LISTEN retains the explicit PRE selection");
    };
    for (bool extraPre : { true, false }) resumeAfterUnequalStop (extraPre, false);
    post->selectLiveComparePre (false);
    for (int i = 0; i < 4; ++i) block();
    require (post->beginLiveBlind() == StartResult::started, "aligned REAPER BLIND starts");
    until ([&] { return post->liveBlindStatus().stage == BlindStage::active; }, "BLIND prepares through the real service");
    const auto command = LiveTimingFixtureAccess::command (*post);
    require (post->selectLiveBlind (command.firstPre() ? 1 : 2), "select anonymous aligned PRE");
    until ([&] { return exact >= 8; }, "anonymous PRE is aligned before stopping");
    for (bool extraPre : { true, false }) resumeAfterUnequalStop (extraPre, true);
    post->finishLiveCompare();
    retainAlignment = false; ended = true;
    for (int i = 0; i < 4; ++i) block();
    require (post->liveCompareAlignFrames() == 0, "a closed session claims no correction");
    post->releaseResources(); pre->releaseResources();
}
}
