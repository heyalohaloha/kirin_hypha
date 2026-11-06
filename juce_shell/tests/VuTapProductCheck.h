#pragma once
// Included in the existing shipping-editor fixture namespace.
struct VuTimerCallback
{
    using Type = void (KirinHyphaEditor::*)();
    friend Type testMember (VuTimerCallback);
};
template struct TestAccess<VuTimerCallback, &KirinHyphaEditor::timerCallback>;

void expectVuLiveFrame (hypha::observatory::View& view, const hypha::hybrid_vu::LiveReading& reading,
                       std::uint64_t began, std::uint64_t ended, double elapsed = 0.0)
{
    using namespace hypha::hybrid_vu;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto initial = reading.needles[(size_t) ch];
        const auto at = [&] (std::uint64_t now)
        {
            auto state = initial;
            const auto age = now > reading.wallNanos ? static_cast<double> (now - reading.wallNanos) * 1.0e-9 : 0.0;
            NeedleStep::forSeconds (std::max (0.0, age + elapsed - reading.aheadSeconds)).apply (state, 0.0);
            return state.value;
        };
        // The frame accepts its snapshot between these clock reads. Zero-input energy only
        // decreases, so this velocity bound also covers a scheduler pause inside that interval.
        const auto uncertainty = std::hypot (NeedleStep::omega * initial.value, initial.velocity)
                               * static_cast<double> (ended - began) * 1.0e-9 + 1.0e-9;
        const auto a = at (began), b = at (ended);
        const auto low = vuNormalized (needleDbfs (std::min (a, b) - uncertainty), view.vuCalibration());
        const auto high = vuNormalized (needleDbfs (std::max (a, b) + uncertainty), view.vuCalibration());
        require (view.vuPositionForTest (ch) >= low - 1.0e-6 && view.vuPositionForTest (ch) <= high + 1.0e-6,
                 "shipping needles follow the publication time and processed audio horizon");
    }
}

