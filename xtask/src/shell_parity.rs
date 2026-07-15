#[cfg(test)]
mod tests {
    const POST_CONTROLS_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PostControls.cpp"
    ));
    const PLUGIN_EDITOR_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditor.cpp"
    ));
    const PLUGIN_EDITOR_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditor.h"
    ));
    const PLUGIN_PROCESSOR_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessor.cpp"
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
    const HYPHA_UI_CONTRACT_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaUiContract.h"
    ));
    const HYPHA_DISPLAY_CONTRACT_H: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/HyphaDisplayContract.h"
    ));

    fn read_juce_au_wrapper() -> Option<String> {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(
            "../juce_shell/JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_AU_1.mm",
        );
        std::fs::read_to_string(path).ok()
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
            "watchMaximum.lufs_m",
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
    fn shipped_vst3_and_au_use_one_native_font_contract() {
        assert!(HYPHA_THEME_H.contains("ui_contract::labelFontFamily"));
        assert!(HYPHA_THEME_H.contains("ui_contract::monoFontFamily"));
        assert!(HYPHA_UI_CONTRACT_H.contains("labelFontFamily = \".SF NS\""));
        assert!(HYPHA_UI_CONTRACT_H.contains("monoFontFamily  = \".SF NS Mono\""));
    }

    #[test]
    fn shipped_post_pair_selector_has_one_geometry_source() {
        assert!(
            PLUGIN_EDITOR_CPP.contains("const auto layout = ui::editorLayout (isPost, getWidth())")
        );
        assert!(PLUGIN_EDITOR_CPP.contains("nameField.setBounds (juceRect (layout.name))"));
        assert!(
            PLUGIN_EDITOR_CPP.contains("pairDropdown.setBounds (juceRect (layout.pairDropdown))")
        );
        assert!(!PLUGIN_EDITOR_CPP.contains("const int ddW = 22;"));
        assert!(HYPHA_UI_CONTRACT_H.contains("constexpr int pairDropdownWidth = 28"));
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
}
