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
    const PLUGIN_PROCESSOR_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessor.cpp"
    ));

    fn between<'a>(source: &'a str, start: &str, end: &str) -> &'a str {
        let start_index = source.find(start).expect(start) + start.len();
        let tail = &source[start_index..];
        let end_index = tail.find(end).expect(end);
        &tail[..end_index]
    }

    #[test]
    fn post_controls_keep_visibility_depends_on_selected_pair() {
        assert!(POST_CONTROLS_CPP.contains(
            "void PostControls::update (bool recording, int license, bool pairNonEmpty)"
        ));
        assert!(
            POST_CONTROLS_CPP.contains("keepBtn  .setVisible (! recording && os && pairNonEmpty);")
        );
        assert!(!POST_CONTROLS_CPP.contains("keepBtn  .setVisible (! recording && os);"));
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
            "void PostControls::update (bool recording, int license, bool pairNonEmpty)",
            "void PostControls::resized()",
        );
        assert!(body.contains("const bool os    = (license == 0);"));
        assert!(body.contains("const bool sense = (license == 1);"));
        assert!(body.contains("keepBtn  .setVisible (! recording && os && pairNonEmpty);"));
        assert!(body.contains("senseBtn .setVisible (! recording && sense);"));
        assert!(body.contains("stopBtn  .setVisible (recording && os && ! notePickerOpen);"));
        assert!(body.contains("noteBtn  .setVisible (recording && os && ! notePickerOpen);"));
        assert!(body.contains("const bool picker = recording && os && notePickerOpen;"));
        assert!(body.contains("goodBtn  .setVisible (picker);"));
        assert!(body.contains("fixBtn   .setVisible (picker);"));
        assert!(body.contains("holdBtn  .setVisible (picker);"));
        assert!(body.contains("cancelBtn.setVisible (picker);"));
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
        assert!(body.contains("const juce::String currentPairName = processorRef.pairName();"));
        assert!(body.contains("if (c.hasName && c.name.isNotEmpty())"));
        assert!(body.contains("const bool keepReady = (c.name == currentPairName);"));
        assert!(body.contains("const bool inUse = claimedByOtherPost"));
        assert!(body.contains("const bool duplicateName = hasDuplicateCandidateName"));
        assert!(
            body.contains("duplicateName ? \"Duplicate: \"")
                && body.contains(
                    "inUse ? \"In use: \" : (keepReady ? \"Keep ready: \" : \"Can Keep: \")"
                )
        );
        assert!(body.contains("labels.add (prefix + c.name);"));
        assert!(body.contains("labelEnabled.add (! duplicateName && ! inUse);"));
        assert!(body.contains("labelChecked.add (! duplicateName && keepReady && ! inUse);"));
        assert!(
            !body.contains("instanceId.substring (0, 8)"),
            "candidate rows must hide instance_id in normal POST menu display"
        );
        assert!(body.contains("const int nReady = processorRef.keepReadyCount();"));
        assert!(body.contains("if (! rec && nReady >= 1)"));
        assert!(body.contains("menu.addItem (1, allKeepMenuLabel (nReady));"));
        assert!(body.contains("menu.addItem (2, \"All Stop: recording POSTs\");"));
        assert!(
            body.contains("menu.addItem (4, \"Pair choices (not Keep targets)\", false, false);")
        );
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
    fn candidate_selection_commits_pair_name_to_processor_and_field() {
        let body = between(
            PLUGIN_EDITOR_CPP,
            "void KirinHyphaEditor::handleCandidateMenu (int result)",
            "void KirinHyphaEditor::timerCallback()",
        );

        assert!(body.contains("processorRef.setPairName (name);"));
        assert!(body.contains("nameField.setModelName (name);"));
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
            "void KirinHyphaEditor::handleCandidateMenu (int result)",
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
    fn juce_release_resources_keeps_engine_alive_while_polling_offline_end() {
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
            body.contains("maybeAutoStopOnOfflineEnd();"),
            "releaseResources must delegate offline-end auto-stop to the edge gate"
        );
        assert!(body.contains("offline bounce/freeze"));
    }

    #[test]
    fn juce_offline_end_auto_stop_is_post_recording_edge_gated() {
        let prepare = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::prepareToPlay",
            "void KirinHyphaProcessorBase::releaseResources()",
        );
        assert!(
            prepare.contains("maybeAutoStopOnOfflineEnd();"),
            "prepareToPlay must poll offline-end auto-stop before normal prepare work"
        );

        let body = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::maybeAutoStopOnOfflineEnd()",
            "// --- B-073: POST",
        );
        assert!(body.contains("const bool offlineJustEnded = prevNonRealtime && ! nowNonRealtime;"));
        assert!(body.contains("prevNonRealtime = nowNonRealtime;"));
        assert!(
            body.contains("if (! offlineJustEnded || ! isPostRole())"),
            "offline-end auto-stop must be edge-gated and POST-only"
        );
        assert!(
            body.contains("kirin_hypha_is_recording"),
            "offline-end auto-stop must only call Stop when Record/Keep is active"
        );
        assert!(
            body.contains("if (recording)\n        stopPair();"),
            "offline-end auto-stop must reuse manual Stop cleanup"
        );
    }

    #[test]
    fn juce_post_record_display_keeps_six_metrics_before_signal_fallback() {
        let start = PLUGIN_EDITOR_CPP
            .find("void KirinHyphaEditor::updatePost()")
            .expect("updatePost");
        let body = &PLUGIN_EDITOR_CPP[start..];
        let record_branch = body.find("if (rec)").expect("record branch");
        let signal_branch = body.find("else if (sig != 1)").expect("signal fallback");

        assert!(
            record_branch < signal_branch,
            "POST Record must keep Delta6/N/Sharp visible even if the host goes inactive during bounce"
        );
        assert!(body.contains("const bool haveD = (sig == 1) && processorRef.pollDelta (d);"));
    }

    #[test]
    fn juce_post_watch_delta_requires_active_pre_mode() {
        let start = PLUGIN_EDITOR_CPP
            .find("void KirinHyphaEditor::updatePost()")
            .expect("updatePost");
        let body = &PLUGIN_EDITOR_CPP[start..];
        let watch_branch = body.find("else // Active + Watch").expect("watch branch");
        let window = &body[watch_branch..body.len().min(watch_branch + 1400)];

        assert!(
            window.contains("const bool showDelta = pairNonEmpty && haveD && d.mode == 0;"),
            "JUCE POST Watch must show delta only for DeltaMode::Active"
        );
        assert!(
            !window.contains("d.mode == 1"),
            "JUCE POST Watch must not treat DeltaMode::Stale as a delta display state"
        );
        assert!(
            window.contains("pair empty / Stale / NoPre / no delta -> POST absolute"),
            "JUCE POST Watch must document Stale fallback to POST absolute values"
        );
    }
}
