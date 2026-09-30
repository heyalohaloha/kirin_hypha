use super::{function_body, PROCESSOR_CPP};

#[test]
fn one_pass_blind_uses_exact_receipts_and_end_waits_for_real_unity() {
    let trial = include_str!("../../juce_shell/src/live_compare/LiveBlindSession.h");
    for signature in ["void observe (", "void invalidate ("] {
        let body = function_body(trial, signature);
        for forbidden in ["new ", "make_unique", "mutex", "sleep", "secureRandomBit"] {
            assert!(!body.contains(forbidden), "RT receipt contains {forbidden}");
        }
    }
    assert!(trial.contains("command().word != cmd.word"));
    assert!(trial.contains("state.played != 3"));
    let rt = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    assert!(rt.contains("report.stableSource && report.gainSettled"));
    assert!(rt.contains("if (finishing && usable && ! outputTaken && frames > 0)"));
    let owner = include_str!("../../juce_shell/src/PluginProcessorLiveBlind.cpp");
    assert!(owner.contains("kirin_hypha_begin_local_blind (hyphaHandle, &epoch)"));
    assert!(
        owner.contains("liveCompare.blind.startWith (hypha::reference_audition::secureRandomBit)")
    );
    let editor = include_str!("../../juce_shell/src/PluginEditorLiveBlind.cpp");
    let open = function_body(editor, "void KirinHyphaEditor::openLiveBlind");
    assert!(
        open.find("layoutLocalBlindProduct();").unwrap()
            < open.find("refreshLiveBlind();").unwrap()
    );
}
