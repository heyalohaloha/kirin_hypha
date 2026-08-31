#[cfg(test)]
mod tests {
    const POST_CONTROLS_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PostControls.cpp"
    ));
    const POST_CONTROLS_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PostControls.h"
    ));
    const README: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/../README.md"));
    const PLUGIN_EDITOR_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditor.cpp"
    ));
    const PLUGIN_EDITOR_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditor.h"
    ));
    const PLUGIN_EDITOR_OBSERVATORY_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditorObservatory.cpp"
    ));
    const PLUGIN_EDITOR_CAPTURE_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditorCapture.cpp"
    ));
    const PLUGIN_PROCESSOR_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessor.cpp"
    ));
    const PLUGIN_PROCESSOR_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessor.h"
    ));
    const JUCE_PLUGIN_CONFIG: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/KirinJucePluginConfig.h"
    ));
    const JUCE_CMAKE: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/CMakeLists.txt"
    ));
    const FFI_HEADER: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h"
    ));
    const HYPHA_THEME_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaTheme.h"
    ));
    const HYPHA_TYPOGRAPHY_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaTypography.cpp"
    ));
    const HYPHA_UI_CONTRACT_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaUiContract.h"
    ));
    const HYPHA_OBSERVATORY_VIEW_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaObservatoryView.h"
    ));
    const HYPHA_DISPLAY_CONTRACT_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaDisplayContract.h"
    ));
    const PRE_DISPLAY_CONTROLLER_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayController.cpp"
    ));
    const PRE_DISPLAY_REPOSITORY_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayRepository.cpp"
    ));
    const PRE_DISPLAY_PROTOCOL_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayProtocol.cpp"
    ));
    const PRE_DISPLAY_PROTOCOL_TIME_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayProtocolTime.cpp"
    ));
    const PRE_DISPLAY_TRANSPORT_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/pre_display/PreDisplayTransport.cpp"
    ));

    fn read_juce_au_wrapper() -> Option<String> {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(
            "../juce_shell/JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_AU_1.mm",
        );
        std::fs::read_to_string(path)
            .ok()
            .map(|source| source.replace("\r\n", "\n"))
    }

    fn between<'a>(source: &'a str, start: &str, end: &str) -> &'a str {
        let start_index = source.find(start).expect(start) + start.len();
        let tail = &source[start_index..];
        let end_index = tail.find(end).expect(end);
        &tail[..end_index]
    }

    fn count_occurrences(source: &str, needle: &str) -> usize {
        source.match_indices(needle).count()
    }

    #[test]
    fn readme_record_controls_match_shipped_post_ui() {
        let record_mode = README
            .split("\n## ")
            .find(|section| section.starts_with("Record mode (Kirin OS required)\n"))
            .expect("README Record mode section");
        assert!(record_mode.contains("1. Press **Keep**"));
        assert!(record_mode.contains("2. Press **Stop**"));
        assert!(
            !record_mode.contains("**Mark**"),
            "README must not advertise a Mark control absent from the shipping UI"
        );
        assert!(POST_CONTROLS_H.contains("HyphaTextButton keepBtn"));
        assert!(POST_CONTROLS_H.contains("HyphaTextButton stopBtn"));
        assert!(!POST_CONTROLS_H.contains("markBtn"));
        assert!(!POST_CONTROLS_CPP.contains("onMark"));
    }

    #[test]
    fn common_juce_shell_owns_watch_max_for_both_formats_and_roles() {
        assert!(FFI_HEADER.contains("KirinMeasureResult current;"));
        assert!(FFI_HEADER.contains("KirinMeasureResult maximum;"));
        assert!(FFI_HEADER.contains("kirin_hypha_poll_watch_display"));
        assert_eq!(
            count_occurrences(PLUGIN_EDITOR_CPP, "processorRef.pollWatchDisplay (watch)"),
            2,
            "shared AU/VST3 editor must poll Watch current+MAX in both roles"
        );
        assert_eq!(
            count_occurrences(PLUGIN_EDITOR_CPP, "watchMaximum = watch.maximum;"),
            2,
            "shared AU/VST3 editor must retain MAX in both roles"
        );
        for metric in [
            "return useShortTerm ? value.lufs_s : value.lufs_m",
            "selectedMeasure (watchMaximum)",
            "watchMaximum.true_peak",
            "watchMaximum.crest",
        ] {
            assert!(
                PLUGIN_EDITOR_CPP.contains(metric),
                "common shell missing {metric}"
            );
        }
        assert!(JUCE_CMAKE.contains("FORMATS ${KIRIN_PLUGIN_FORMATS}"));
        assert!(JUCE_CMAKE.contains("src/PluginEditor.cpp"));
    }

    #[test]
    fn shipped_vst3_and_au_share_the_licensed_kimera_typeface_contract() {
        assert!(HYPHA_THEME_H.contains("usingKimeraTypography"));
        assert!(HYPHA_TYPOGRAPHY_CPP.contains("BinaryData::KMRWaldenburgBook_otf"));
        assert!(HYPHA_UI_CONTRACT_H.contains("kimeraFontFamily = \"KMR Waldenburg Book\""));
        assert!(JUCE_CMAKE.contains("KIRIN_HYPHA_KIMERA_APP_LICENSE_CONFIRMED"));
        assert!(JUCE_CMAKE.contains("KIRIN_HYPHA_REQUIRE_KIMERA_FONT"));
        assert!(JUCE_CMAKE.contains("src/HyphaTypography.cpp"));
    }

    #[test]
    fn capture_freezes_the_authoritative_frame_before_the_async_save_panel() {
        let freeze = PLUGIN_EDITOR_CAPTURE_CPP
            .find("auto image = observatoryView.createCaptureImage")
            .expect("Capture must freeze the parent frame");
        let chooser = PLUGIN_EDITOR_CAPTURE_CPP
            .find("captureChooser->launchAsync")
            .expect("Capture must use the asynchronous save panel");
        assert!(
            freeze < chooser,
            "visual facts must freeze before filename selection"
        );
        assert!(PLUGIN_EDITOR_CAPTURE_CPP.contains("[safeThis, image]"));
        assert_eq!(
            count_occurrences(
                PLUGIN_EDITOR_CAPTURE_CPP,
                "observatoryView.createCaptureImage"
            ),
            1
        );
    }

    #[test]
    fn shipped_post_pair_selector_has_one_geometry_source() {
        assert!(HYPHA_OBSERVATORY_VIEW_H.contains("connectionBounds() const noexcept"));
        assert!(PLUGIN_EDITOR_CPP
            .contains("auto connection = observatoryView.connectionBounds().reduced (4, 2)"));
        assert!(
            PLUGIN_EDITOR_CPP.contains("pairDropdown.setBounds (connection.removeFromRight (18))")
        );
        assert_eq!(
            count_occurrences(PLUGIN_EDITOR_CPP, "pairDropdown.setBounds"),
            1
        );
        assert!(!PLUGIN_EDITOR_CPP.contains("const int ddW = 22;"));
        assert!(PLUGIN_EDITOR_CPP.contains("menu.setLookAndFeel (&pairMenuLookAndFeel())"));
        assert!(
            PLUGIN_EDITOR_H.contains("setColour (juce::PopupMenu::backgroundColourId, hypha::BG);")
        );
    }

    #[test]
    fn au_and_vst3_compile_the_same_editor_without_format_specific_ui_branches() {
        assert!(JUCE_CMAKE.contains("set(KIRIN_PLUGIN_FORMATS AU VST3)"));
        assert_eq!(count_occurrences(JUCE_CMAKE, "src/PluginEditor.cpp"), 1);
        assert_eq!(count_occurrences(JUCE_CMAKE, "src/HyphaWidgets.cpp"), 1);
        assert_eq!(count_occurrences(JUCE_CMAKE, "src/PostControls.cpp"), 1);
        for forbidden in [
            "JucePlugin_Build_AU",
            "JucePlugin_Build_VST3",
            "JucePlugin_Build_AUv3",
        ] {
            assert!(
                !PLUGIN_EDITOR_CPP.contains(forbidden)
                    && !PLUGIN_EDITOR_H.contains(forbidden)
                    && !POST_CONTROLS_CPP.contains(forbidden),
                "format-specific UI branch can make AU and VST3 visually diverge: {forbidden}"
            );
        }
    }

    #[test]
    fn guide_transport_is_compiled_into_both_roles_and_projected_by_the_parent_shell() {
        assert!(JUCE_PLUGIN_CONFIG.contains("#define KIRIN_HYPHA_PRE_DISPLAY 0"));
        assert!(JUCE_PLUGIN_CONFIG.contains("#define KIRIN_HYPHA_GUIDE_TRANSPORT 0"));
        assert_eq!(
            count_occurrences(JUCE_CMAKE, "src/pre_display/PreDisplayController.cpp"),
            2
        );
        for source in [
            "PreDisplayPresence.cpp",
            "PreDisplayProjection.cpp",
            "PreDisplayProtocol.cpp",
            "PreDisplayProtocolTime.cpp",
            "PreDisplayRepository.cpp",
            "PreDisplayTransport.cpp",
        ] {
            assert_eq!(count_occurrences(JUCE_CMAKE, source), 2, "{source}");
        }
        assert!(JUCE_CMAKE.contains("if(\"${TARGET}\" STREQUAL \"KirinHyphaPRE\")"));
        assert!(JUCE_CMAKE.contains("KIRIN_HYPHA_PRE_DISPLAY=1"));
        assert!(JUCE_CMAKE.contains("KIRIN_HYPHA_PRE_DISPLAY=0"));
        assert!(JUCE_CMAKE.contains("KIRIN_HYPHA_GUIDE_TRANSPORT=1"));
        assert!(PLUGIN_PROCESSOR_H.contains("#if KIRIN_HYPHA_GUIDE_TRANSPORT"));
        assert!(PLUGIN_PROCESSOR_CPP.contains("#if KIRIN_HYPHA_GUIDE_TRANSPORT"));
        assert!(PRE_DISPLAY_PROTOCOL_TIME_CPP.contains("bool canonicalIsoInstant"));
        assert!(!PRE_DISPLAY_PROTOCOL_CPP.contains("bool canonicalIsoInstant"));
        assert!(
            JUCE_CMAKE.contains("target_link_libraries(${TARGET} PRIVATE juce::juce_cryptography)")
        );
        assert!(PLUGIN_EDITOR_H.contains("juce::TextButton          guideConnectButton"));
        assert!(PLUGIN_EDITOR_OBSERVATORY_CPP.contains("pendingPreDisplayConnection"));
        assert!(PLUGIN_EDITOR_OBSERVATORY_CPP.contains("preDisplaySnapshot"));
        assert!(
            PLUGIN_EDITOR_OBSERVATORY_CPP.contains("display.sectionActive || display.cueActive")
        );
        assert!(PLUGIN_EDITOR_OBSERVATORY_CPP.contains("observatoryView.setGuide"));
        assert!(PLUGIN_EDITOR_CPP.contains("acceptPreDisplayConnection"));
        assert_eq!(
            count_occurrences(PLUGIN_PROCESSOR_CPP, "preDisplayClock.publish"),
            1,
            "the audio callback is the only PRE display clock writer"
        );
        assert!(HYPHA_UI_CONTRACT_H.contains("POST must not acquire PRE display geometry"));
        assert!(PRE_DISPLAY_REPOSITORY_CPP
            .contains(".getChildFile (\"active\").getChildFile (\"kirin_os.json\")"));
        for source in [
            PRE_DISPLAY_CONTROLLER_CPP,
            PRE_DISPLAY_REPOSITORY_CPP,
            PRE_DISPLAY_PROTOCOL_CPP,
            PRE_DISPLAY_PROTOCOL_TIME_CPP,
            PRE_DISPLAY_TRANSPORT_CPP,
        ] {
            assert!(!source.contains("TRACE"));
        }
        assert!(PRE_DISPLAY_CONTROLLER_CPP.lines().count() < 200);
        assert!(PRE_DISPLAY_CONTROLLER_CPP.contains("stopThread (-1)"));
        assert!(!PRE_DISPLAY_CONTROLLER_CPP.contains("stopThread (2'000)"));
        assert!(PRE_DISPLAY_TRANSPORT_CPP.contains("juce::File::windowsLocalAppData"));
        assert!(!PRE_DISPLAY_TRANSPORT_CPP.contains("juce::File::userApplicationDataDirectory"));
        assert!(PRE_DISPLAY_CONTROLLER_CPP.contains("root.getFullPathName().isEmpty()"));
        assert!(PRE_DISPLAY_CONTROLLER_CPP.contains("writeAcknowledgement"));
        assert!(!PLUGIN_EDITOR_CPP.contains("preDisplayPrimaryLabel.setAlpha"));
        assert!(!PLUGIN_EDITOR_CPP.contains("preDisplayDetailLabel.setAlpha"));
        for forbidden in ["juce::JSON::parse", "juce::SHA256", "kirin_os.clear.json"] {
            assert!(!PRE_DISPLAY_CONTROLLER_CPP.contains(forbidden));
        }
    }

    #[test]
    fn pre_display_explicit_clear_precedes_active_pointer_recovery() {
        let clear = PRE_DISPLAY_REPOSITORY_CPP
            .find("kirin_os.clear.json")
            .expect("group clear authority must be read");
        let pointer = PRE_DISPLAY_REPOSITORY_CPP
            .find("kirin_os.json")
            .expect("active pointer must be read");
        assert!(
            clear < pointer,
            "explicit clear must be resolved before any active pointer"
        );
        assert!(PRE_DISPLAY_REPOSITORY_CPP.contains("retainedGuide = {};"));
    }

    #[test]
    fn keep_preparing_and_armed_are_visible_and_stoppable_before_record_ack() {
        let body = between(
            PLUGIN_EDITOR_CPP,
            "void KirinHyphaEditor::updatePost()",
            "const int ledSig",
        );
        assert!(FFI_HEADER.contains("#define KIRIN_KEEP_PHASE_IDLE 0u"));
        assert!(FFI_HEADER.contains("#define KIRIN_KEEP_PHASE_PREPARING 1u"));
        assert!(FFI_HEADER.contains("#define KIRIN_KEEP_PHASE_ARMED 2u"));
        assert!(FFI_HEADER.contains("uint8_t kirin_hypha_keep_phase(KirinHypha* handle);"));
        assert!(PLUGIN_PROCESSOR_CPP.contains("kirin_hypha_keep_phase (hyphaHandle)"));
        assert!(body.contains("const bool keepActive = rec || preparing || armed;"));
        assert!(body.contains("\"Preparing pairs...\""));
        assert!(body.contains("\"Ready to bounce\""));
        assert!(body.contains("postControls->update (keepActive"));
        assert!(POST_CONTROLS_CPP.contains("stopBtn  .setVisible (keepActive && os);"));
    }

    #[test]
    fn common_vst3_preserves_legacy_component_ids_and_state() {
        for word in [
            "0x4B697269",
            "0x6E487970",
            "0x68615052",
            "0x45763031",
            "0x6861504F",
            "0x53547631",
        ] {
            assert!(JUCE_CMAKE.contains(word), "missing VST3 UID word {word}");
        }
        assert!(FFI_HEADER.contains("KirinLegacyNihState"));
        assert!(FFI_HEADER.contains("kirin_hypha_decode_legacy_nih_state"));
        assert!(FFI_HEADER.contains("kirin_hypha_restore_pair_candidate"));
        assert!(FFI_HEADER.contains("kirin_hypha_get_paired_pre_locator"));
        assert!(PLUGIN_PROCESSOR_CPP.contains("kirin_hypha_decode_legacy_nih_state"));
        assert!(PLUGIN_PROCESSOR_CPP
            .contains("xml.setAttribute (\"paired_pre_instance_id\", persistPairInstanceId)"));
        assert!(PLUGIN_PROCESSOR_CPP.contains(
            "restoredPairInstanceId = xml->getStringAttribute (\"paired_pre_instance_id\")"
        ));
        assert!(PLUGIN_PROCESSOR_CPP
            .contains("xml.setAttribute (\"paired_pre_project_hash\", persistPairProjectHash)"));
        assert!(PLUGIN_PROCESSOR_CPP.contains(
            "restoredPairProjectHash = xml->getStringAttribute (\"paired_pre_project_hash\")"
        ));
        assert!(PLUGIN_PROCESSOR_CPP
            .contains("pairedPreLocator (livePairProjectHash, livePairInstanceId)"));
        assert!(PLUGIN_PROCESSOR_CPP.contains("kirin_hypha_restore_pair_candidate ("));
    }

    #[test]
    fn juce_default_build_refreshes_rust_ffi_before_link() {
        assert!(JUCE_CMAKE.contains("add_custom_target(KirinHyphaRustFFI"));
        assert!(JUCE_CMAKE.contains("build --release -p kirin_hypha_ffi --locked"));
        assert!(JUCE_CMAKE.contains("BYPRODUCTS ${KIRIN_FFI_LIB}"));
        assert!(JUCE_CMAKE.contains("add_dependencies(${TARGET} KirinHyphaRustFFI)"));
        assert!(JUCE_CMAKE.contains("cmake_path(ABSOLUTE_PATH _KIRIN_FFI_SELECTED_PATH NORMALIZE"));
        assert!(JUCE_CMAKE.contains("cmake_path(ABSOLUTE_PATH _KIRIN_FFI_DEFAULT_PATH NORMALIZE"));
        assert!(JUCE_CMAKE
            .contains("if(\"${_KIRIN_FFI_SELECTED_ABS}\" STREQUAL \"${_KIRIN_FFI_DEFAULT_ABS}\")"));
    }

    #[test]
    fn post_controls_keep_slot_is_fixed_and_availability_depends_on_selected_pair() {
        assert!(POST_CONTROLS_CPP.contains(
            "void PostControls::update (bool keepActive, int license, bool pairSelected)"
        ));
        assert!(POST_CONTROLS_CPP.contains("keepBtn  .setVisible (! keepActive && os);"));
        assert!(POST_CONTROLS_CPP.contains("keepBtn  .setEnabled (pairSelected);"));
        assert!(!POST_CONTROLS_CPP
            .contains("keepBtn  .setVisible (! keepActive && os && pairSelected);"));
    }

    /// B-195 (Step3 監査ギャップ): PostControls::update の可視性式を **全行** 固定する。
    /// これにより kirin_hypha_ffi の値レベル parity replica
    /// (post_controls_parity_tests::post_controls_visibility_matches_rust_license_helpers) が
    /// C++ ソースと一致したままであることを保証する。C++ の os/sense マッピングや
    /// 各ボタン行が変われば本テストが落ち、replica を同時更新する必要があると分かる。
    #[test]
    fn post_controls_update_visibility_formula_is_pinned() {
        let body = between(
            POST_CONTROLS_CPP,
            "void PostControls::update (bool keepActive, int license, bool pairSelected)",
            "void PostControls::resized()",
        );
        assert!(body.contains("const bool os    = (license == 0);"));
        assert!(body.contains("const bool sense = (license == 1);"));
        assert!(body.contains("keepBtn  .setVisible (! keepActive && os);"));
        assert!(body.contains("keepBtn  .setEnabled (pairSelected);"));
        assert!(body.contains("senseBtn .setVisible (! keepActive && sense);"));
        assert!(body.contains("stopBtn  .setVisible (keepActive && os);"));
        assert!(!body.contains("markBtn"));
        assert!(!body.contains("markPickerOpen"));
    }

    #[test]
    fn candidate_menu_enumerates_pre_candidates_independent_of_current_pair() {
        let body = between(
            PLUGIN_EDITOR_CPP,
            "void KirinHyphaEditor::showCandidateMenu()",
            "void KirinHyphaEditor::handleCandidateMenu",
        );

        assert!(body.contains("const auto cands = processorRef.enumeratePreCandidates();"));
        assert!(body.contains("const auto claims = processorRef.enumeratePostPairClaims();"));
        assert!(
            body.contains("const juce::String currentPreInstanceId = resolvedOwnPreInstanceId (")
        );
        assert!(body.contains("ownInstanceId, processorRef.pairedPreInstanceId(), claims);"));
        assert!(body.contains("c.instanceId == currentPreInstanceId"));
        assert!(body.contains("const bool inUse = claimedByOtherPost"));
        assert!(
            body.contains("c.hasName && c.name.isNotEmpty()")
                && body.contains("c.instanceId.substring (0, 8)"),
            "unnamed PREs must remain independently selectable by an exact-id fallback label"
        );
        assert!(body.contains(
            "labels.add ((inUse ? \"In use: \" : (keepReady ? \"Keep ready: \" : \"Can Keep: \")) + shown);"
        ));
        assert!(body.contains("const bool keepReady = currentPreInstanceId.isNotEmpty()"));
        assert!(!body.contains("c.name == currentPairName"));
        assert!(body.contains("sameNameCount > 1"));
        assert!(body.contains("labelEnabled.add (! inUse);"));
        assert!(body.contains("labelChecked.add (keepReady && ! inUse);"));
        assert!(body.contains("const int nReady = processorRef.keepReadyCount();"));
        assert!(body.contains("processorRef.keepPhase() != (int) KIRIN_KEEP_PHASE_IDLE"));
        assert!(body.contains("if (! keepActive && processorRef.licenseIsOs() && nReady >= 1)"));
        assert!(body.contains("menu.addItem (1, allKeepMenuLabel (nReady));"));
        assert!(body.contains("menu.addItem (2, \"All Stop: active POSTs\");"));
        assert!(body.contains("menu.addSectionHeader (\"Pair choices (not Keep targets)\");"));
        assert!(body.contains("menu.addItem (3, \"No pair choices\", false, false);"));
        assert!(body.contains("! pairLocked && labelEnabled[i], labelChecked[i]"));
        assert!(
            !body.contains("All Keep ("),
            "old ambiguous All Keep label must not remain in the JUCE menu"
        );
        assert!(
            !body.contains("No candidates"),
            "old ambiguous empty label must not remain in the JUCE menu"
        );
        assert!(!body.contains("pairNonEmpty"));
    }

    #[test]
    fn juce_candidate_menu_owns_async_state_and_lifetimes() {
        let body = between(
            PLUGIN_EDITOR_CPP,
            "void KirinHyphaEditor::showCandidateMenu()",
            "void KirinHyphaEditor::handleCandidateMenu",
        );
        assert!(body.contains(".withDeletionCheck (*this)"));
        assert!(body.contains("juce::Component::SafePointer<KirinHyphaEditor> safeThis (this);"));
        assert!(body.contains("[safeThis, candidates = cands] (int result)"));
        assert!(body.contains("safeThis->handleCandidateMenu (result, candidates);"));
        assert!(PLUGIN_EDITOR_CPP.contains("static PairMenuLookAndFeel lookAndFeel;"));
        assert!(!PLUGIN_EDITOR_H.contains("PairMenuLookAndFeel       pairMenuLookAndFeel"));
        assert!(!PLUGIN_EDITOR_H.contains("menuCandidates;"));
    }

    #[test]
    fn candidate_selection_commits_exact_instance_and_updates_display_field() {
        let body = between(
            PLUGIN_EDITOR_CPP,
            "void KirinHyphaEditor::handleCandidateMenu (",
            "void KirinHyphaEditor::timerCallback()",
        );

        assert!(body.contains("processorRef.setPairCandidate (candidate.instanceId, name)"));
        assert!(body.contains("nameField.setModelName (name);"));
        assert!(body.contains("candidate.instanceId.substring (0, 8)"));
    }

    #[test]
    fn juce_keep_does_not_pre_reject_at_twelve_reservations() {
        let ctor_body = between(
            PLUGIN_EDITOR_CPP,
            "KirinHyphaEditor::KirinHyphaEditor",
            "KirinHyphaEditor::~KirinHyphaEditor",
        );
        let keep_body = between(ctor_body, "postControls->onKeep = [this] {", "};");

        assert!(
            !keep_body.contains("recordExclusionConflict()"),
            "JUCE Keep must not pre-check the 12-cap; FFI reserve->count>MAX is authoritative"
        );
        assert!(keep_body.contains("processorRef.keepPair()"));
        assert!(
            keep_body.contains("processorRef.recordErrorMessage()"),
            "Keep failure must surface the FFI cap/error message instead of falling through to No PRE"
        );
    }

    #[test]
    fn juce_all_keep_uses_authoritative_engine_result() {
        let body = between(
            PLUGIN_EDITOR_CPP,
            "void KirinHyphaEditor::handleCandidateMenu (",
            "void KirinHyphaEditor::timerCallback()",
        );
        let all_keep_body = between(body, "if (result == 1)", "else if (result == 2)");

        assert!(
            !all_keep_body.contains("recordExclusionConflict()"),
            "All Keep must not pre-check the 12-cap before the engine has resolved same-pair reservations"
        );
        assert!(all_keep_body.contains("processorRef.keepAll()"));
        assert!(
            all_keep_body.contains("processorRef.recordErrorMessage()"),
            "All Keep cap failure must use the FFI record_error_message"
        );
    }

    #[test]
    fn juce_entitlement_refresh_is_user_driven_and_never_a_periodic_disk_poll() {
        assert!(
            PLUGIN_PROCESSOR_CPP.contains("const int observed = (int) kirin_hypha_load_license();")
        );
        assert!(PLUGIN_PROCESSOR_CPP
            .contains("void KirinHyphaProcessorBase::refreshLicenseForUserAction()"));
        assert!(PLUGIN_PROCESSOR_CPP.contains("refreshLicenseForUserAction();"));
        assert!(!PLUGIN_PROCESSOR_CPP.contains("licenseRefreshTicks"));
        let timer = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::timerCallback()",
            "void KirinHyphaProcessorBase::enableWritesNow()",
        );
        assert!(!timer.contains("kirin_hypha_load_license"));
        assert!(PLUGIN_PROCESSOR_CPP
            .contains("kirin_hypha_set_license (hyphaHandle, (uint8_t) observed);"));
        assert!(PLUGIN_PROCESSOR_CPP
            .contains("cachedLicenseCode.load (std::memory_order_acquire) == 0"));
        assert!(PLUGIN_EDITOR_CPP.contains("Record requires Kirin OS license"));
    }

    #[test]
    fn juce_prepare_to_play_reuses_record_engine_for_same_format() {
        let body = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::prepareToPlay",
            "void KirinHyphaProcessorBase::releaseResources()",
        );

        assert!(body.contains("const bool needsNewHandle = hyphaHandle == nullptr"));
        assert!(body.contains("std::abs (preparedSampleRate - sampleRate) > 0.001"));
        assert!(body.contains("preparedInputChannels != numCh"));
        assert!(
            body.contains("if (! needsNewHandle)\n        return;"),
            "same-format reprepare must not destroy the Rust engine or Record state"
        );
        let reuse_gate = body.find("if (! needsNewHandle)").expect("reuse gate");
        let destroy = body.find("kirin_hypha_destroy").expect("destroy path");
        assert!(
            reuse_gate < destroy,
            "reuse gate must precede any destroy path"
        );
    }

    #[test]
    fn juce_release_resources_keeps_engine_alive_without_stop_authority() {
        let body = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::releaseResources()",
            "bool KirinHyphaProcessorBase::isBusesLayoutSupported",
        );

        assert!(
            !body.contains("kirin_hypha_destroy"),
            "releaseResources can be called around offline bounce and must not destroy the engine"
        );
        assert!(
            !body.contains("stopPair()") && !body.contains("kirin_hypha_stop"),
            "releaseResources is host lifecycle, not Record Stop authority"
        );
        assert!(body.contains("offline bounce/freeze"));
    }

    include!("shell_parity/runtime_tests.rs");
}
