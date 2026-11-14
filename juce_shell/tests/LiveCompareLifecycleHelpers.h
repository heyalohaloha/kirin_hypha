// Included inside Lifecycle: existing restore and fade fixture helpers.
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
