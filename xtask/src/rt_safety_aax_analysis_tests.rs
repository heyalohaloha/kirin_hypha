const MEASUREMENT_INGRESS_CPP: &str =
    include_str!("../../juce_shell/src/PluginProcessorMeasurementIngress.cpp");
const OPTIONAL_ANALYSIS_CLOCK_RS: &str =
    include_str!("../../crates/kirin_hypha_ffi/src/optional_analysis_clock.rs");

#[test]
fn process_block_calls_only_rt_safe_ffi_surface() {
    let mut calls = BTreeSet::new();
    for (source, signature) in [
        (
            PLUGIN_PROCESSOR_CPP,
            "void KirinHyphaProcessorBase::processBlock",
        ),
        (AUDITION_OUTPUT_CPP, PROCESS_COMPARISON_SIGNATURE),
        (
            MEASUREMENT_INGRESS_CPP,
            "bool KirinHyphaProcessorBase::feedMeasurementInput",
        ),
        (
            MEASUREMENT_INGRESS_CPP,
            "void KirinHyphaProcessorBase::pushPairedAnalysis",
        ),
    ] {
        calls.extend(ffi_calls(&function_body(source, signature)));
    }
    let expected = BTreeSet::from(
        [
            "kirin_hypha_get_signal_state",
            "kirin_hypha_is_recording",
            "kirin_hypha_note_capture_window",
            "kirin_hypha_note_capture_loop_wrap",
            "kirin_hypha_note_transport_block",
            "kirin_hypha_note_record_window",
            "kirin_hypha_note_oversized_drop",
            "kirin_hypha_push_samples",
            "kirin_hypha_set_signal_state",
            "kirin_hypha_push_paired_analysis",
        ]
        .map(str::to_string),
    );
    assert_eq!(calls, expected);
}

#[test]
fn aax_analysis_callees_are_rt_safe_and_consume_original_input_after_timing() {
    for (source, signature) in [
        (
            MEASUREMENT_INGRESS_CPP,
            "bool KirinHyphaProcessorBase::feedMeasurementInput",
        ),
        (
            MEASUREMENT_INGRESS_CPP,
            "void KirinHyphaProcessorBase::pushPairedAnalysis",
        ),
        (
            OPTIONAL_ANALYSIS_CLOCK_RS,
            "pub(super) fn push_optional_analysis",
        ),
        (
            OPTIONAL_ANALYSIS_CLOCK_RS,
            "pub unsafe extern \"C\" fn kirin_hypha_push_paired_analysis",
        ),
    ] {
        let body = function_body(source, signature);
        for forbidden in [
            "std::fs",
            "StoragePaths",
            "File::",
            ".lock(",
            ".try_lock(",
            "Vec::",
            "Box::",
            "String::",
            "format!",
            "thread::",
            "spawn",
            "sleep",
            "join",
            "getArrayOfWritePointers",
            "setSample (",
        ] {
            assert!(
                !body.contains(forbidden),
                "{signature} must not contain {forbidden}"
            );
        }
    }
    let callback = function_body(
        PLUGIN_PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processBlock",
    );
    assert!(
        callback.find("feedMeasurementInput (").unwrap()
            < callback.find(PROCESS_COMPARISON_CALL).unwrap()
    );
    assert!(
        callback.find(PROCESS_COMPARISON_CALL).unwrap()
            < callback.find("pushPairedAnalysis (").unwrap()
    );
    let late = function_body(
        MEASUREMENT_INGRESS_CPP,
        "void KirinHyphaProcessorBase::pushPairedAnalysis",
    );
    assert!(late.contains("interleaveScratch.data()"));
    assert!(!late.contains("kirin_hypha_push_samples"));
    let ffi = function_body(
        OPTIONAL_ANALYSIS_CLOCK_RS,
        "pub unsafe extern \"C\" fn kirin_hypha_push_paired_analysis",
    );
    assert!(ffi.contains("frames.swap(0"));
    assert!(ffi.contains("staged != num_frames"));
    assert!(!ffi.contains("write_role"));
}
