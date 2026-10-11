//! Shipping callers must carry the accepted comparison through a partial absolute update.
#[path = "support/juce_lifecycle_sources.rs"]
mod sources;

#[test]
fn shipping_level_polls_seed_the_actual_ffi_output_with_the_accepted_frame() {
    let editor = sources::read_repo("juce_shell/src/PluginEditorObservatory.cpp");
    assert!(editor.contains("KirinObservatoryFrame frame = observatoryView.frameForPoll();"));
    assert!(editor.contains("levelSnapshot.frame = frame;"));
    let processor = sources::read_repo("juce_shell/src/PluginProcessorMeter.cpp");
    assert!(processor.contains("KirinLevelSnapshot next = out;"));
    assert!(processor.contains("chain.data(), chainCapacity, &next)"));
}
