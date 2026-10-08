#[test]
fn capture_freezes_the_authoritative_frame_before_the_async_save_panel() {
    let freeze = cpp_body(
        PLUGIN_EDITOR_CAPTURE_CPP,
        "KirinHyphaEditor::freezeObservatoryCapture (",
    );
    let attach = cpp_body(
        PLUGIN_EDITOR_CAPTURE_CPP,
        "KirinHyphaEditor::attachObservatoryCapture (",
    );
    let choose = cpp_body(
        PLUGIN_EDITOR_CAPTURE_CPP,
        "KirinHyphaEditor::chooseObservatoryCapture (",
    );
    let save = cpp_body(
        PLUGIN_EDITOR_CAPTURE_CPP,
        "KirinHyphaEditor::saveFrozenObservatoryCapture (",
    );
    let expiry = freeze
        .find("observatoryView.advanceTimePresentation")
        .expect("apply current expiry");
    let stamp = freeze
        .find("snapshot.stamp = observatoryView.capturePresentationStamp()")
        .expect("adopted meaning stamp");
    let image = freeze
        .find("snapshot.image = observatoryView.createCaptureImage")
        .expect("freeze adopted visual facts");
    assert!(
        expiry < stamp && stamp < image,
        "expire the adopted presentation before metadata and PNG freeze"
    );
    assert!(save.contains("captureChooser->launchAsync"));
    assert!(save.contains("[safeThis, image = snapshot.image]"));
    assert!(freeze.contains("attackView.advanceCapturePresentationAt"));
    assert!(
        !save.contains("freezeObservatoryCapture"),
        "an asynchronous save must not refreeze"
    );
    assert!(
        choose.contains("saveFrozenObservatoryCapture (freezeObservatoryCapture (width, height))")
    );
    assert_eq!(count_occurrences(attach, "freezeObservatoryCapture"), 1);
    for source in [freeze, attach, save] {
        for forbidden in [
            "pollMeterHistory",
            "pollDeltaHistory",
            "pollMeterSession",
            "pollTimeSnapshot",
            "setPairCandidate",
            "setHistory (",
            "presentationTickAt",
            "capture_history::retainThrough",
        ] {
            assert!(
                !source.contains(forbidden),
                "Capture must use the adopted facts without a new acquisition: {forbidden}"
            );
        }
    }
    for required in [
        "frame.timePresentation = timePresentation;",
        "frame.sessionCoverage = sessionCoverage;",
        "frame.haveSessionCoverage = haveSessionCoverage;",
        "frame.levelInspection = levelInspection;",
        "frame.history = historySnapshot != nullptr ? *historySnapshot : history;",
    ] {
        assert!(
            HYPHA_OBSERVATORY_CAPTURE_CPP.contains(required),
            "Capture must preserve the accepted source: {required}"
        );
    }
    assert!(
        attach.contains("descriptor.v1MeaningPreserved = ! snapshot.stamp.requiresTypedMetadata;")
    );
    let unsupported = between(
        attach,
        "else if (submitted == hypha::capture::WorkAttachmentSubmit::unsupportedPresentation)",
        "\n    else",
    );
    assert!(
        unsupported.contains("Work cannot preserve this observation; save the local PNG instead")
    );
    assert!(unsupported.contains("saveFrozenObservatoryCapture (snapshot);"));
    assert!(!unsupported.contains("freezeObservatoryCapture"));
    let controller = include_str!("../../../juce_shell/src/CaptureWorkAttachment.cpp");
    let meaning = controller
        .find("if (! descriptor.v1MeaningPreserved)")
        .expect("v1 meaning validation");
    let request = controller
        .find("job->descriptor =")
        .expect("accepted job takes the descriptor");
    assert!(
        meaning < request,
        "unsupported meaning must fail before publishing a request"
    );
    for forbidden in [
        "instanceId()",
        "pairedPreInstanceId",
        "persistProjectUuid",
        "persistDawSessionUuid",
        ".workId",
        ".bindingId",
    ] {
        assert!(
            !PLUGIN_EDITOR_CAPTURE_CPP.contains(forbidden),
            "Capture must not export implementation identity: {forbidden}"
        );
    }
    assert_eq!(
        count_occurrences(
            PLUGIN_EDITOR_CAPTURE_CPP,
            "observatoryView.createCaptureImage"
        ),
        1
    );
}
