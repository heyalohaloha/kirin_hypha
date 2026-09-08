#[path = "support/juce_lifecycle_sources.rs"]
mod sources;
use sources::read_repo;
#[path = "support/juce_presentation_contract.rs"]
mod presentation;

#[test]
fn direct_keep_feedback_is_a_consumable_edge_not_a_persistent_error() {
    let header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    assert!(header.contains("kirin_hypha_drain_keep_action_notice"));

    let ffi = read_repo("crates/kirin_hypha_ffi/src/lib.rs");
    assert!(ffi.contains("keep_action_notice: Arc<RwLock<Option<String>>>"));
    assert!(ffi.contains("pub fn drain_keep_action_notice(&self) -> Option<String>"));
    assert!(ffi.contains("and_then(|mut notice| notice.take())"));
    assert!(ffi.contains("*message = Some(\"Another Keep is active\".to_string())"));

    let processor = read_repo("juce_shell/src/PluginProcessor.cpp")
        + &read_repo("juce_shell/src/PluginProcessorDisplayState.cpp");
    assert!(processor.contains("kirin_hypha_drain_keep_action_notice"));
    let editor = read_repo("juce_shell/src/PluginEditor.cpp")
        + &read_repo("juce_shell/src/PluginEditorAnalysis.cpp");
    assert!(editor.contains("processorRef.drainKeepActionNotice()"));
    assert!(editor.contains("toastUntil = t + 3.0"));
}

#[test]
fn juce_commits_take_start_only_after_whole_block_admission() {
    let header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    assert!(header.contains("bool kirin_hypha_push_samples("));

    let ffi = read_repo("crates/kirin_hypha_ffi/src/lib.rs");
    let push = slice_between(
        &ffi,
        "pub unsafe extern \"C\" fn kirin_hypha_push_samples",
        "/// B-125: prealloc-max",
    );
    assert!(push.contains(") -> bool"));
    assert!(push.contains("engine.push_samples_transaction(slice, num_channels)"));
    assert!(push.contains(".unwrap_or(false)"));

    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");
    let push_index = processor
        .find("const bool blockAccepted = kirin_hypha_push_samples")
        .expect("JUCE must observe whole-block admission");
    let latch_index = processor[push_index..]
        .find("if (blockAccepted && renderedRecordWindow)")
        .map(|offset| push_index + offset)
        .expect("take-start latch must be conditional on admission");
    assert!(push_index < latch_index);
}

