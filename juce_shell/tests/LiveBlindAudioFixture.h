#pragma once

// Included in BlindContract. Test-only host/physical delay/independent PCM oracle. Expected
// samples never use the product's K, ring read address, PPQ or reported output selection.
float sourceSample (int channel, std::int64_t frame) const
{
    if (frame < 0) return 0;
    if (! loopMode) return signal[static_cast<std::size_t> (frame) % signal.size()];
    const auto token = static_cast<std::uint32_t> (frame + 1) * 0x9e3779b1u;
    return static_cast<float> (((token >> (channel == 0 ? 0 : 16)) & 0xffffu) + 1) / 131072.0f;
}

void processAudio()
{
    juce::AudioBuffer<float> buffer (2, blockFrames);
    juce::MidiBuffer midi;
    std::array<std::array<float, 4096>, 2> physicalDelay {};
    std::size_t delayHead = 0;
    std::int64_t emitted = 0;
    auto next = std::chrono::steady_clock::now();
    auto previousPre = std::chrono::steady_clock::time_point();
    auto previousPost = std::chrono::steady_clock::time_point();
    // The test machine's own stalls, apart from the deliberate gaps below. The product calls an
    // interval longer than 2.5 blocks a callback gap, so each side is measured where its callback
    // starts, before the call, and a failure report carries the longest interval.
    const auto countStall = [this] (std::chrono::steady_clock::time_point& previous) {
        const auto callbackAt = std::chrono::steady_clock::now();
        if (previous != std::chrono::steady_clock::time_point())
        {
            const auto interval = static_cast<std::int64_t> (
                std::chrono::duration_cast<std::chrono::microseconds> (callbackAt - previous).count());
            longestCallbackMicros.store (std::max (longestCallbackMicros.load(), interval));
            if (interval * 48000 > static_cast<std::int64_t> (blockFrames) * 2'500'000) machineStalls.fetch_add (1);
        }
        previous = callbackAt;
    };
    while (running.load())
    {
        if (suspendAudio.load())
        {
            suspended.store (true);
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
            next = std::chrono::steady_clock::now();
            previousPre = previousPost = {};
            continue;
        }
        suspended.store (false);
        if (injectGap.exchange (false))
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (600));
            next = std::chrono::steady_clock::now();
            previousPre = previousPost = {};
        }
        postClock.playing = clock.playing = play.load();
        clock.loop.advance (clock.position);
        postClock.position = clock.position - (loopMode ? 4096 : 0);
        const auto inputPosition = loopMode ? emitted : clock.position;
        const auto delayedPosition = inputPosition - (loopMode ? 4096 : 0);
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < blockFrames; ++f)
                buffer.setSample (c, f, sourceSample (c, inputPosition + f));
        countStall (previousPre);
        pre->processBlock (buffer, midi);
        for (int f = 0; f < blockFrames; ++f)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float current = buffer.getSample (c, f);
                const float delayed = loopMode ? physicalDelay[static_cast<std::size_t> (c)][delayHead] : current;
                physicalDelay[static_cast<std::size_t> (c)][delayHead] = current;
                buffer.setSample (c, f, processedInput (delayed));
            }
            delayHead = (delayHead + 1) % 4096;
        }
        const auto before = LiveTimingFixtureAccess::audioView (*post);
        const bool renderedOffline = offline.load();
        post->setNonRealtime (renderedOffline);
        countStall (previousPost);
        post->processBlock (buffer, midi);
        const auto after = LiveTimingFixtureAccess::audioView (*post);
        const float inputEnd = sourceSample (0, delayedPosition + blockFrames - 1);
        const float outputEnd = buffer.getSample (0, blockFrames - 1);
        lastOutputRatio.store (outputEnd / inputEnd);
        lastPcmError.store (std::min (std::abs (outputEnd - inputEnd * before.gain),
                                    std::abs (outputEnd - processedInput (inputEnd) * before.postActual)));
        if (loopMode && loopPcmAuditActive.load() && clock.loop.laps.load() >= 1 && before.active && after.active
            && before.matched && after.matched && before.preAudible == after.preAudible)
        {
            require (! after.preWaiting && after.verdict == hypha::live_compare::Verdict::accepted,
                     "normal wrap must not hide POST fallback as a completed comparison");
            // Skip the single fade block by requiring the prior block to have the same side.
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < blockFrames; ++f)
                {
                    const float original = sourceSample (c, delayedPosition + f);
                    const float expected = after.preAudible ? original * before.gain
                                                           : processedInput (original) * before.postActual;
                    if (std::abs (buffer.getSample (c, f) - expected) > 0.0000001f) loopPcmErrors.fetch_add (1);
                }
            (after.preAudible ? loopPreFrames : loopPostFrames).fetch_add (blockFrames);
        }
        audioBlocks.fetch_add (1);
        if (renderedOffline || (! before.active && ! before.finishing && before.postActual >= 1.0f
            && ! after.active && after.postTarget >= 1.0f))
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < blockFrames; ++f)
                {
                    const float expected = processedInput (sourceSample (c, delayedPosition + f));
                    const float actual = buffer.getSample (c, f);
                    if (std::memcmp (&actual, &expected, sizeof (float)) != 0) rawPostErrors.fetch_add (1);
                }
        if (clock.playing) clock.position += blockFrames;
        emitted += blockFrames;
        next += std::chrono::nanoseconds (static_cast<long long> (blockFrames) * 1'000'000'000 / 48000);
        std::this_thread::sleep_until (next);
    }
}

std::atomic<std::uint64_t> loopPreFrames { 0 }, loopPostFrames { 0 }, loopPcmErrors { 0 };
std::atomic<std::int64_t> longestCallbackMicros { 0 };
std::atomic<int> machineStalls { 0 };
std::atomic<bool> loopPcmAuditActive { false }; // only the first MATCH/Blind session, not later fault/END fixtures
