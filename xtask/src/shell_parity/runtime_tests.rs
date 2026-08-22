    #[test]
    fn juce_offline_lifecycle_does_not_stop_record() {
        let prepare = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::prepareToPlay",
            "void KirinHyphaProcessorBase::releaseResources()",
        );
        assert!(
            !prepare.contains("maybeAutoStopOnOfflineEnd")
                && !prepare.contains("offlineAutoStop")
                && !prepare.contains("kirin_hypha_stop"),
            "prepareToPlay must not translate Offline->Realtime lifecycle edges into Stop"
        );
        assert!(
            !PLUGIN_PROCESSOR_CPP.contains("KIRIN_HYPHA_OFFLINE_AUTOSTOP"),
            "env-gated offline auto-stop must not remain as a hidden third Stop path"
        );
        assert!(
            !PLUGIN_PROCESSOR_CPP.contains("offlineRenderedSamples")
                && !PLUGIN_PROCESSOR_CPP.contains("prevNonRealtime")
                && !PLUGIN_PROCESSOR_CPP.contains("offlineJustEnded"),
            "offline-end state must not remain as latent Stop machinery"
        );
        assert_eq!(
            count_occurrences(
                PLUGIN_PROCESSOR_CPP,
                "void KirinHyphaProcessorBase::stopPair()"
            ),
            1,
            "POST Stop authority must stay isolated in the explicit stopPair entry point"
        );
        assert_eq!(
            count_occurrences(PLUGIN_PROCESSOR_CPP, "kirin_hypha_stop (hyphaHandle);"),
            1,
            "kirin_hypha_stop must only be called by the explicit stopPair entry point"
        );
        assert!(
            PLUGIN_EDITOR_CPP
                .contains("postControls->onStop = [this] { processorRef.stopPair(); };"),
            "manual POST Stop must remain the UI path into stopPair"
        );
        assert_eq!(
            count_occurrences(PLUGIN_EDITOR_CPP, "processorRef.stopPair();"),
            1,
            "no non-manual JUCE editor path should call stopPair"
        );
    }

    #[test]
    fn juce_record_silent_offline_buffers_are_pushed_for_record_timeline() {
        let body = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::processBlock",
            "bool KirinHyphaProcessorBase::bufferIsSilent",
        );

        assert!(
            body.contains("shouldCaptureBufferForMeasurement"),
            "processBlock must separate display signal state from Record capture eligibility"
        );
        assert!(
            body.contains("const bool nonRealtimeMode = isNonRealtime();"),
            "processBlock must still observe the host non-realtime/offline mode"
        );
        assert!(
            body.contains("shouldCaptureBufferForMeasurement (stateCode")
                && body.contains("positionChanged,\n                                                                  nonRealtimeMode"),
            "offline mode must still feed Record capture eligibility"
        );
        assert!(
            body.contains("kirin_hypha_note_record_window")
                && body.contains("clockStartSamples")
                && body.contains("hasClockEnd")
                && body.contains("clockEndSamples"),
            "offline mode must still be reported to the Record clock"
        );
        assert!(
            body.contains("recordStartWindowLatched")
                && body.contains("recordStartCandidateWindow")
                && body.contains("renderedRecordWindow")
                && body.contains("const bool pushBuffer = recording ? renderedRecordWindow : captureBuffer"),
            "JUCE Record must not push or render idle pre-start windows before the first valid Record window"
        );
        assert!(
            body.contains("recording,\n                                    renderedRecordWindow,"),
            "JUCE rendered flag passed to FFI must be the strict Record render window, not the broad Watch capture flag"
        );
        assert!(
            !body.contains("getLoopPoints") && !body.contains("ppqStart") && !body.contains("ppqEnd"),
            "JUCE PPQ loop points are not exact WAV/export sample bounds and must not become wav_clock_native"
        );
        assert!(
            body.contains("if (pushBuffer)"),
            "Record silent/offline buffers must be gated through the strict Record push window"
        );
    }

    #[test]
    fn au_transport_omission_uses_only_valid_render_clock_without_arming_on_idle() {
        assert!(JUCE_PLUGIN_CONFIG.contains("KIRIN_HYPHA_AU_CLOCK_PROVENANCE 1"));
        let Some(juce_au_wrapper) = read_juce_au_wrapper() else {
            return;
        };

        assert!(juce_au_wrapper
            .contains("info.setKirinAuUsesHostTransportTimeline (transportTimelineValid);"));
        assert!(juce_au_wrapper.contains("kAudioTimeStampSampleTimeValid"));
        assert!(juce_au_wrapper.contains("sampleTimeIsRepresentable"));
        assert!(juce_au_wrapper.contains("std::numeric_limits<int64_t>::min()"));
        assert!(juce_au_wrapper.contains("std::numeric_limits<int64_t>::max()"));
        assert!(juce_au_wrapper
            .contains("sampleTimeIsRepresentable (audioUnit.lastTimeStamp.mSampleTime)"));
        assert!(juce_au_wrapper.contains(
            "else if (renderTimelineValid)\n                setTimeInSamples (audioUnit.lastTimeStamp.mSampleTime);"
        ));

        let body = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::processBlock",
            "bool KirinHyphaProcessorBase::bufferIsSilent",
        );
        assert!(body.contains(
            "wrapperType == juce::AudioProcessor::wrapperType_AudioUnit\n                    && hypha::clock_source_contract::audioUnitV2UsesRenderTimeline ("
        ));
        assert!(!body.contains("#if JucePlugin_Build_AU && KIRIN_HYPHA_AU_CLOCK_PROVENANCE"));
        assert!(body.contains("KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE"));
        assert!(body.contains(
            "const bool measurementTimelineActive = playing\n                                        || clockSource == KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE;"
        ));
        assert!(body
            .contains("&& (stateCode == 1 || playing || nonRealtimeMode || hasClockEnd);"));
        assert!(!body.contains("&& (stateCode == 1 || measurementTimelineActive"));
        assert!(body.contains("windowPositionSamples, windowNumFrames, clockSource"));
        assert!(body
            .contains("kirin_hypha_note_transport_block (hyphaHandle, measurementTimelineActive"));
        assert!(PLUGIN_PROCESSOR_CPP
            .contains("lastMeasurementTimelineActive.load (std::memory_order_acquire)"));
    }

    #[test]
    fn juce_prealloc_absorbs_large_offline_blocks_without_audio_thread_realloc() {
        assert!(
            PLUGIN_PROCESSOR_CPP.contains("constexpr int kOversizeHeadroomFrames = 262144;"),
            "JUCE shell should absorb large offline-render blocks before falling back to oversized_drop"
        );

        let prepare = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::prepareToPlay",
            "void KirinHyphaProcessorBase::releaseResources()",
        );
        assert!(
            prepare.contains("interleaveScratch.assign")
                && prepare.contains("kOversizeHeadroomFrames")
                && prepare.contains("scratchCapacitySamples = interleaveScratch.size();"),
            "offline block scratch must be allocated once in prepareToPlay, not on the audio thread"
        );

        let process = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::processBlock",
            "bool KirinHyphaProcessorBase::bufferIsSilent",
        );
        assert!(
            process.contains("needed <= scratchCapacitySamples")
                && process.contains("kirin_hypha_note_oversized_drop")
                && !process.contains("interleaveScratch.assign")
                && !process.contains("interleaveScratch.resize"),
            "processBlock must measure within prealloc capacity and report, not allocate, beyond it"
        );
    }

    #[test]
    fn juce_offline_non_silent_buffers_are_active_before_record_edge() {
        let body = between(
            PLUGIN_PROCESSOR_CPP,
            "uint8_t resolveSignalStateCode",
            "bool shouldCaptureBufferForMeasurement",
        );

        assert!(
            body.contains("bool nonRealtime"),
            "JUCE signal-state resolver must receive the host non-realtime flag"
        );
        assert!(
            body.contains("if (! silent && (recording || playing || nonRealtime))"),
            "non-silent offline bounce buffers must be Active even before PRE observes the Record edge"
        );
    }

    #[test]
    fn juce_watch_silence_continuity_is_isolated_from_record_and_transport_stop() {
        let body = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::processBlock",
            "bool KirinHyphaProcessorBase::bufferIsSilent",
        );

        assert!(body.contains(
            "WatchSilenceGate::eligible (\n            bypassed, recording, measurementTimelineActive)"
        ));
        assert!(body.contains("WatchSilenceGate::sampleTimelineStartsNewPass ("));
        assert!(body.contains("kirin_hypha_get_signal_state (hyphaHandle)"));
        assert!(body.contains("availabilityStartsNewPass ("));
        assert!(body.contains(
            "watchSampleTimelineStartedNewPass || watchAvailabilityBoundary,"
        ));
        assert!(!body.contains("steady_clock::now"));
        assert!(body.contains(
            "const bool stateSilent = silent && ! watchActiveThroughSilence;"
        ));
        assert!(body.contains(
            "resolveSignalStateCode (bypassed, measurementTimelineActive,\n                                                      stateSilent, recording, nonRealtimeMode)"
        ));
        assert!(body.contains(
            "const bool pushBuffer = recording ? renderedRecordWindow : captureBuffer;"
        ));
    }

    #[test]
    fn juce_post_record_display_keeps_six_metrics_before_signal_fallback() {
        let start = PLUGIN_EDITOR_CPP
            .find("void KirinHyphaEditor::updatePost()")
            .expect("updatePost");
        let body = &PLUGIN_EDITOR_CPP[start..];
        let record_branch = body.find("if (displayRecord)").expect("record branch");
        let signal_branch = body
            .find("else if (sig != KIRIN_SIGNAL_STATE_ACTIVE)")
            .expect("signal fallback");

        assert!(
            record_branch < signal_branch,
            "POST Record must keep its six fixed cells visible through finalize and result hold"
        );
        assert!(
            body.contains("cachedRecordDisplay.has_delta != 0")
                && body.contains("d = cachedRecordDisplay.delta")
                && body.contains("display::recordPairContext (")
                && body.contains("cachedRecordDisplay.pair_matches_current != 0")
                && body.contains("display::recordMetricMode (recordPairSelected, haveD, d.mode)"),
            "POST Record must bind its held delta and final I to one display generation and PRE"
        );
        assert!(
            body.contains("summary.max_true_peak")
                && body.contains("summary.lufs_i")
                && body.contains("recordPhase == KIRIN_RECORD_DISPLAY_LIVE"),
            "POST Record must show absolute Max TP/I and bypass the timed Watch hold after finalize"
        );
        assert!(HYPHA_DISPLAY_CONTRACT_H
            .contains("return pairSelected ? MetricMode::delta : MetricMode::absolute;"));
    }

    #[test]
    fn juce_post_watch_keeps_delta_grid_for_paired_stale_delta() {
        let start = PLUGIN_EDITOR_CPP
            .find("void KirinHyphaEditor::updatePost()")
            .expect("updatePost");
        let body = &PLUGIN_EDITOR_CPP[start..];
        let watch_branch = body.find("else // Active + Watch").expect("watch branch");
        let window = &body[watch_branch..body.len().min(watch_branch + 3600)];

        assert!(
            window.contains("display::watchMetricMode (pairSelected, haveRawD, rawD.mode)")
                && window.contains("configureForKind (Kind::WatchDelta6)")
                && window.contains("display::deltaIsActive (d.mode)"),
            "JUCE POST Watch must keep the Delta+MAX grid while an explicit pair is selected"
        );
        assert!(
            window.contains("else if (! preUnavailable && pairSelected)")
                && window.contains("displaySmoother.heldDeltaDisplay (held, t)")
                && window.contains("const juce::Colour base = liveDelta ? COL_NORMAL : COL_MUTED;"),
            "JUCE POST Watch must use held/muted delta values for transient stale PRE reads"
        );
        assert!(
            PLUGIN_EDITOR_CPP.contains(
                "if (pairSelected)\n        {\n            if (currentKind != Kind::WatchDelta6)"
            ) && PLUGIN_EDITOR_CPP.contains("const bool unavailable = ! haveHeldD;"),
            "JUCE POST Watch must keep the paired delta grid even when PRE measurements are unavailable"
        );
        assert_eq!(
            count_occurrences(
                HYPHA_DISPLAY_CONTRACT_H,
                "return pairSelected ? MetricMode::delta : MetricMode::absolute;"
            ),
            2,
            "Watch and Record layouts must both be owned only by pair context"
        );
        assert!(
            window.contains("selectedMeasure (watchMaximum)")
                && window.contains("watchMaximum.true_peak")
                && window.contains("watchMaximum.crest"),
            "JUCE POST Watch must expose selected M/S, TP, and Crest absolute MAX values in both paired and unpaired layouts"
        );
    }

    #[test]
    fn juce_post_led_follows_display_mute_boundary() {
        let start = PLUGIN_EDITOR_CPP
            .find("void KirinHyphaEditor::updatePost()")
            .expect("updatePost");
        let body = &PLUGIN_EDITOR_CPP[start..];

        assert!(
            body.contains("bool watchHeldNormal = false;")
                && body.contains("watchHeldNormal = haveHeldD && ! mutedHeldD;")
                && body.contains("watchHeldNormal = haveM && ! mutedM;")
                && body.contains(
                    "const int ledSig = (! displayRecord && sig == KIRIN_SIGNAL_STATE_INACTIVE && watchHeldNormal)"
                )
                && body.contains("? KIRIN_SIGNAL_STATE_ACTIVE : sig;"),
            "JUCE POST Watch LED must remain active only while the displayed held values are not muted"
        );
    }