#[test]
fn paired_pre_off_is_absolute_while_inactive_and_stale_preserve_delta_layout() {
    let ffi_header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    for required in [
        "KIRIN_DELTA_MODE_ACTIVE 0u",
        "KIRIN_DELTA_MODE_BYPASSED 3u",
        "KIRIN_DELTA_MODE_PRE_INACTIVE 4u",
        "KIRIN_PAIR_STATUS_PAIRED 2u",
        "KIRIN_SIGNAL_STATE_ACTIVE 1u",
    ] {
        assert!(
            ffi_header.contains(required),
            "ABI contract missing {required}"
        );
    }

    let editor = read_repo("juce_shell/src/PluginEditor.cpp");
    let display_contract = read_repo("juce_shell/src/HyphaDisplayContract.h");
    assert!(display_contract.contains("mode == KIRIN_DELTA_MODE_BYPASSED"));
    assert!(display_contract.contains("mode == KIRIN_DELTA_MODE_PRE_INACTIVE"));
    assert!(display_contract.contains("mode == KIRIN_DELTA_MODE_ACTIVE"));
    assert!(display_contract.contains("pairedPreIsExplicitlyBypassed"));
    assert!(editor.contains("display::preUnavailableForDelta (rawD.mode)"));
    assert!(editor.contains("display::recordPairContext ("));
    assert!(editor.contains("cachedRecordDisplay.pair_matches_current != 0"));
    assert!(editor.contains("display::recordMetricMode (recordPairSelected, haveD, d.mode)"));
    assert!(
        editor.contains("display::watchMetricMode (pairSelected, effectiveHaveD, effectiveMode)")
    );
    assert!(!editor.contains("rawD.mode == 0"));
    assert!(editor.contains("Kind::Abs6"));
    assert!(editor.contains("const bool unavailable = ! haveHeldD;"));
    assert!(editor
        .contains("else // no selected pair, or paired PRE explicitly bypassed -> POST absolute"));
    assert!(display_contract.contains("mode == KIRIN_DELTA_MODE_BYPASSED"));
    assert!(editor.contains("Paired PRE is off. Showing POST absolute values."));
    assert!(editor.contains("COL_SPECTRUM_POST"));
    let producer = read_repo("crates/kirin_measure/src/io_thread_post_tick.rs")
        + &read_repo("crates/kirin_measure/src/io_thread_post_delta.rs");
    assert!(producer.contains("mode: DeltaMode::PreInactive"));
    assert!(producer.contains("Some(SignalState::Inactive) => DeltaMode::PreInactive"));
    assert!(producer.contains("POST absolute until it resumes"));
    assert!(!producer.contains("idle はラッチ維持で Stale"));
}
#[test]
fn loudness_view_and_integrated_result_are_additive_display_only_state() {
    let header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    assert!(header.contains("double lufs_s"));
    assert!(header.contains("KirinRecordDisplay"));
    assert!(header.contains("KIRIN_RECORD_DISPLAY_RESULT_HOLD 3u"));

    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");
    assert!(processor.contains("xml.setAttribute (\"loudness_view\""));
    assert!(processor.contains("getStringAttribute (\"loudness_view\") == \"S\""));
    assert!(processor.contains("xml.setAttribute (\"display_state_version\", 4)"));
    assert!(processor.contains("meter_context"));
    assert!(processor.contains("scale_mode"));
    assert!(processor.contains("observatory_width"));
    assert!(processor.contains("observatory_height"));
    assert!(processor.contains("withNonParameterStateChanged (true)"));

    let contract = read_repo("juce_shell/src/HyphaUiContract.h");
    assert!(contract.contains("Metric::maxTruePeak"));
    assert!(contract.contains("Metric::integrated"));
    assert!(!contract.contains("Metric::loudness"));

    let editor = read_repo("juce_shell/src/PluginEditor.cpp");
    assert!(editor.contains("return useShortTerm ? value.lufs_s : value.lufs_m"));
    assert!(editor.contains("summary.max_true_peak"));
    assert!(editor.contains("summary.lufs_i"));
    assert!(!editor.contains("fillAbs (3, V (m.n_prime_total)"));

    let pre_json = read_repo("crates/kirin_measure/src/io_thread_pre.rs");
    let post_json = read_repo("crates/kirin_measure/src/io_thread_post_json.rs");
    assert!(pre_json.contains("\"lufs_s\""));
    assert!(post_json.contains("\"lufs_s\""));
    let plugin_data = read_repo("crates/kirin_measure/src/plugin_data.rs");
    assert!(
        !plugin_data.contains("\"lufs_s\""),
        "plugin_data/.kirin schema must remain unchanged"
    );
}

#[test]
fn saved_daw_state_restores_the_exact_pre_without_registry_rescan() {
    let ffi_header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    assert!(ffi_header.contains("kirin_hypha_restore_pair_candidate"));
    assert!(ffi_header.contains("kirin_hypha_get_paired_pre_locator"));

    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");
    assert!(
        processor.contains("xml.setAttribute (\"paired_pre_instance_id\", persistPairInstanceId)")
    );
    assert!(processor
        .contains("restoredPairInstanceId = xml->getStringAttribute (\"paired_pre_instance_id\")"));
    assert!(processor
        .contains("xml.setAttribute (\"paired_pre_project_hash\", persistPairProjectHash)"));
    assert!(processor.contains(
        "restoredPairProjectHash = xml->getStringAttribute (\"paired_pre_project_hash\")"
    ));
    assert!(processor.contains("pairedPreLocator (livePairProjectHash, livePairInstanceId)"));
    assert!(processor.contains("kirin_hypha_restore_pair_candidate ("));

    let ffi = read_repo("crates/kirin_hypha_ffi/src/lib.rs");
    let pair_snapshot = read_repo("crates/kirin_hypha_ffi/src/pair_snapshot_ffi.rs");
    let restore = slice_between(&ffi, "pub fn restore_pair_candidate", "pub fn pair_status");
    assert!(restore.contains("restored_pair_latch("));
    assert!(!restore.contains("enumerate_live_pre_pair_choices"));
    assert!(!restore.contains("select_live_pre_pair_choice"));
    assert!(ffi.contains("project_dir.join(pre_instance_id).join(\"pre.json\")"));
    assert!(ffi.contains("LatchedPreReadiness::RestoredWaiting"));
    assert!(pair_snapshot.contains("pub fn paired_pre_locator(&self) -> Option<(String, String)>"));

    let pairing = read_repo("crates/kirin_measure/src/pairing_scope.rs");
    assert!(pairing.contains("confirm_restored_latch_runtime"));
    assert!(pairing.contains("LatchedTarget::RestoredWaiting => return None"));
}

