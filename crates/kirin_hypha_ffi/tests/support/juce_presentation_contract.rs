use super::{read_repo, slice_between};

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
    let juce_editor = read_repo("juce_shell/src/PluginEditor.cpp")
        + &read_repo("juce_shell/src/PluginEditorObservatory.cpp")
        + &read_repo("juce_shell/src/PluginEditorAnalysis.cpp");
    for text in ["PAIR —", "PAIR ◌", "PAIR ●"] {
        assert!(
            juce_editor.contains(text),
            "common JUCE shell missing {text}"
        );
    }
    assert!(juce_editor.contains("unpackEditorSize"));
    assert!(juce_editor.contains("setSize (initialWidth, initialHeight)"));
    assert!(juce_editor.contains("observatoryView.setBounds (scaleRoot.getLocalBounds())"));
    assert!(juce_editor.contains("observatoryView.connectionBounds()"));
    assert!(juce_editor.contains("observatoryView.bodyBounds()"));
    assert!(juce_editor.contains("ui::watchMetrics"));
    assert!(juce_editor.contains("ui::recordMetrics"));
    assert!(juce_editor.contains("ui::metricLabelFontHeight"));
    assert!(juce_editor.contains("ui::metricValueFontHeight"));
    assert!(juce_editor.contains("ui::metricUnitFontHeight"));
    assert!(juce_editor.contains("ui::maximumLabel"));
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
    let observatory = read_repo("juce_shell/src/HyphaObservatoryContract.h");
    for required in [
        "enum class Domain",
        "    level,",
        "    time,",
        "    frequency,",
        "    space,",
        "enum class ObservationTarget",
        "    absolute,",
        "    delta,",
    ] {
        assert!(
            observatory.contains(required),
            "Observatory contract missing {required}"
        );
    }
    let observatory_resize = read_repo("juce_shell/src/HyphaObservatoryResizeContract.h");
    assert!(observatory_resize.contains("std::array<SizePreset, 5>"));
    assert!(observatory_resize.contains("validEditorSize"));
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
        "usingKimeraTypography",
        "tabularTextWidth",
    ] {
        assert!(
            theme.contains(required),
            "theme bypasses UI contract: {required}"
        );
    }
    let juce_controls = read_repo("juce_shell/src/PostControls.cpp");
    assert!(!juce_controls.contains("markBtn"));
    let juce_controls_header = read_repo("juce_shell/src/PostControls.h");
    assert!(juce_controls_header.contains("ui_contract::keepLabel"));
    assert!(juce_controls_header.contains("ui_contract::stopLabel"));
    let cmake = read_repo("juce_shell/CMakeLists.txt");
    assert!(cmake.contains("set(KIRIN_PLUGIN_FORMATS AU VST3)"));
    assert!(cmake.contains("FORMATS ${KIRIN_PLUGIN_FORMATS}"));
    assert!(cmake.contains("src/PluginProcessor.cpp"));
    assert!(cmake.contains("src/PluginEditor.cpp"));
    assert!(cmake.contains("src/PostControls.cpp"));
    assert!(cmake.contains("tests/OsAccessUiContractTest.cpp"));
    assert!(cmake.contains("add_test(NAME kirin_ui_render_contract"));
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
fn hypha_title_information_is_shipped_while_capture_validation_stays_debug_only() {
    let editor = read_repo("juce_shell/src/PluginEditor.cpp");
    let information = read_repo("juce_shell/src/PluginEditorInformation.cpp");

    assert!(editor.contains("observatoryView.onInformation = [this] { showInformationMenu(); }"));
    let (shipped_before_debug, debug_and_after) = information
        .split_once("#if JUCE_DEBUG")
        .expect("information menu must isolate host validation from shipped information");
    let (_, shipped_after_debug) = debug_and_after
        .split_once("#endif")
        .expect("information menu debug isolation must be closed");
    let shipped_information = format!("{shipped_before_debug}{shipped_after_debug}");
    for required in [
        "Loaded v",
        "Official release identity not verified",
        "Update information and downloads (English)",
        "Release notes",
        "Show hover help",
        "Show Hybrid VU while recording",
        "Show selected view for this recording",
    ] {
        assert!(
            shipped_information.contains(required),
            "shipped HYPHA PRE/POST information menu missing {required}"
        );
    }
    assert!(!shipped_information.contains("Capture one exact 4 s PRE/POST range"));
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
    assert!(processor_header.contains("index < 5u ? index : uint8_t { 0 }"));
    assert!(processor_header.contains("preferredSpectrumSize { 0 }"));
    assert!(processor_header.contains("preferredEditorSize { (300u << 16u) | 200u }"));

    let editor = read_repo("juce_shell/src/PluginEditor.cpp")
        + &read_repo("juce_shell/src/PluginEditorAnalysis.cpp");
    let observatory_editor = read_repo("juce_shell/src/PluginEditorObservatory.cpp");
    let resize_contract = read_repo("juce_shell/src/HyphaObservatoryResizeContract.h");
    let time_navigation = read_repo("juce_shell/src/HyphaTimePageNavigation.cpp");
    let time_navigation_header = read_repo("juce_shell/src/HyphaTimePageNavigation.h");
    let analysis_navigation = read_repo("juce_shell/src/HyphaAnalysisNavigation.h");
    assert!(editor.contains("#if ! KIRIN_HYPHA_PRE_DISPLAY"));
    assert!(editor.contains("setAnalysisPage (analysisPage == AnalysisPage::meters"));
    assert!(editor.contains("timePageNavigation.onPageChange"));
    for label in ["HISTORY", "RUN", "DRUM", "SHARP", "LIVE"] {
        assert!(time_navigation.contains(label) || time_navigation_header.contains(label));
    }
    assert!(analysis_navigation
        .contains("Page::meters, Page::run, Page::attack, Page::perceptual, Page::absolute"));
    assert!(observatory_editor.contains("? AnalysisPage::spectrum : AnalysisPage::meters"));
    assert!(editor.contains("processorRef.setSpectrumVisible (false)"));
    assert!(editor.contains("processorRef.setPerceptualVisible (false)"));
    assert!(editor.contains("AnalysisPage::perceptual"));
    assert!(editor.contains("AnalysisPage::absolute"));
    assert!(editor.contains("processorRef.pollAbsoluteBatch"));
    assert!(editor.contains("observatorySizeIndex + 1u"));
    assert!(editor.contains("ui::spectrumSizePresets[observatorySizeIndex]"));
    assert!(editor.contains("setSize (preset.width, preset.height)"));
    assert!(editor.contains("setResizable (true, false)"));
    assert!(editor.contains("setResizeLimits (300, 200, 900, 600)"));
    assert!(editor.contains("setFixedAspectRatio (1.5)"));
    assert!(editor.contains("displayViewport (getWidth(), getHeight())"));
    assert!(editor.contains("scaleRoot.setOpaque (true)"));
    assert!(editor.contains("scaleRoot.setBufferedToImage (false)"));
    assert!(resize_contract.contains("return { width, height, 1.0f };"));
    assert!(observatory_editor.contains("observatoryEditorSizePreference"));
    assert!(observatory_editor
        .contains("setSize (restoredEditorSize.width, restoredEditorSize.height)"));

    let ui_contract = read_repo("juce_shell/src/HyphaSpectrumUiContract.h");
    for fixed_size in [
        "{ 300, 200, \"100%\"",
        "{ 375, 250, \"125%\"",
        "{ 450, 300, \"150%\"",
        "{ 600, 400, \"200%\"",
        "{ 900, 600, \"300%\"",
    ] {
        assert!(
            ui_contract.contains(fixed_size),
            "POST Spectrum fixed size missing {fixed_size}"
        );
    }

    let processor = read_repo("juce_shell/src/PluginProcessor.cpp")
        + &read_repo("juce_shell/src/PluginProcessorDisplayState.cpp");
    assert!(processor.contains("setObservatoryEditorSizePreference"));
    assert!(processor.contains("editorSizeFromState"));
    assert!(processor.contains("packEditorSize"));
    let editor = read_repo("juce_shell/src/PluginEditor.cpp")
        + &read_repo("juce_shell/src/PluginEditorObservatory.cpp");
    assert!(editor.contains("void KirinHyphaEditor::visibilityChanged()"));
    assert!(editor.contains("commitEditorSizeStateIfSettled (true)"));

    let cmake = read_repo("juce_shell/CMakeLists.txt");
    let post_only_branch = slice_between(
        &cmake,
        "else()\n        target_sources(${TARGET} PRIVATE",
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
