#pragma once

// Public playback conditions: a DAW owns callback boundaries and the user may cue before the
// captured range. Do not truncate the host's final callback to make a four-second trial pass.
static void nativeRangePlaybackContract()
{
    for (const auto rate : { 44'100u, 48'000u, 96'000u })
        for (const int blockFrames : { 64, 257, 512, 2048 })
            for (const int channels : { 1, 2 })
            {
                const auto frames = static_cast<std::int64_t> (rate) * 4;
                const std::int64_t start = 48'001;
                TrialFormat f { { 1, 2, 3, 4 }, rate, channels, start, frames,
                                static_cast<std::uint64_t> (frames), true };
                std::vector<float> post (static_cast<std::size_t> (frames) * channels);
                for (std::size_t i = 0; i < post.size(); ++i)
                    post[i] = static_cast<float> (int (i % 127) - 63) / 256.0f;
                auto pre = post;
                for (auto& sample : pre) sample *= 0.5f;
                LocalBlindTrial t (f, { 2.0f, 1.0f, false }, post, pre, false,
                                   post.size() * sizeof (float) * 2);
                std::vector<float> left (blockFrames), right (blockFrames);
                float* pointers[] { left.data(), right.data() };
                require (t.start(), "real-block trial starts explicitly");
                for (int side : { 1, 2 })
                {
                    if (side == 2) require (t.select (2), "explicit next side rearms the same exact range");
                    auto position = start - blockFrames * 2 - 17;
                    for (; position < start + frames + blockFrames; position += blockFrames)
                    {
                        std::fill (left.begin(), left.end(), 0.8f);
                        std::fill (right.begin(), right.end(), -0.4f);
                        TrialBlock b { f.epochs, rate, position, true, true, true, false };
                        inRt = true;
                        t.render (pointers, channels, blockFrames, b);
                        inRt = false;
                        require (t.view().failure == TrialFailure::none,
                                 "preroll and partial boundary callbacks are valid playback");
                        for (int c = 0; c < channels; ++c)
                            for (int i = 0; i < blockFrames; ++i)
                            {
                                const auto native = position + i;
                                const float expected = native >= start && native < start + frames
                                    ? post[static_cast<std::size_t> (native - start) * channels + c]
                                    : c == 0 ? 0.8f : -0.4f;
                                require (pointers[c][i] == expected,
                                         "only the exact native range is replaced; prefix/tail stay live");
                            }
                    }
                    // After a complete pass, a normal stop must leave the answer and next-side
                    // controls usable. Stopping an incomplete pass is still a terminal failure.
                    TrialBlock stopped { f.epochs, rate, position, true, false, true, false };
                    t.render (pointers, channels, blockFrames, stopped);
                    require (t.view().failure == TrialFailure::none,
                             "finished pass survives stopped host callbacks");
                    if (side == 1) require (! t.answer (TrialAnswer::one), "one complete side cannot answer");
                }
                require (t.answer (TrialAnswer::cannotDistinguish) && t.reveal(),
                         "two complete passes finish without a sample-exact DAW loop");
                t.stop();
                t.requestNormalReturn();
                TrialBlock normal { f.epochs, rate, 0, true, true, true, false };
                t.render (pointers, channels, blockFrames, normal);
                require (t.normalReturnConfirmed(), "normal return remains audio-confirmed");
            }
}

static void capturedClockPlaybackContract()
{
    for (bool beforeFirstCopy : { false, true })
        for (int change = 0; change < 6; ++change)
        {
            auto f = format();
            f.clock = { 1, 1, 32, 4096, true, true };
            LocalBlindTrial t (f, {}, std::vector<float> (512, 0.25f),
                               std::vector<float> (512, -0.125f), false, 4096);
            auto b = block();
            b.clock = f.clock;
            Buffer buffer;
            require (t.start(), "clock fixture arms explicitly");
            if (! beforeFirstCopy)
            {
                require (buffer.render (t, b) == TrialOutput::copy, "admitted host clock renders");
                b.position += 64;
            }
            if (change == 0) b.clock.source = 2;
            if (change == 1) b.clock.presentationSource = 2;
            if (change == 2) ++b.clock.inputLatency;
            if (change == 3) ++b.clock.outputLatency;
            if (change == 4) b.clock.hasInputLatency = false;
            if (change == 5) b.clock.hasOutputLatency = false;
            buffer.fill();
            require (buffer.render (t, b) == TrialOutput::untouched && buffer.original()
                         && t.view().failure == TrialFailure::clock,
                     "clock/PDC change before or during audition invalidates frozen alignment");
            b.clock = f.clock;
            require (buffer.render (t, b) == TrialOutput::untouched && ! t.select (2),
                     "restored clock cannot silently resume an invalid trial");
        }
}

static void stoppedHostClockContract()
{
    auto f = format();
    f.minimumHeardFrames = static_cast<std::uint64_t> (f.frames);
    f.clock = { 1, 1, 32, 4096, true, true };
    LocalBlindTrial trial (f, {}, std::vector<float> (512, 0.25f),
                           std::vector<float> (512, -0.125f), false, 4096);
    Buffer buffer;
    auto stopped = block();
    stopped.playing = false;
    stopped.positionValid = false;
    stopped.clock = {};
    require (trial.start(), "comparison can be armed while the host is stopped");
    buffer.fill();
    require (buffer.render (trial, stopped) == TrialOutput::untouched && buffer.original()
                 && trial.view().failure == TrialFailure::none,
             "a stopped host need not supply a playback clock before the first pass");
    for (auto position = f.start; position < f.start + f.frames; position += 64)
    {
        auto playing = block (position);
        playing.clock = f.clock;
        require (buffer.render (trial, playing) == TrialOutput::copy, "valid playing clock completes the range");
    }
    require (trial.view().passComplete, "full first pass is retained");
    buffer.fill();
    require (buffer.render (trial, stopped) == TrialOutput::untouched
                 && trial.view().failure == TrialFailure::none && trial.view().passComplete,
             "missing stopped clock does not erase a complete pass");
    require (trial.select (2), "second side arms explicitly while stopped");
    buffer.render (trial, stopped);
    auto changed = block();
    changed.clock = f.clock;
    ++changed.clock.outputLatency;
    require (buffer.render (trial, changed) == TrialOutput::untouched
                 && trial.view().failure == TrialFailure::clock,
             "the next playing callback must still match the captured clock and PDC");
}
