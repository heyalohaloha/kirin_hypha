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

    #[test]
    fn candidate_menu_enumerates_pre_candidates_independent_of_current_pair() {
        let body = between(
            PLUGIN_EDITOR_CPP,
            "void KirinHyphaEditor::showCandidateMenu()",
            "void KirinHyphaEditor::handleCandidateMenu",
        );

        assert!(body.contains("const auto cands = processorRef.enumeratePreCandidates();"));
        assert!(body.contains("if (c.hasName && c.name.isNotEmpty())"));
        assert!(body.contains("const int nReady = processorRef.keepReadyCount();"));
        assert!(body.contains("if (! rec && nReady >= 1)"));
        assert!(!body.contains("pairName"));
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
    fn juce_release_resources_does_not_drop_record_state() {
        let body = between(
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::releaseResources()",
            "bool KirinHyphaProcessorBase::isBusesLayoutSupported",
        );

        assert!(
            !body.contains("kirin_hypha_destroy"),
            "releaseResources can be called around offline bounce and must not exit Record"
        );
        assert!(body.contains("offline bounce/freeze"));
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
}
