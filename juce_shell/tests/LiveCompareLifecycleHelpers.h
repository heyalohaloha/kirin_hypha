// Included inside Lifecycle: existing restore and fade fixture helpers.
    void inspectAuditionBlock (const juce::AudioBuffer<float>& buffer)
    {
        if (checkFirstPre.exchange (false))
        {
            for (int f = 0; f < frames; ++f)
            {
                const auto input = signal[static_cast<std::size_t> (clock.position + f) % signal.size()];
                if (std::abs (input) > 0.01f && buffer.getSample (0, f) * input <= 0.0f) ++firstPreErrors;
            }
            firstPreDone.fetch_add (1, std::memory_order_release);
        }
        if (captureRamp.load() && rampBlocks.load() < rampBlockCount)
        {
            const auto offset = static_cast<std::size_t> (rampBlocks.load()) * frames * 2;
            for (int f = 0; f < frames; ++f)
                for (int c = 0; c < 2; ++c)
                {
                    rampSamples[offset + static_cast<std::size_t> (f * 2 + c)] = buffer.getSample (c, f);
                    const auto input = (clock.position + f) % 4 < 2 ? 0.44615422f : -0.44615422f;
                    if (buffer.getSample (c, f) * input <= 0.0f) ++rampPreErrors;
                }
            rampBlocks.fetch_add (1, std::memory_order_release);
        }
    }
    void verifyRampPeak()
    {
        KirinReferenceGainFacts facts {};
        require (kirin_hypha_analyze_reference_gain (rampSamples.data(), rampSamples.data(),
                    frames * rampBlockCount, 48000, 2, &facts), "captured ramp true peak is measurable");
        require (rampPreErrors.load() == 0, "PRE remains audible throughout the captured ramp");
        if (facts.a_cue_true_peak_millidbtp > -1000)
            std::cerr << "rendered ramp true peak (milli-dBTP): " << facts.a_cue_true_peak_millidbtp << '\n';
        require (facts.a_cue_true_peak_millidbtp <= -1000,
                 "rendered output true peak stays within the ceiling through the MATCH on ramp");
    }
    void restore (bool endRequested = false)
    {
        pause.store (true);
        waitWithoutMessageService ([this] { return paused.load(); });
        held = post->liveCompareStatus().postActual;
        const auto approval = post->liveBlindStatus().generation;
        const auto oldMatch = post->measureLiveCompare();
        juce::MemoryBlock saved;
        post->getStateInformation (saved);
        if (endRequested) post->finishLiveCompare();
        std::thread host ([&]
        {
            if (mode == "restore-invalid") post->setStateInformation (nullptr, 0);
            else post->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        });
        host.join();
        require (! post->revealLiveBlind() && ! post->selectLiveBlind (1), "old trial controls rejected before service");
        require (! post->liveCompareStatus().matched && ! post->liveCompareStatus().active,
                 "MATCH and output permission immediately revoked");
        require (! post->liveBlindStatus().trial.revealed && post->liveBlindStatus().trial.played == 0,
                 "mapping and played receipts immediately hidden");
        if (oldMatch.ok()) require (! post->applyLiveCompareMatch (planMatch (oldMatch, 0.0), MatchChoice::basis),
                                   "old measurement rejected after restore");
        const auto before = blocks.load();
        expectedPost.store (held);
        verifyPost.store (! endRequested);
        pause.store (false);
        waitWithoutMessageService ([&] { return blocks.load() > before; });
        require (postErrors.load() == 0, "first buffer is held POST only without message service");
        if (! endRequested) require (std::abs (post->liveCompareStatus().postActual - held) <= 0.0f, "restore never raises POST");
        require (! post->approveLiveBlindMatch (approval), "old approval rejected");
        if (endRequested) require (post->liveCompareStatus().finishing || post->liveCompareStatus().postActual == 1.0f,
                                  "explicit END survives until actual unity");
        checkedAt = Steady::now();
    }
    void closeWithFade()
    {
        pause.store (true);
        waitWithoutMessageService ([this] { return paused.load(); });
        require (ratio.load() > 0.99f, "PRE is fully audible before closing");
        post->closeLiveBlind();
        require (LiveTimingFixtureAccess::retainsFadeSource (*post), "closing retains the proven PRE source and demand for its fade");
        const auto before = blocks.load();
        verifyClose.store (true); pause.store (false);
        waitWithoutMessageService ([&] { return blocks.load() > before; }, 1000);
        require (fadeChecks.load() == 1 && fadeErrors.load() == 0, "first closing buffer fades from PRE to exact POST");
        post->serviceLiveCompare();
        require (! LiveTimingFixtureAccess::retainsFadeSource (*post), "completed fade releases its PRE source");
        passed = true; stopTimer(); juce::MessageManager::getInstance()->stopDispatchLoop();
    }
