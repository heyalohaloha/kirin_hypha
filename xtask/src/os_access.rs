#[cfg(test)]
mod tests {
    use crate::rt_contract_surface::PROCESS_COMPARISON_CALL;

    const PROCESSOR: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessor.cpp"
    ));
    const GUIDE: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessorGuideTransport.cpp"
    ));
    const REFERENCE: &str = include_str!("../../juce_shell/src/PluginProcessorReference.cpp");
    const EDITOR_OBSERVATORY: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditorObservatory.cpp"
    ));
    const EDITOR_CAPTURE: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginEditorCapture.cpp"
    ));

    fn body<'a>(source: &'a str, start: &str, end: &str) -> &'a str {
        let tail = source
            .split_once(start)
            .unwrap_or_else(|| panic!("missing {start}"))
            .1;
        tail.split_once(end)
            .unwrap_or_else(|| panic!("missing {end}"))
            .0
    }

    #[test]
    fn reference_has_ui_action_and_audio_thread_entitlement_gates() {
        let process = body(
            PROCESSOR,
            "void KirinHyphaProcessorBase::processBlock",
            "bool KirinHyphaProcessorBase::acceptsMidi",
        );
        assert!(process.contains(PROCESS_COMPARISON_CALL));
        let output = include_str!("../../juce_shell/src/PluginProcessorAudition.cpp");
        assert!(output.contains("! nonRealtimeMode && licenseIsOs()"));
        let snapshot = body(
            REFERENCE,
            "KirinHyphaProcessorBase::referenceAuditionSnapshot",
            "bool KirinHyphaProcessorBase::selectReferenceB",
        );
        assert!(!snapshot.contains("licenseIsOs()"));
        for (start, end) in [
            (
                "bool KirinHyphaProcessorBase::selectReferenceB",
                "void KirinHyphaProcessorBase::selectReferenceA",
            ),
            (
                "bool KirinHyphaProcessorBase::startReferenceBlind",
                "bool KirinHyphaProcessorBase::selectReferenceBlindStimulus",
            ),
            (
                "bool KirinHyphaProcessorBase::selectReferenceBlindStimulus",
                "bool KirinHyphaProcessorBase::approveReferenceBlindLowerA",
            ),
            (
                "bool KirinHyphaProcessorBase::approveReferenceBlindLowerA",
                "bool KirinHyphaProcessorBase::answerReferenceBlind",
            ),
            (
                "bool KirinHyphaProcessorBase::answerReferenceBlind",
                "bool KirinHyphaProcessorBase::revealReferenceBlind",
            ),
            (
                "bool KirinHyphaProcessorBase::revealReferenceBlind",
                "void KirinHyphaProcessorBase::endReferenceBlind",
            ),
        ] {
            let action = body(REFERENCE, start, end);
            assert!(action.contains("refreshLicenseForUserAction();"));
            assert!(action.contains("if (! licenseIsOs())"));
            assert!(action.contains("suspendAudition();"));
        }
        assert!(EDITOR_OBSERVATORY.contains("observatoryView.setReferenceOwned (referenceOwned)"));
        assert!(!EDITOR_OBSERVATORY.contains("Domain::reference && ! processorRef.licenseIsOs()"));
        let reference = include_str!("../../juce_shell/src/PluginEditorReference.cpp");
        assert!(reference.contains("hypha::reference_ui::needsAccessPanel (referenceView.state())"));
        assert!(reference.contains("processorRef.refreshLicenseForUserAction();"));
        assert!(reference.contains("safe->handleInformationMenu (selected)"));
        assert!(reference.contains("hypha::reference_ui::runtimeStatus (runtime, state)"));
        let status = body(
            include_str!("../../juce_shell/src/HyphaReferenceRuntimeStatus.cpp"),
            "using Runtime = hypha::reference_audition::RuntimeState;",
            "if (runtime.presetSelectionStatus == \"pending\")",
        );
        assert!(
            status.find("BlindPhase::invalidated").unwrap()
                < status.find("state.osAccess == Access::unowned").unwrap(),
            "an active return contract must outrank the access status"
        );
    }

    #[test]
    fn guide_and_work_capture_are_entitlement_gated_without_limiting_local_capture() {
        for snapshot in [
            "preDisplaySnapshot",
            "guidePresentationSnapshot",
            "pendingPreDisplayConnection",
            "connectedWorkReference",
        ] {
            let start = format!("KirinHyphaProcessorBase::{snapshot}");
            let source = GUIDE
                .split_once(&start)
                .unwrap_or_else(|| panic!("missing {snapshot}"))
                .1;
            assert!(source
                .lines()
                .take(8)
                .collect::<String>()
                .contains("licenseIsOs()"));
        }
        for action in ["acceptPreDisplayConnection", "attachCaptureToWork"] {
            let start = format!("KirinHyphaProcessorBase::{action}");
            let source = GUIDE
                .split_once(&start)
                .unwrap_or_else(|| panic!("missing {action}"))
                .1;
            let prefix = source.lines().take(12).collect::<String>();
            assert!(prefix.contains("refreshLicenseForUserAction();"));
            assert!(prefix.contains("! licenseIsOs()"));
        }
        assert!(EDITOR_CAPTURE.contains("Attach to Work - Kirin OS required"));
        assert!(EDITOR_CAPTURE.contains("osOwned && guideAvailable"));
        assert!(EDITOR_CAPTURE.contains("chooseObservatoryCapture"));
    }
    // Cover the path-owning production modules, independent of how a test includes or loads
    // processors (headers and dynamic plug-in modules must be isolated too).
    #[test]
    fn all_juce_kirin_os_defaults_honor_the_shared_test_root() {
        let mut pending =
            vec![std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../juce_shell/src")];
        let mut checked = 0;
        while let Some(directory) = pending.pop() {
            for entry in std::fs::read_dir(directory).expect("source directory") {
                let path = entry.expect("source entry").path();
                if path.is_dir() {
                    pending.push(path);
                    continue;
                }
                if !matches!(path.extension().and_then(|e| e.to_str()), Some("cpp" | "h")) {
                    continue;
                }
                let source = std::fs::read_to_string(&path).expect("UTF-8 source");
                if source.contains("getSpecialLocation")
                    && source.contains("Kirin OS")
                    && source.contains("plugin_data")
                {
                    checked += 1;
                    assert!(
                        source.contains("storage::testPluginDataRoot()"),
                        "{} resolves Kirin OS storage without the shared test root",
                        path.display()
                    );
                }
            }
        }
        assert!(checked >= 6, "all Kirin OS path owners are covered");
        let sandbox = include_str!("../../juce_shell/tests/ValidationStorageSandbox.h");
        assert!(sandbox.contains("redirect (\"KIRIN_HYPHA_TEST_STORAGE_ROOT\", root)"));
        assert!(!sandbox.contains("clear (\"KIRIN_HYPHA_TEST_STORAGE_ROOT\")"));
        for directory in ["home/AppData/Roaming", "home/AppData/Local", "tmp", "home"] {
            assert!(sandbox.contains(&format!("getChildFile (\"{directory}\")")));
        }
    }
}