#[test]
fn local_blind_capture_binds_the_existing_exact_pair_without_requiring_a_name() {
    let ffi_header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_pair_snapshot_ffi.h");
    assert!(ffi_header.contains("KirinExactPairBinding"));
    assert!(ffi_header.contains("pair_generation"));
    assert!(ffi_header.contains("project_hash"));
    assert!(ffi_header.contains("pre_instance_id"));
    assert!(ffi_header.contains("kirin_hypha_get_local_blind_pair_binding"));

    let binding = read_repo("crates/kirin_hypha_ffi/src/pair_binding.rs");
    assert!(binding.contains("selection_intent: AtomicBool"));
    assert!(binding.contains("pub(crate) fn exact_snapshot"));
    assert!(binding.contains("self.selection_intent.store(true, Ordering::Release)"));

    let capture = read_repo("juce_shell/src/local_blind/PairCaptureBarrier.h");
    assert!(capture.contains("struct ExactPairBinding"));
    assert!(capture.contains("struct ExactCaptureRequest"));
    assert!(capture.contains("class PairCaptureBarrier"));
    assert!(capture.contains("receipt.pair == pair"));
    assert!(capture.contains("sameRange (receipt.range, range (receipt.side))"));

    let processor = read_repo("juce_shell/src/PluginProcessorPairing.cpp");
    assert!(processor.contains("kirin_hypha_get_local_blind_pair_binding"));
    assert!(processor.contains("binding.pair_generation"));
    assert!(processor.contains("hostClockProbe.read (clock)"));
    assert!(processor.contains("const auto nativeStart = clock.position + sampleRate"));
    let request_header =
        read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_local_blind_capture_ffi.h");
    for required in [
        "KirinLocalBlindCaptureRequest",
        "pair_generation",
        "capture_generation",
        "clock_generation",
        "clock_source",
        "clock_position_at_issue",
        "native_start",
        "expires_at_unix_ms",
        "kirin_hypha_issue_local_blind_capture_request",
        "kirin_hypha_poll_local_blind_capture_request",
        "kirin_hypha_ack_local_blind_capture_request",
        "kirin_hypha_local_blind_capture_is_armed",
    ] {
        assert!(
            request_header.contains(required),
            "capture ABI missing {required}"
        );
    }
    let result_header =
        read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_local_blind_result_ffi.h");
    let transport = read_repo("juce_shell/src/PluginProcessorLocalBlindTransport.cpp");
    for required in [
        "KirinLocalBlindPreCaptureReceipt",
        "kirin_hypha_publish_local_blind_pre_capture",
        "kirin_hypha_read_local_blind_pre_capture",
        "kirin_hypha_ack_local_blind_pre_capture",
        "kirin_hypha_local_blind_pre_capture_was_consumed",
        "kirin_hypha_retire_local_blind_pre_capture",
    ] {
        assert!(
            result_header.contains(required),
            "result ABI missing {required}"
        );
        assert!(
            transport.contains(required),
            "JUCE result wiring missing {required}"
        );
    }
    assert!(transport.contains("receiptMatchesRequest (localReceipt, request)"));
    assert!(transport.contains("ExactRangeCapture::fromCompletedInterleaved"));
    for required in [
        "decodeCaptureRequest",
        "source.pair_generation",
        "source.capture_generation",
        "source.clock_generation",
        "source.clock_source",
        "source.clock_position_at_issue",
        "source.native_start",
        "kirin_hypha_issue_local_blind_capture_request",
        "kirin_hypha_poll_local_blind_capture_request",
        "kirin_hypha_ack_local_blind_capture_request",
        "kirin_hypha_local_blind_capture_is_armed",
    ] {
        assert!(
            processor.contains(required),
            "JUCE capture wiring missing {required}"
        );
    }
    let cmake = read_repo("juce_shell/CMakeLists.txt");
    assert!(cmake.contains("src/PluginProcessorPairing.cpp"));
    assert!(cmake.contains("src/PluginProcessorLocalBlindTransport.cpp"));
    let audio_callback = read_repo("juce_shell/src/PluginProcessor.cpp");
    assert!(!audio_callback.contains("kirin_hypha_publish_local_blind_pre_capture"));
    assert!(!audio_callback.contains("kirin_hypha_read_local_blind_pre_capture"));
}

