#pragma once

static void localBlindTransitionContract()
{
    constexpr std::int64_t start = 1003, frames = 4800;
    constexpr std::uint32_t fadeFrames = 240;
    for (bool lower : { false, true })
    {
        std::vector<float> expected;
        for (int blockFrames : { 1, 17, 64, 257, 2048 })
        {
            TrialFormat f { { 1, 2, 3, 4 }, 48000, 2, start, frames,
                            static_cast<std::uint64_t> (frames), false, {}, fadeFrames };
            const auto gain = lower ? TrialGain { 2.0f, 0.5f, true } : TrialGain {};
            const float live = lower ? 0.5f : 0.25f;
            LocalBlindTrial t (f, gain, std::vector<float> (frames * 2, live),
                               std::vector<float> (frames * 2, lower ? 0.25f : -0.25f),
                               ! lower, frames * 2 * sizeof (float) * 2);
            require (t.start (lower), "transition fixture starts explicitly");
            std::vector<float> left (blockFrames), right (blockFrames), rendered;
            float* pointers[] { left.data(), right.data() };
            for (std::int64_t position = start - 11; position < start + frames + 64; position += blockFrames)
            {
                std::fill (left.begin(), left.end(), live);
                std::fill (right.begin(), right.end(), live);
                TrialBlock b { f.epochs, 48000, position, true, true, true, false };
                inRt = true;
                t.render (pointers, 2, blockFrames, b);
                inRt = false;
                require (t.view().failure == TrialFailure::none, "de-click preserves exact playback");
                for (int i = 0; i < blockFrames && position + i < start + frames + 64; ++i)
                {
                    require (left[i] == right[i], "stereo channels share one de-click weight");
                    rendered.push_back (left[i]);
                }
            }
            if (expected.empty()) expected = rendered;
            require (rendered == expected, "de-click is sample-identical across callback partitions");
            float maximumStep = 0;
            for (std::size_t i = 1; i < rendered.size(); ++i)
                maximumStep = std::max (maximumStep, std::abs (rendered[i] - rendered[i - 1]));
            require (maximumStep <= 0.5f / fadeFrames + 0.000001f,
                     "range edges eliminate the full-level hard step");
            require (rendered[11] == live, "first captured sample connects continuously to live input");
            require (rendered[11 + frames / 2] == (lower ? 0.25f : -0.25f),
                     "interior uses the original fixed-gain copy");
            require (rendered[11 + frames] == 0.25f,
                     "range exit preserves the approved held level or unchanged live signal");
            if (blockFrames == 1)
                std::cout << "Blind edge transition: lower=" << lower
                          << " fade_frames=" << fadeFrames << " max_step=" << maximumStep << '\n';
        }
    }

    TrialFormat f { { 1, 2, 3, 4 }, 48000, 2, start, frames,
                    static_cast<std::uint64_t> (frames), false, {}, fadeFrames };
    LocalBlindTrial t (f, {}, std::vector<float> (frames * 2, 0.25f),
                       std::vector<float> (frames * 2, -0.25f), false, frames * 16);
    t.start();
    float left = 0.25f, right = left, previous = left;
    float* pointers[] { &left, &right };
    float maximumSwitchStep = 0;
    for (std::int64_t offset = 0; offset < frames; ++offset)
    {
        if (offset == 1000) require (t.select (2), "explicit source switch");
        if (offset == 1077) require (t.select (1), "rapid reversed source switch");
        if (offset == 1173) require (t.select (2), "rapid second source switch");
        left = right = 0.25f;
        TrialBlock b { f.epochs, 48000, start + offset, true, true, true, false };
        inRt = true;
        t.render (pointers, 2, 1, b);
        inRt = false;
        require (left == right && std::isfinite (left), "source transition is stereo coherent and finite");
        maximumSwitchStep = std::max (maximumSwitchStep, std::abs (left - previous));
        previous = left;
    }
    require (maximumSwitchStep <= 0.5f / fadeFrames + 0.000001f,
             "rapid switching retains the same bounded de-click slope");
    require (! t.answer (TrialAnswer::one), "crossfaded fragments cannot qualify as full passes");

    LocalBlindTrial same (f, {}, std::vector<float> (frames * 2, -0.0f),
                          std::vector<float> (frames * 2, -0.0f), false, frames * 16);
    same.start();
    const float negativeZero = -0.0f;
    for (std::int64_t offset = 0; offset < frames; ++offset)
    {
        left = right = negativeZero;
        TrialBlock b { f.epochs, 48000, start + offset, true, true, true, false };
        same.render (pointers, 2, 1, b);
        require (std::memcmp (&left, &negativeZero, sizeof (float)) == 0,
                 "equal live/copy signed-zero control stays bit identical through fades");
    }
}
