#[path = "support/juce_lifecycle_sources.rs"]
mod sources;
use sources::read_repo;

#[test]
fn vu_calibration_is_a_shared_display_choice_outside_audio_and_measurement() {
    let processor = read_repo("juce_shell/src/PluginProcessorDisplayState.cpp");
    assert!(processor.contains("kirin_hypha_get_vu_calibration_locator"));
    assert!(processor.contains("std::array<char, 65> project {}, instance {}"));
    assert!(processor.contains("scope == expectedScope"));
    assert!(processor.contains("isThisTheMessageThread()"));
    let realtime = read_repo("juce_shell/src/PluginProcessor.cpp");
    assert!(!realtime.contains("refreshHybridVuCalibration"));
    assert!(!realtime.contains("setHybridVuCalibration"));
    let state = read_repo("juce_shell/src/PluginProcessorState.cpp");
    assert!(!state.contains("hybridVuCalibration"));
    assert!(!state.contains("vu_calibration"));
    let painter = read_repo("juce_shell/src/HyphaHybridVuPainter.cpp");
    assert!(painter.contains("dbfs - reference"));
    assert!(painter.contains("quiet_NaN(), state.calibrationDbfs"));
    assert!(painter.contains("channel_instant_true_peak_dbtp[channel]"));
    let measure = read_repo("crates/kirin_measure/src/stereo_meter.rs");
    assert!(measure.contains("300"));
}

#[test]
fn vu_calibration_preserves_exact_scope_and_uses_bounded_atomic_preferences() {
    let preference = read_repo("juce_shell/src/HyphaVuCalibrationPreference.h");
    assert!(preference.contains("juce::SHA256"));
    assert!(preference.contains("refreshMs = 250"));
    assert!(preference.contains("if (scope.isEmpty()) return cachedValue"));
    assert!(preference.contains("size > 256"));
    assert!(preference.contains("text == encoded (scope, choice)"));
    let file = read_repo("juce_shell/src/HyphaVuCalibrationFile.h");
    assert!(file.contains("juce::TemporaryFile temporary (target"));
    assert!(file.contains("stream->write (text.toRawUTF8(), bytes)"));
    assert!(file.contains("stream->flush()"));
    assert!(file.contains("stream->getStatus().wasOk()"));
    assert!(file.contains("temporary.getFile().loadFileAsString() != text"));
    assert!(file.contains("replaceFileIn (target)"));
    assert!(!file.contains("replaceWithText"));
    let editor = read_repo("juce_shell/src/PluginEditorObservatory.cpp");
    assert!(editor.contains("scope != safe->processorRef.hybridVuCalibrationScope()"));
    assert!(editor.contains("SafePointer<KirinHyphaEditor>"));
    let capture = read_repo("juce_shell/src/HyphaObservatoryCapture.cpp");
    assert!(capture.contains("frame.setVuCalibration (vuCalibration(), false)"));
    assert!(!capture.contains("refreshHybridVuCalibration"));
}

#[test]
fn vu_calibration_controls_only_its_existing_legend_and_has_translated_failures() {
    let control = read_repo("juce_shell/src/HyphaVuCalibrationControl.h");
    assert!(control.contains("observatory-vu-calibration"));
    assert!(control.contains("typography::TextRole::legend"));
    assert!(control.contains("isEnabled() &&"));
    let choices = read_repo("juce_shell/src/HyphaVuCalibration.h");
    assert!(choices.contains("{ -12, -14, -16, -18, -20 }"));
    assert!(choices.contains("defaultDbfs = -18"));
    let japanese = read_repo("juce_shell/src/HyphaJapaneseObservatory.cpp");
    assert!(japanese.contains("Chain unavailable; previous VU setting retained."));
    assert!(japanese.contains("VU calibration could not be saved. Previous setting retained."));
}