#[test]
fn juce_wrappers_forward_host_presentation_latency_as_diagnostics() {
    let ffi_header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    assert!(ffi_header.contains("KIRIN_HYPHA_PRESENTATION_SOURCE_VST3 1"));
    assert!(ffi_header.contains("KIRIN_HYPHA_PRESENTATION_SOURCE_AUDIO_UNIT_V2 2"));

    let patch = read_repo("juce_shell/patches/0005-host-presentation-clock.patch");
    for required in [
        "setKirinInputPresentationLatencySamples",
        "setKirinOutputPresentationLatencySamples",
        "setKirinPresentationLatencySource",
        "Vst::IAudioPresentationLatency",
        "setAudioPresentationLatencySamples",
        "setKirinPresentationLatencySource (1)",
        "inputPresentationLatencySamples { -1 }",
        "outputPresentationLatencySamples { -1 }",
        "std::atomic<int64_t>::is_always_lock_free",
        "kAudioUnitProperty_PresentationLatency",
        "setKirinPresentationLatencySource (2)",
        "inputPresentationLatencySeconds { -1.0 }",
        "outputPresentationLatencySeconds { -1.0 }",
        "std::atomic<double>::is_always_lock_free",
        "info.busNr != 0 || info.kind != BusKind::processor",
        "sampleRate <= 0.0",
    ] {
        assert!(patch.contains(required), "patch must contain {required}");
    }

    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");
    let host_clock = read_repo("juce_shell/src/PluginProcessorHostClock.cpp");
    assert!(processor.contains("readHostProcessClock()"));
    assert!(host_clock.contains("getKirinInputPresentationLatencySamples"));
    assert!(host_clock.contains("getKirinOutputPresentationLatencySamples"));
    assert!(host_clock.contains("getKirinPresentationLatencySource"));
    assert!(processor.contains("kirin_hypha_note_capture_window"));

    // Both shipped formats now compile this processor and the same patched JUCE host adapter.
    let cmake = read_repo("juce_shell/CMakeLists.txt");
    assert!(cmake.contains("src/PluginProcessorHostClock.cpp"));
    assert!(cmake.contains("set(KIRIN_PLUGIN_FORMATS AU VST3)"));
    assert!(cmake.contains("FORMATS ${KIRIN_PLUGIN_FORMATS}"));
}

fn slice_between<'a>(src: &'a str, start_marker: &str, end_marker: &str) -> &'a str {
    let start = src.find(start_marker).expect("start marker must exist");
    let end = src[start..]
        .find(end_marker)
        .map(|idx| start + idx)
        .expect("end marker must follow start marker");
    &src[start..end]
}

#[test]
fn juce_prepare_does_not_destroy_engine_while_recording() {
    let src = read_repo("juce_shell/src/PluginProcessor.cpp");
    let body = slice_between(
        &src,
        "void KirinHyphaProcessorBase::prepareToPlay",
        "void KirinHyphaProcessorBase::releaseResources",
    );

    let guard = body
        .find("kirin_hypha_is_recording (hyphaHandle)")
        .expect("prepareToPlay must guard Record before engine rebuild");
    let destroy = body
        .find("kirin_hypha_destroy (hyphaHandle)")
        .expect("prepareToPlay contains the incompatible rebuild destroy path");
    assert!(
        guard < destroy,
        "Record guard must run before kirin_hypha_destroy in prepareToPlay"
    );

    let guarded_return = body[guard..destroy].contains("return;");
    assert!(
        guarded_return,
        "Record guard must return before the incompatible rebuild can destroy the engine"
    );
}