void verifyVuTapProduct()
{
    struct Clock final : juce::AudioPlayHead
    {
        bool playing = true, hasPosition = true;
        int64_t position = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info; info.setIsPlaying (playing); info.setIsRecording (false);
            if (hasPosition) info.setTimeInSamples (position);
            info.setKirinAuUsesHostTransportTimeline (true);
            return info;
        }
    };
    for (const auto wrapper : { juce::AudioProcessor::wrapperType_AudioUnit,
                               juce::AudioProcessor::wrapperType_VST3,
                               juce::AudioProcessor::wrapperType_AAX })
        for (const auto role : { Processor::Role::Pre, Processor::Role::Post })
        {
            auto due = std::chrono::steady_clock::now();
            const auto advanceFrames = [&due] (int frames)
            {
                due += std::chrono::nanoseconds (static_cast<std::int64_t> (frames) * 1'000'000'000 / 48000);
                if (std::chrono::steady_clock::now() < due) std::this_thread::sleep_until (due);
            };
            juce::AudioProcessor::setTypeOfNextNewPlugin (wrapper);
            Processor processor (role); Clock clock; processor.setPlayHead (&clock);
            hypha::tests::prepareFixtureProcessor (processor, 48000, 512);
            auto editor = std::unique_ptr<KirinHyphaEditor> (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
            require (editor != nullptr, "VU shipping editor opens");
            editor->setSize (600, 400); editor->setVisible (true);
            auto* view = component<hypha::observatory::View> (*editor);
            require (view != nullptr, "VU uses the shipping Observatory");
            require (static_cast<bool> (view->onVuLiveReading), "live provider is bound before the first timer");
            auto* vu = dynamic_cast<juce::Button*> (view->findChildWithID ("observatory-hybrid-vu"));
            require (vu != nullptr && static_cast<bool> (vu->onClick), "shipping VU entry exists");
            vu->onClick();
            require (processor.vuLive().enabled(), "the VU press enables its tap without a timer delay");
            view->setVuFrameTestMode (true);
            view->createComponentSnapshot (view->getLocalBounds()); view->tickVuForTest (1.0 / 60);
            require (view->vuPositionForTest (0) == 0.0, "opening does not start with a stale public reading");
            (editor.get()->*testMember (VuTimerCallback {}))();
            require (processor.vuLive().enabled() && static_cast<bool> (view->onVuLiveReading),
                     "shipping editor wires and enables the physical needle provider");
            juce::AudioBuffer<float> block (2, 512); juce::MidiBuffer midi;
            for (int iteration = 0; iteration < 160; ++iteration)
            {
                for (int ch = 0; ch < 2; ++ch) for (int frame = 0; frame < 512; ++frame)
                    block.setSample (ch, frame, ch == 0 ? .1f : -.05f);
                processor.processBlock (block, midi); clock.position += 512;
                advanceFrames (512);
                for (int ch = 0; ch < 2; ++ch) for (int frame = 0; frame < 512; ++frame)
                {
                    const float expected = ch == 0 ? .1f : -.05f, actual = block.getSample (ch, frame);
                    require (std::memcmp (&actual, &expected, sizeof (actual)) == 0, "VU tap leaves A bit identical");
                }
            }
            const auto live = view->onVuLiveReading();
            require (live.coherent && live.feeding && live.channels == 2
                     && live.needles[0].value > .15, "processBlock feeds the actual live provider");
            const auto firstFrameBegan = hypha::live_compare::callbackWallNanos();
            view->tickVuForTest (1.0 / 60);
            expectVuLiveFrame (*view, live, firstFrameBegan, hypha::live_compare::callbackWallNanos());
            KirinMeterSession meter {}; meter.state = KIRIN_METER_SESSION_ACTIVE;
            meter.generation = 1; meter.sample_rate = 48000; meter.active_frames = 96000;
            meter.channels = 2; meter.channel_vu_dbfs[0] = meter.channel_vu_dbfs[1] = -18;
            view->setMeterSnapshot (meter, true);
            view->createCaptureImage (600, 400, false, "fixed", "test");
            view->createCaptureImage (600, 400, false, "fixed", "test");
            const auto captureBefore = view->createCaptureImage (600, 400, false, "fixed", "test");
            view->setVuFrameTestMode (true);
            const auto beforePaint = view->vuPositionForTest (0);
            view->createComponentSnapshot (view->getLocalBounds());
            require (view->vuPositionForTest (0) == beforePaint, "paint cannot advance outside the frame's dirty regions");
            // Real processor -> published state -> shipping editor, with 60 Hz UI frames
            // interleaved among normal groups of small callbacks at their audio deadlines.
            juce::AudioBuffer<float> burst (2, 64);
            for (int ch = 0; ch < 2; ++ch) for (int frame = 0; frame < 64; ++frame)
                burst.setSample (ch, frame, ch == 0 ? .1f : -.05f);
            for (int group = 0; group < 6; ++group)
            {
                for (int callback = 0; callback < 32; ++callback)
                { processor.processBlock (burst, midi); clock.position += 64; }
                const auto groupReading = view->onVuLiveReading();
                const auto frameBegan = hypha::live_compare::callbackWallNanos();
                view->tickVuForTest (1.0 / 60);
                const auto frameEnded = hypha::live_compare::callbackWallNanos();
                for (int frame = 0; frame < 2; ++frame)
                {
                    advanceFrames (800); view->tickVuForTest (1.0 / 60);
                    expectVuLiveFrame (*view, groupReading, frameBegan, frameEnded, (frame + 1) / 60.0);
                }
                advanceFrames (448);
            }
            const auto burstEnd = view->onVuLiveReading();
            clock.playing = false; processor.processBlock (block, midi);
            require (! view->onVuLiveReading().feeding, "stopping a callback group ends the shipping input span");
            advanceFrames (7200);
            clock.playing = true;
            constexpr float resumedSample = .001f;
            for (int ch = 0; ch < 2; ++ch) for (int frame = 0; frame < 64; ++frame)
                burst.setSample (ch, frame, resumedSample);
            processor.processBlock (burst, midi); clock.position += 64;
            const auto resumed = view->onVuLiveReading();
            require (resumed.feeding && resumed.wallNanos > burstEnd.wallNanos,
                     "finite resumption enters the actual processor tap instead of a heartbeat-only callback");
            const auto coastSeconds = static_cast<double> (resumed.wallNanos - burstEnd.wallNanos) * 1.0e-9
                                    - burstEnd.aheadSeconds;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto expectedState = burstEnd.needles[(size_t) ch];
                hypha::hybrid_vu::NeedleStep::forSeconds (coastSeconds).apply (expectedState, 0.0);
                hypha::hybrid_vu::NeedleStep::forSeconds (64.0 / 48000).apply (
                    expectedState, static_cast<double> (resumedSample) * 1.5707963267948966);
                require (std::abs (resumed.needles[(size_t) ch].value - expectedState.value) < .0001,
                         "shipping stop/resume preserves the callback group's processed audio horizon");
            }
            view->tickVuForTest (1.0 / 60); advanceFrames (64);
            for (int frames : { 4096, 8192 })
                for (int stopKind : { 0, 1, 2 })
            {
                clock.hasPosition = stopKind != 1;
                juce::AudioBuffer<float> large (2, frames);
                for (int ch = 0; ch < 2; ++ch) for (int frame = 0; frame < frames; ++frame)
                    large.setSample (ch, frame, ch == 0 ? .1f : -.05f);
                processor.processBlock (large, midi); clock.position += frames;
                const auto largeEnd = view->onVuLiveReading();
                require (largeEnd.feeding && largeEnd.channels == 2, "shipping tap accepts large variable blocks");
                if (stopKind == 2)
                {
                    clock.playing = false; processor.processBlock (block, midi);
                    require (! view->onVuLiveReading().feeding, "large-block stop reaches the shipping interruption entry");
                }
                due = std::chrono::steady_clock::now();
                advanceFrames (static_cast<int> (std::ceil (largeEnd.aheadSeconds * 48000)) + 4800);
                view->tickVuForTest (1.0 / 60);
                clock.playing = true;
                if (stopKind == 2)
                {
                    burst.clear(); processor.processBlock (burst, midi); clock.position += 64;
                    require (! view->onVuLiveReading().feeding, "idle silence keeps the interrupted shipping needle returning");
                    for (int ch = 0; ch < 2; ++ch) for (int frame = 0; frame < 64; ++frame)
                        require (burst.getSample (ch, frame) == 0.0f, "short-stop VU recovery leaves A silent");
                }
                for (int ch = 0; ch < 2; ++ch) for (int frame = 0; frame < 64; ++frame)
                    burst.setSample (ch, frame, resumedSample);
                processor.processBlock (burst, midi); clock.position += 64;
                const auto resumedAfterSilence = view->onVuLiveReading();
                require (resumedAfterSilence.feeding, "the first small input automatically resumes the shipping tap");
                const auto elapsed = static_cast<double> (resumedAfterSilence.wallNanos - largeEnd.wallNanos) * 1.0e-9
                                   - largeEnd.aheadSeconds;
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto expectedState = largeEnd.needles[(size_t) ch];
                    hypha::hybrid_vu::NeedleStep::forSeconds (elapsed).apply (expectedState, 0.0);
                    hypha::hybrid_vu::NeedleStep::forSeconds (64.0 / 48000).apply (
                        expectedState, static_cast<double> (resumedSample) * 1.5707963267948966);
                    require (std::abs (resumedAfterSilence.needles[(size_t) ch].value - expectedState.value) < .00005
                             && std::abs (resumedAfterSilence.needles[(size_t) ch].velocity - expectedState.velocity) < .001,
                             "shipping large-block resume continues the UI return with or without stop and position notifications");
                    for (int frame = 0; frame < 64; ++frame)
                        require (burst.getSample (ch, frame) == resumedSample, "short-stop VU recovery leaves small A input unchanged");
                }
                const auto frameBegan = hypha::live_compare::callbackWallNanos();
                view->tickVuForTest (1.0 / 60);
                expectVuLiveFrame (*view, resumedAfterSilence, frameBegan, hypha::live_compare::callbackWallNanos());
                advanceFrames (64);
            }
            clock.hasPosition = true;
            for (int callback = 0; callback < 48; ++callback)
            {
                block.setSample (0, 0, std::numeric_limits<float>::quiet_NaN());
                processor.processBlock (block, midi); clock.position += 512;
                advanceFrames (512);
                if (callback % 3 != 0) view->tickVuForTest (1.0 / 60);
            }
            require (view->vuPositionForTest (0) < .02 && view->vuPositionForTest (1) < .02,
                     "repeated invalid audio returns both shipping needles");
            for (int callback = 0; callback < 160; ++callback)
            {
                block.setSample (0, 0, .1f);
                processor.processBlock (block, midi); clock.position += 512;
                advanceFrames (512);
            }
            const auto recovered = view->onVuLiveReading();
            require (recovered.feeding && recovered.needles[0].value > .15, "finite audio automatically resumes the shipping needle");
            const auto recoveryBegan = hypha::live_compare::callbackWallNanos();
            view->tickVuForTest (1.0 / 60);
            expectVuLiveFrame (*view, recovered, recoveryBegan, hypha::live_compare::callbackWallNanos());
            clock.playing = false; processor.processBlock (block, midi);
            require (! view->onVuLiveReading().feeding, "unmeasured stop publishes the coast state");
            view->createComponentSnapshot (view->getLocalBounds()); view->tickVuForTest (1.0 / 60);
            for (int frame = 0; frame < 18; ++frame)
            { view->createComponentSnapshot (view->getLocalBounds()); view->tickVuForTest (1.0 / 60); }
            require (view->vuPositionForTest (0) < .02, "stale public readings cannot pin the stopped needle");
            advanceFrames (48000);
            vu->onClick(); vu->onClick();
            require (view->vuPositionForTest (0) < .02, "stopped VU reopening cannot revive old audio on first paint");
            view->tickVuForTest (1.0 / 60);
            require (view->vuPositionForTest (0) < .02, "stopped VU reopening keeps the returned needle");
            const auto captureAfter = view->createCaptureImage (600, 400, false, "fixed", "test");
            require (differentPixels (captureBefore, captureAfter, captureBefore.getBounds()) == 0,
                     "needle-only motion and stop leave snapshot Capture bit identical");
            processor.editorBeingDeleted (editor.get()); editor.reset();
            editor.reset (dynamic_cast<KirinHyphaEditor*> (processor.createEditorIfNeeded()));
            require (editor != nullptr, "stopped shipping editor reopens");
            view = component<hypha::observatory::View> (*editor);
            view->setManualHybridVuVisible (true); view->setVuFrameTestMode (true);
            require (view->vuPositionForTest (0) < .02, "new editor uses the original publication time");
            processor.setPlayHead (nullptr);
            processor.editorBeingDeleted (editor.get()); editor.reset(); processor.releaseResources();
        }
    std::cout << "VU audio/editor entry: PRE/POST, AU/VST3/AAX simulated wrappers, transparency and Capture PASS\n";
}
