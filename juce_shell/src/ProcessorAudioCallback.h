// Included once by PluginProcessor.cpp: the common float input transaction.
void KirinHyphaProcessorBase::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh     = getTotalNumInputChannels();
    const int numOut    = getTotalNumOutputChannels();
    const int numFrames = buffer.getNumSamples();

    // R-12 A path: read-only passthrough. Explicit POST Reference B, if selected, is rendered
    // only after the canonical A measurement transaction at the end of this callback.
    for (int ch = numCh; ch < numOut; ++ch)
        buffer.clear (ch, 0, numFrames);

    // Not locked on the audio thread: JUCE suspends processing around prepare/release,
    // so hyphaHandle is stable here. (The editor poll and create/destroy use handleLock.)
    if (hyphaHandle == nullptr)
        return;

    // Enable writes once, deferred to the message-thread Timer so any
    // setStateInformation (identity / pair name restore) can win before the io_thread snapshots
    // path identity. The audio thread does ONLY lock-free atomics (no alloc/lock/syscall).
    if (! writesEnabled.load (std::memory_order_acquire))
    {
        if (stateInformationSeen.load (std::memory_order_acquire))
            enableDelayTicks.store (0, std::memory_order_release);
        enablePending.store (true, std::memory_order_release);
    }
    // --- Signal state derivation (parity: hypha_pre.rs:397-403) -------------------
    const bool bypassed = (bypassParam != nullptr && bypassParam->get());
    const auto processClock = readHostProcessClock();
    const auto [playing, hasPosition, clockSource, positionSamples, hasClockEnd, clockStartSamples, clockEndSamples,
          presentationSource, inputPresentationValid, inputPresentationSamples, outputPresentationValid,
          outputPresentationSamples, looping, auxiliaryClock, callbackNanos] = processClock;
    juce::ignoreUnused (looping, callbackNanos); // Used by the comparison paths below.
    lastPlaying.store (playing, std::memory_order_release); // POST pair lock reads this
    if (role == Role::Post && candidateMeasurementIsolated())
    {
        // A/B changes the upstream B input. No normal history, Record, Keep, capture or
        // Reference observer may consume it, including fallback and the downstream return.
        kirin_hypha_set_signal_state (hyphaHandle, KIRIN_SIGNAL_STATE_INACTIVE);
        feedNoMeasurement (static_cast<std::uint32_t> (numCh));
        hypha::chain_trace::Scope trace (liveCompare, buffer.getArrayOfReadPointers(), numCh,
            numFrames, preparedFormat.sampleRate, true, static_cast<int> (wrapperType), processClock,
            bypassed, isNonRealtime(), isUsingDoublePrecision() ? 64 : 32);
        processLiveCompare (buffer, processClock, bypassed, isNonRealtime(), false);
        pushPairedAnalysis (0, static_cast<std::uint32_t> (numCh), 0);
        return;
    }
#if KIRIN_HYPHA_GUIDE_TRANSPORT
    preDisplayClock.publish (positionSamples, preparedFormat.sampleRate,
                             static_cast<std::uint32_t> (juce::jmax (0, numFrames)), playing,
                             static_cast<hypha::pre_display::ClockSource> (clockSource));