#[test]
fn vst3_component_activation_is_distinct_from_release_resources() {
    let header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    assert!(header.contains("kirin_hypha_set_host_component_active"));

    let processor_header = read_repo("juce_shell/src/PluginProcessor.h");
    assert!(processor_header.contains("hostComponentActivationChanged (bool active) override"));

    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");
    let release = slice_between(
        &processor,
        "void KirinHyphaProcessorBase::releaseResources",
        "void KirinHyphaProcessorBase::hostComponentActivationChanged",
    );
    assert!(
        !release.contains("kirin_hypha_set_host_component_active"),
        "generic releaseResources must not fabricate a user OFF state"
    );
    let activation = slice_between(
        &processor,
        "void KirinHyphaProcessorBase::hostComponentActivationChanged",
        "bool KirinHyphaProcessorBase::isBusesLayoutSupported",
    );
    assert!(activation.contains("hostComponentActive = active"));
    assert!(activation.contains("kirin_hypha_set_host_component_active"));

    let prepare = slice_between(
        &processor,
        "void KirinHyphaProcessorBase::prepareToPlay",
        "void KirinHyphaProcessorBase::releaseResources",
    );
    assert!(
        prepare
            .contains("kirin_hypha_set_host_component_active (hyphaHandle, hostComponentActive)"),
        "a host OFF delivered before engine creation must be applied to the fresh handle"
    );

    assert!(processor_header.contains("bool hostComponentActive = true"));

    let juce_patch = read_repo("juce_shell/patches/0007-vst3-host-component-activation.patch");
    assert!(juce_patch.contains("hostComponentActivationChanged (willBeActive)"));
    assert!(juce_patch.contains("virtual void hostComponentActivationChanged (bool)"));
}

#[test]
fn io_thread_shutdown_paths_mark_lifecycle_shutdown() {
    for (path, start_marker, close_marker) in [
        (
            "crates/kirin_measure/src/io_thread_pre.rs",
            "if let Some(mut ctx) = writer_ctx.take()",
            "writer_close_with_summary(ctx, summary);",
        ),
        (
            "crates/kirin_measure/src/io_thread_post_shutdown.rs",
            "if let Some(mut ctx) = recording",
            "writer_close_with_summary_and_marks(ctx, summary, record_mark_queue);",
        ),
    ] {
        let src = read_repo(path);
        let start = src
            .find(start_marker)
            .expect("shutdown-during-record writer context must exist");
        let close_end = src[start..]
            .find(close_marker)
            .map(|idx| start + idx + close_marker.len())
            .expect("shutdown path must close the writer");
        let body = &src[start..close_end];

        assert!(
            body.contains("ctx.writer.add_integrity_reason(\"lifecycle_shutdown\")"),
            "{path} must tag shutdown-during-record artifacts"
        );
        let reason = body
            .find("ctx.writer.add_integrity_reason(\"lifecycle_shutdown\")")
            .expect("reason call exists");
        let close = body.find(close_marker).expect("writer close exists");
        assert!(
            reason < close,
            "{path} must tag lifecycle shutdown before closing the writer"
        );
        if close_marker.contains("and_marks") {
            assert!(
                close_marker.contains("and_marks"),
                "POST lifecycle close must final-drain accepted MARKs"
            );
        }
    }
}

#[test]
fn pre_lifecycle_shutdown_does_not_exit_record_state_machine() {
    let src = read_repo("crates/kirin_measure/src/io_thread_pre.rs");
    let body = slice_between(
        &src,
        "if let Some(mut ctx) = writer_ctx.take()",
        "Do not delete pre.json",
    );

    assert!(
        body.contains("lifecycle shutdown is not Stop authority"),
        "PRE lifecycle shutdown block must document that it has no Stop authority"
    );
    assert!(
        !body.contains("record_sm.exit_record()"),
        "PRE lifecycle shutdown must not move the Record state machine back to Watch"
    );
    assert!(
        !body.contains("recording.store(false"),
        "PRE lifecycle shutdown must not publish a false recording state"
    );
    assert!(
        !body.contains("record_acknowledged.store(false"),
        "PRE lifecycle shutdown must not clear ACK state"
    );
}
