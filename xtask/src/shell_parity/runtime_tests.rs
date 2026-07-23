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
            body.contains("const bool nonRealtime = isNonRealtime();"),
            "processBlock must still observe the host non-realtime/offline mode"
        );
        assert!(
            body.contains("shouldCaptureBufferForMeasurement (stateCode")
                && body.contains("positionChanged,\n                                                                  nonRealtime"),
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
        assert!(body.contains("KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE"));
        assert!(body.contains(
            "const bool measurementTimelineActive = playing\n                                        || clockSource == KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE;"
        ));
        assert!(body.contains("&& (stateCode == 1 || playing || nonRealtime || hasClockEnd);"));
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
    fn juce_post_record_display_keeps_six_metrics_before_signal_fallback() {
        let start = PLUGIN_EDITOR_CPP
            .find("void KirinHyphaEditor::updatePost()")
            .expect("updatePost");
        let body = &PLUGIN_EDITOR_CPP[start..];
        let record_branch = body.find("if (rec)").expect("record branch");
        let signal_branch = body
            .find("else if (sig != KIRIN_SIGNAL_STATE_ACTIVE)")
            .expect("signal fallback");

        assert!(
            record_branch < signal_branch,
            "POST Record must keep Delta6/N/Sharp visible even if the host goes inactive during bounce"
        );
        assert!(
            body.contains("if (sig == KIRIN_SIGNAL_STATE_ACTIVE && processorRef.pollDelta (rawD))",)
                && body.contains("display::deltaIsActive (rawD.mode)")
                && body.contains("d = displaySmoother.smoothDelta (rawD, t);"),
            "POST Record active display must pass delta values through the GUI-only smoother"
        );
        assert!(
            body.contains("else if (sig == KIRIN_SIGNAL_STATE_INACTIVE)")
                && body.contains("displaySmoother.heldDeltaDisplay (held, t)")
                && body.contains("display::recordMetricMode (haveD, d.mode)")
                && body.contains("display::deltaIsStale (d.mode)"),
            "POST Record inactive display must keep recent delta values until the display mute boundary instead of immediately falling to ---"
        );
        assert!(HYPHA_DISPLAY_CONTRACT_H.contains("return preUnavailableForDelta (mode)"));
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
            window.contains("else // pair empty or paired PRE bypassed/inactive -> POST absolute"),
            "JUCE POST Watch may fall back to POST absolute values only when the pair is empty or PRE is explicitly OFF"
        );
        assert!(HYPHA_DISPLAY_CONTRACT_H.contains("if (! pairSelected)"));
        assert!(HYPHA_DISPLAY_CONTRACT_H.contains("if (preUnavailableForDelta (mode))"));
        assert!(
            window.contains("watchMaximum.lufs_m")
                && window.contains("watchMaximum.true_peak")
                && window.contains("watchMaximum.crest"),
            "JUCE POST Watch must expose all three absolute MAX values in both paired and unpaired layouts"
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
                && body.contains("watchHeldNormal = ! mutedHeldD;")
                && body.contains("watchHeldNormal = haveM && ! mutedM;")
                && body.contains(
                    "const int ledSig = (! rec && sig == KIRIN_SIGNAL_STATE_INACTIVE && watchHeldNormal)"
                )
                && body.contains("? KIRIN_SIGNAL_STATE_ACTIVE : sig;"),
            "JUCE POST Watch LED must remain active only while the displayed held values are not muted"
        );
    }
