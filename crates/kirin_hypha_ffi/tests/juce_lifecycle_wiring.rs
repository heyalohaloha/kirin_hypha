use std::fs;
use std::path::PathBuf;

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|p| p.parent())
        .expect("crate lives under crates/")
        .to_path_buf()
}

fn read_repo(path: &str) -> String {
    let path = repo_root().join(path);
    fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()))
}

#[test]
fn shipped_au_and_vst3_compile_the_same_editor_processor_and_control_contract() {
    let ffi_header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    for symbol in [
        "kirin_hypha_select_pair_candidate",
        "kirin_hypha_pair_status",
        "kirin_hypha_get_paired_pre_instance_id",
        "kirin_hypha_drain_keep_action_notice",
        "kirin_hypha_poll_record_display",
        "kirin_hypha_poll_spectrum_batch",
    ] {
        assert!(ffi_header.contains(symbol), "FFI must expose {symbol}");
    }

    let juce_editor = read_repo("juce_shell/src/PluginEditor.cpp");
    for text in ["PAIR —", "PAIR ◌", "PAIR ●"] {
        assert!(
            juce_editor.contains(text),
            "common JUCE shell missing {text}"
        );
    }
    assert!(juce_editor.contains("setSize (ui::editorWidth, ui::editorHeight)"));
    assert!(juce_editor.contains("ui::editorLayout (isPost, getWidth(), getHeight())"));
    assert!(juce_editor.contains("ui::watchMetrics"));
    assert!(juce_editor.contains("ui::recordMetrics"));
    assert!(juce_editor.contains("ui::metricLabelFontHeight"));
    assert!(juce_editor.contains("ui::metricValueFontHeight"));
    assert!(juce_editor.contains("ui::metricUnitFontHeight"));
    assert!(juce_editor.contains("ui::maximumLabel"));
    assert!(juce_editor.contains("ui::loudnessSelectorBounds"));
    assert!(juce_editor.contains("cachedRecordDisplay.session"));
    assert!(juce_editor.contains("menu.addSectionHeader"));
    assert!(juce_editor.contains("withMinimumWidth (ui::pairMenuMinimumWidth)"));
    assert!(juce_editor.contains("withMaximumNumColumns (ui::pairMenuMaximumColumns)"));
    assert!(juce_editor.contains("withStandardItemHeight (ui::pairMenuItemHeight)"));
    assert!(juce_editor.contains("updateFeedback (t, t < bannerUntil, status)"));
    assert!(!juce_editor.contains("bannerLabel"));
    assert!(!juce_editor.contains("toastLabel"));
    assert!(!juce_editor.contains("recordErrorLabel"));
    assert!(
        juce_editor.contains("ui::preTitle : ui::postTitle")
            || juce_editor.contains("ui::postTitle : ui::preTitle")
    );
    assert!(!juce_editor.contains("setSize (300, 200)"));

    let ui_contract = read_repo("juce_shell/src/HyphaUiContract.h");
    for required in [
        "constexpr int editorWidth  = 300",
        "constexpr int editorHeight = 200",
        "constexpr EditorLayout editorLayout",
        "constexpr Rect metricCellBounds",
        "constexpr int pairMenuItemHeight     = 28",
        "constexpr int pairMenuMinimumWidth   = editorWidth",
        "constexpr int pairMenuMaximumColumns = 1",
        "constexpr std::array<MetricSlot, 6> watchMetrics",
        "constexpr std::array<MetricSlot, 6> recordMetrics",
        "POST feedback row must fit the 300x200 editor boundary",
    ] {
        assert!(
            ui_contract.contains(required),
            "common UI contract missing {required}"
        );
    }

    let theme = read_repo("juce_shell/src/HyphaTheme.h");
    for required in [
        "ui_contract::background",
        "ui_contract::normal",
        "ui_contract::muted",
        "ui_contract::flora",
        "ui_contract::labelFontFamily",
        "ui_contract::monoFontFamily",
    ] {
        assert!(
            theme.contains(required),
            "theme bypasses UI contract: {required}"
        );
    }

    let juce_controls = read_repo("juce_shell/src/PostControls.cpp");
    assert!(juce_controls.contains("keepBtn  .setVisible (! keepActive && os)"));
    assert!(juce_controls.contains("keepBtn  .setEnabled (pairSelected)"));
    assert!(juce_controls.contains("stopBtn  .setVisible (keepActive && os)"));
    assert!(!juce_controls.contains("markBtn"));
    let juce_controls_header = read_repo("juce_shell/src/PostControls.h");
    assert!(juce_controls_header.contains("ui_contract::keepLabel"));
    assert!(juce_controls_header.contains("ui_contract::stopLabel"));

    let cmake = read_repo("juce_shell/CMakeLists.txt");
    assert!(cmake.contains("set(KIRIN_PLUGIN_FORMATS AU VST3)"));
    assert!(cmake.contains("FORMATS ${KIRIN_PLUGIN_FORMATS}"));
    assert!(cmake.contains("src/PluginProcessor.cpp"));
    assert!(cmake.contains("src/PluginEditor.cpp"));
    assert!(cmake.contains("add_kirin_plugin(KirinHyphaPRE"));
    assert!(cmake.contains("add_kirin_plugin(KirinHyphaPOST"));

    let source_gate = read_repo("scripts/test_release_source.sh");
    assert!(source_gate.contains("juce_shell/tests/ui_contract_test.cpp"));
    assert!(source_gate.contains("-Wpedantic -Werror"));
    assert!(source_gate.contains("cargo build -p kirin_hypha_ffi --locked"));
    assert!(source_gate.contains("kirin_hypha_restore_pair_candidate"));
    assert!(source_gate.contains("kirin_hypha_get_paired_pre_locator"));
    assert!(source_gate.contains("kirin_hypha_poll_record_display"));
}

