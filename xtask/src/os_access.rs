#[cfg(test)]
mod tests {
    const PROCESSOR: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessor.cpp"
    ));
    const GUIDE: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessorGuideTransport.cpp"
    ));
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
            "void KirinHyphaProcessorBase::getStateInformation",
        );
        assert!(process.contains("! nonRealtimeMode && licenseIsOs()"));
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
                "bool KirinHyphaProcessorBase::revealReferenceBlind",
            ),
            (
                "bool KirinHyphaProcessorBase::revealReferenceBlind",
                "void KirinHyphaProcessorBase::endReferenceBlind",
            ),
        ] {
            let action = body(GUIDE, start, end);
            assert!(action.contains("refreshLicenseForUserAction();"));
            assert!(action.contains("if (! licenseIsOs())"));
        }
        assert!(
            EDITOR_OBSERVATORY.contains("observatoryView.setReferenceEnabled (referenceEnabled)")
        );
        assert!(EDITOR_OBSERVATORY.contains("Domain::reference && ! processorRef.licenseIsOs()"));
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
}
