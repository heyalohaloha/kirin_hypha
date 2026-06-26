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
}