#[test]
fn direct_keep_feedback_is_a_consumable_edge_not_a_persistent_error() {
    let header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    assert!(header.contains("kirin_hypha_drain_keep_action_notice"));

    let ffi = read_repo("crates/kirin_hypha_ffi/src/lib.rs");
    assert!(ffi.contains("keep_action_notice: Arc<RwLock<Option<String>>>"));
    assert!(ffi.contains("pub fn drain_keep_action_notice(&self) -> Option<String>"));
    assert!(ffi.contains("and_then(|mut notice| notice.take())"));
    assert!(ffi.contains("*message = Some(\"Another Keep is active\".to_string())"));

    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");
    assert!(processor.contains("kirin_hypha_drain_keep_action_notice"));
    let editor = read_repo("juce_shell/src/PluginEditor.cpp");
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

    let producer = read_repo("crates/kirin_measure/src/io_thread_post.rs");
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
    let post_json = read_repo("crates/kirin_measure/src/io_thread_post.rs");
    assert!(pre_json.contains("\"lufs_s\""));
    assert!(post_json.contains("\"lufs_s\""));
    let plugin_data = read_repo("crates/kirin_measure/src/plugin_data.rs");
    assert!(
        !plugin_data.contains("\"lufs_s\""),
        "plugin_data/.kirin schema must remain unchanged"
    );
}

#[test]
fn hover_help_is_one_user_preference_without_touching_measurement_state() {
    let header = read_repo("juce_shell/src/HyphaHoverHelpPreference.h");
    let implementation = read_repo("juce_shell/src/HyphaHoverHelpPreference.cpp");
    let editor = read_repo("juce_shell/src/PluginEditor.cpp");
    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");

    assert!(header.contains("class HoverHelpTooltipWindow"));
    assert!(header.contains("TooltipWindow::getTipFor (component)"));
    assert!(implementation.contains("show_hover_help=1"));
    assert!(implementation.contains("show_hover_help=0"));
    assert!(implementation.contains("kRefreshIntervalMs = 1000u"));
    assert!(editor.contains("menu.addItem (10, \"Show hover help\""));
    assert!(editor.contains("tooltip.hideTip()"));
    assert!(editor.contains("Hover help changed for this session only"));
    assert!(!processor.contains("show_hover_help"));
    assert!(!processor.contains("HoverHelpPreference"));
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
    let restore = slice_between(&ffi, "pub fn restore_pair_candidate", "pub fn pair_status");
    assert!(restore.contains("restored_pair_latch("));
    assert!(!restore.contains("enumerate_live_pre_pair_choices"));
    assert!(!restore.contains("select_live_pre_pair_choice"));
    assert!(ffi.contains("project_dir.join(pre_instance_id).join(\"pre.json\")"));
    assert!(ffi.contains("LatchedPreReadiness::RestoredWaiting"));
    assert!(ffi.contains("pub fn paired_pre_locator(&self) -> Option<(String, String)>"));

    let pairing = read_repo("crates/kirin_measure/src/pairing_scope.rs");
    assert!(pairing.contains("confirm_restored_latch_runtime"));
    assert!(pairing.contains("LatchedTarget::RestoredWaiting => return None"));
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
    assert!(processor.contains("getKirinInputPresentationLatencySamples"));
    assert!(processor.contains("getKirinOutputPresentationLatencySamples"));
    assert!(processor.contains("getKirinPresentationLatencySource"));
    assert!(processor.contains("kirin_hypha_note_capture_window"));

    // Both shipped formats now compile this processor and the same patched JUCE host adapter.
    let cmake = read_repo("juce_shell/CMakeLists.txt");
    assert!(cmake.contains("set(KIRIN_PLUGIN_FORMATS AU VST3)"));
    assert!(cmake.contains("FORMATS ${KIRIN_PLUGIN_FORMATS}"));
}