#endif
    const bool positionChanged = hasPosition && lastProcessPositionValid
                              && positionSamples != lastProcessPositionSamples;
    const bool watchSampleTimelineStartedNewPass =
        hypha::signal_state_contract::WatchSilenceGate::sampleTimelineStartsNewPass (
            hasPosition,
            lastProcessPositionValid && lastProcessHadPosition,
            positionSamples,
            lastProcessPositionSamples,
            lastProcessNumFrames);
    if (hasPosition)
    {
        lastProcessPositionValid = true;
        lastProcessPositionSamples = positionSamples;
        lastProcessNumFrames = (uint64_t) juce::jmax (0, numFrames);
    }
    lastProcessHadPosition = hasPosition;
    const bool silent = bufferIsSilent (buffer);
    const bool recording = kirin_hypha_is_recording (hyphaHandle);
    if (! recording)
    {
        recordStartWindowLatched = false;
        recordNativeRangeLatched = false;
    }
    const bool nonRealtimeMode = isNonRealtime();
    // Some AU hosts omit the optional transport callback. A valid AudioUnit render timestamp is
    // still an exact processing timeline, so non-silent Watch audio and an already-started Record
    // can advance on it. Clock availability alone is not transport authority: otherwise silent
    // idle callbacks between Keep and Drop would be captured as Record pre-roll.
    const bool measurementTimelineActive = playing
                                        || clockSource == KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE;
    lastMeasurementTimelineActive.store (measurementTimelineActive, std::memory_order_release);
    const uint8_t previousSignalState = kirin_hypha_get_signal_state (hyphaHandle);
    const bool watchAvailabilityBoundary = ! recording
                                        && ! bypassed
                                        && measurementTimelineActive
                                        && previousSignalState != KIRIN_SIGNAL_STATE_ACTIVE;
    // A single silent callback used to collapse PRE to Inactive, clear its measurement engine,
    // blank the live analysis views, and drop the paired POST delta for one or more UI ticks. Keep
    // Watch and the displayed live input through musical rests shorter than the exact 3 s LUFS-S
    // window. Transport stop, bypass, and every Record/TRACE path keep their existing state rules.
    const bool watchActiveThroughSilence = watchSilenceGate.observeBlock (
        hypha::signal_state_contract::WatchSilenceGate::eligible (
            bypassed, recording, measurementTimelineActive),
        watchSampleTimelineStartedNewPass || watchAvailabilityBoundary,
        silent,
        (uint64_t) juce::jmax (0, numFrames),
        preparedFormat.sampleRate);
    const bool stateSilent = silent && ! watchActiveThroughSilence;
    liveInputPresent.store (! stateSilent, std::memory_order_relaxed); // same rests as Watch
    int windowStartFrame = 0;
    int windowEndFrame = numFrames;
    int64_t windowPositionSamples = positionSamples;
    uint64_t windowNumFrames = (uint64_t) numFrames;
    if (recording && hasPosition)
    {
        const int64_t blockStart = positionSamples;
        const int64_t blockEnd = positionSamples + (int64_t) numFrames;
        const int64_t clockStart = hasClockEnd ? clockStartSamples : 0;
        const int64_t clippedStart = std::max (blockStart, clockStart);
        const int64_t clippedEnd = hasClockEnd ? std::min (blockEnd, clockEndSamples) : blockEnd;
        if (clippedEnd <= clippedStart)
        {
            windowStartFrame = 0;
            windowEndFrame = 0;
            windowPositionSamples = clippedStart;
            windowNumFrames = 0;
        }
        else
        {
            windowStartFrame = (int) (clippedStart - blockStart);
            windowEndFrame = (int) (clippedEnd - blockStart);
            windowPositionSamples = clippedStart;
            windowNumFrames = (uint64_t) (clippedEnd - clippedStart);
        }
    }

    // C ABI signal-state codes: 0 = Inactive, 1 = Active, 2 = Bypassed.
    const uint8_t stateCode = resolveSignalStateCode (bypassed, measurementTimelineActive,
                                                      stateSilent, recording, nonRealtimeMode);
    kirin_hypha_set_signal_state (hyphaHandle, stateCode);
    const bool watchAvailabilityStartedNewPass =
        hypha::signal_state_contract::availabilityStartsNewPass (
            previousSignalState, stateCode, recording);
    kirin_hypha_note_transport_block (hyphaHandle, measurementTimelineActive, hasPosition,
                                      positionSamples, (uint64_t) numFrames,
                                      watchAvailabilityStartedNewPass);
    // editor は signalStateLive()（FFI 直読 / heartbeat-aware）で表示分岐する。

    // --- Feed the engine ---------------------------------------------------------
    // Watch meters push when Active. Record only enters after a real rendered/replayed
    // window has established the native sample start; later silent position-advanced
    // windows may continue the already-started Record timeline.
    const bool captureBuffer = shouldCaptureBufferForMeasurement (stateCode,
                                                                  bypassed,
                                                                  recording,
                                                                  measurementTimelineActive,
                                                                  positionChanged,
                                                                  nonRealtimeMode);
    const bool recordStartCandidateWindow = captureBuffer
                                         && recording
                                         && hasPosition
                                         && windowNumFrames > 0
                                         && numCh > 0
                                         && (stateCode == 1 || playing || nonRealtimeMode || hasClockEnd);
    const bool renderedRecordWindow = recording
                                   && hasPosition
                                   && windowNumFrames > 0
                                   && numCh > 0
                                   && captureBuffer
                                   && (recordStartWindowLatched || recordStartCandidateWindow);
    // An explicit native WAV range is a producer TakeStart edge. Offline entry is intentionally
    // not derived from Record state here: the FFI observes the first fully-admitted offline Watch
    // callback, before Keep ACK, and preserves its raw pre-roll for WAV sample 0. Forcing the first
    // Record callback would discard that exact prefix and recreate the 7,676-sample skew class.
    const bool forceTakeStartEpoch = renderedRecordWindow
                                  && hasClockEnd
                                  && ! recordNativeRangeLatched;
    // While a format change is held, the engine is still built for the old one. Audio keeps
    // passing through untouched (R-12), but it is not fed to a meter that would measure it as
    // something it is not. The keepalive below still advances the heartbeat.
    const bool formatHeld = formatChangeHeld.load (std::memory_order_acquire);
    const bool pushBuffer = ! formatHeld && (recording ? renderedRecordWindow : captureBuffer);
    kirin_hypha_note_record_window (hyphaHandle,
                                    recording,
                                    renderedRecordWindow,
                                    playing,
                                    nonRealtimeMode,
                                    hasPosition,
                                    windowPositionSamples,
                                    windowNumFrames,
                                    clockStartSamples,
                                    hasClockEnd,
                                    clockEndSamples);
    // push_samples advances heartbeat internally, so a 0-frame call is a heartbeat-only
    // keepalive for the non-captured Inactive / Bypassed case.
    uint64_t optionalInputFrames = 0;
    if (pushBuffer)
    {
        const bool blockAccepted = feedMeasurementInput (buffer, processClock, windowStartFrame,
            windowEndFrame, windowPositionSamples, windowNumFrames, forceTakeStartEpoch);
        if (blockAccepted)
        {
            optionalInputFrames = windowNumFrames;
            // Producer take boundaries are commit facts, not callback intentions. If the
            // complete audio+clock transaction is rejected, the next eligible callback must
            // still carry the same first-Record/native-range edge.
            if (renderedRecordWindow)
            {
                recordStartWindowLatched = true;
                if (hasClockEnd)
                    recordNativeRangeLatched = true;
            }
        }
    }
    else
    {
        feedNoMeasurement ((uint32_t) numCh);
    }

    processComparisonPaths (buffer, processClock, measurementTimelineActive, bypassed, nonRealtimeMode);
    pushPairedAnalysis (optionalInputFrames, (uint32_t) numCh, windowStartFrame);
}