#[test]
fn optional_analysis_is_post_only_on_demand_and_isolated_from_existing_schemas() {
    let header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h");
    for required in [
        "KIRIN_SPECTRUM_BAND_COUNT 256u",
        "KIRIN_SPECTRUM_DISPLAY_RANGE_DB 24.0f",
        "kirin_hypha_set_spectrum_visible",
        "kirin_hypha_set_perceptual_visible",
        "kirin_hypha_set_absolute_visible",
        "kirin_hypha_set_spectrum_channel_mode",
        "kirin_hypha_poll_spectrum",
        "kirin_hypha_poll_perceptual",
        "kirin_hypha_poll_perceptual_batch",
        "kirin_hypha_poll_absolute_batch",
        "KIRIN_PERCEPTUAL_BATCH_CAPACITY 64",
        "KIRIN_ABSOLUTE_BATCH_CAPACITY 64",
    ] {
        assert!(header.contains(required), "Spectrum ABI missing {required}");
    }

    let runtime = read_repo("crates/kirin_measure/src/spectrum_runtime.rs");
    let ingress = slice_between(
        &runtime,
        "pub fn push_block_from_audio",
        "pub fn try_history",
    );
    let enabled_check = ingress
        .find("if !self.enabled.load")
        .expect("hidden Spectrum path must start with an atomic enabled gate");
    let producer_access = ingress
        .find("self.sample_producer.get()")
        .expect("enabled Spectrum path must use its own bounded producer");
    assert!(enabled_check < producer_access);
    for forbidden in ["Mutex", "fs::", "SpectrumAnalyzer", "spawn(", "Vec::"] {
        assert!(
            !ingress.contains(forbidden),
            "Audio ingress contains {forbidden}"
        );
    }

    let exchange = read_repo("crates/kirin_measure/src/spectrum_exchange.rs");
    let protocol = read_repo("crates/kirin_measure/src/analysis_exchange_protocol.rs");
    let lease = read_repo("crates/kirin_measure/src/analysis_lease.rs");
    assert!(protocol.contains("join(\"spectrum\").join(\"request.json\")"));
    assert!(protocol.contains("state_epoch_samples"));
    assert!(exchange.contains("AnalysisLease::for_current_process()"));
    assert!(lease.contains("file.try_lock()"));
    assert!(exchange.contains("join(\"spectrum\").join(\"pre.bin\")"));
    assert!(exchange.contains("join(\"spectrum\").join(\"pre_perceptual.bin\")"));
    assert!(!exchange.contains("plugin_data"));
    assert!(!protocol.contains("plugin_data"));

    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");
    assert!(processor.contains("perceptualAnalysisRequested.store (true"));
    assert!(processor.contains("perceptualAnalysisRequested.load"));
    assert!(processor.contains("kirin_hypha_set_perceptual_visible (hyphaHandle, true)"));
    assert!(processor.contains("kirin_hypha_set_absolute_visible (hyphaHandle, true)"));
    let processor_header = read_repo("juce_shell/src/PluginProcessor.h");
    assert!(processor_header.contains("index < 4u ? index : 0u"));

    let editor = read_repo("juce_shell/src/PluginEditor.cpp");
    assert!(editor.contains("#if ! KIRIN_HYPHA_PRE_DISPLAY"));
    assert!(editor.contains("setAnalysisPage (analysisPage == AnalysisPage::meters"));
    assert!(editor.contains("page == AnalysisPage::spectrum ? \"FREQ\""));
    assert!(editor.contains("page == AnalysisPage::perceptual ? \"SHARP\" : \"LIVE\""));
    assert!(editor.contains("processorRef.setSpectrumVisible (false)"));
    assert!(editor.contains("processorRef.setPerceptualVisible (false)"));
    assert!(editor.contains("AnalysisPage::perceptual"));
    assert!(editor.contains("AnalysisPage::absolute"));
    assert!(editor.contains("processorRef.pollAbsoluteBatch"));
    assert!(editor.contains("spectrumSizeIndex + 1u"));
    assert!(editor.contains("ui::spectrumSizePresets[spectrumSizeIndex]"));
    assert!(editor.contains(": ui::spectrumSizePresets[0]"));
    assert!(editor.contains("setSize (preset.width, preset.height)"));
    assert!(!editor.contains("setResizable (true"));

    let ui_contract = read_repo("juce_shell/src/HyphaSpectrumUiContract.h");
    for fixed_size in [
        "{ 300, 200, \"100%\"",
        "{ 375, 250, \"125%\"",
        "{ 450, 300, \"150%\"",
        "{ 600, 400, \"200%\"",
    ] {
        assert!(
            ui_contract.contains(fixed_size),
            "POST Spectrum fixed size missing {fixed_size}"
        );
    }

    let processor = read_repo("juce_shell/src/PluginProcessor.cpp");
    assert!(
        !processor.contains("spectrum_size"),
        "editor size must not enter DAW/plugin state"
    );

    let cmake = read_repo("juce_shell/CMakeLists.txt");
    let post_only_branch = slice_between(
        &cmake,
        "else()\n        target_sources(${TARGET} PRIVATE\n            src/HyphaAbsoluteComponent.cpp",
        "endif()",
    );
    assert!(post_only_branch.contains("src/HyphaPerceptualPainter.cpp"));
    assert!(post_only_branch.contains("src/HyphaAbsolutePainter.cpp"));
    assert!(post_only_branch.contains("src/HyphaSpectrumComponent.cpp"));
    assert!(post_only_branch.contains("src/HyphaSpectrumPainter.cpp"));
    assert!(post_only_branch.contains("src/HyphaSpectrumChromePainter.cpp"));
    assert!(post_only_branch.contains("src/HyphaSpectrumFocusTrail.cpp"));
    assert!(post_only_branch.contains("src/HyphaSpectrumFocusTrailPainter.cpp"));
    assert!(post_only_branch.contains("KIRIN_HYPHA_PRE_DISPLAY=0"));

    let focus_trail = read_repo("juce_shell/src/HyphaSpectrumFocusTrail.h");
    assert!(focus_trail.contains("focusTrailCapacity"));
    assert!(focus_trail.contains("sizeof (FocusTrailHistory) < 256u * 1024u"));
    let component = read_repo("juce_shell/src/HyphaSpectrumComponent.cpp");
    assert!(component.contains("snapshot.presentation_end_samples"));
    assert!(component.contains("focusTrail.reset()"));

    let perceptual_history = read_repo("juce_shell/src/HyphaPerceptualHistory.h");
    assert!(perceptual_history.contains("historyCapacity = 60u"));
    assert!(perceptual_history.contains("continuesPrevious"));
    let perceptual = read_repo("crates/kirin_measure/src/perceptual.rs");
    assert!(perceptual.contains("presentation_end_samples"));
    assert!(perceptual.contains("aperture_samples"));
    assert!(perceptual.contains("post.sharpness - pre.sharpness"));

    let plugin_data = read_repo("crates/kirin_measure/src/plugin_data.rs");
    assert!(!plugin_data.contains("spectrum"));
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
            "crates/kirin_measure/src/io_thread_post.rs",
            "if let Some(mut ctx) = recording.take()",
            "writer_close_with_summary_and_marks(ctx, summary, &record_mark_queue);",
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
        if path.ends_with("io_thread_post.rs") {
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
