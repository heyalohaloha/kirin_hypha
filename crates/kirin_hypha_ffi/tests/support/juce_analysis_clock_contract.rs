use super::{read_repo, slice_between};

#[test]
fn deferred_aax_analysis_serves_every_aax_host_and_only_the_paired_mode() {
    // AAX's configuration is independent of certification; only Perceptual is staged for
    // it (LIVE / absolute PSB keep the project timeline). The wrapper accepts a certified K
    // within its bound, or LISTEN's linear K outside a loop, and never a negative K.
    let source = read_repo("juce_shell/src/PluginProcessorFormat.cpp");
    let binding = slice_between(
        &source,
        "kirin_hypha_set_aax_analysis_ingress (hyphaHandle,",
        "kirin_hypha_set_chain_clock_policy",
    );
    assert!(
        binding.contains("wrapperType_AAX, role == Role::Post") && !binding.contains("bounded")
    );
    assert!(source.contains("liveCompare.maximumDelaySamples = liveClock.maximumDelaySamples;"));
    let ingress = read_repo("crates/kirin_hypha_ffi/src/optional_analysis_clock.rs");
    let staging = slice_between(
        &ingress,
        "pub(super) fn push_optional_analysis(",
        "if let Some(runtime) = self.attack_runtime.as_ref()",
    );
    assert!(staging.contains(".deferred(self.spectrum_runtime.analysis_mode())"));
    let policy = slice_between(&ingress, "fn deferred(", "pub(super) fn begin_callback");
    assert!(policy.contains("mode == AnalysisViewMode::Perceptual"));
    assert!(policy
        .contains("self.content.load(Ordering::Acquire) && mode == AnalysisViewMode::Spectrum"));
    assert!(!staging.contains("AnalysisViewMode::Absolute"));
    let clock = read_repo("juce_shell/src/live_compare/AaxAnalysisClock.h");
    assert!(clock.contains("if (certifiedAaxClock (pre) && certifiedAaxClock (post))"));
    assert!(clock.contains("evidence.kind == LoopEntryKind::linear && ! post.loop.active"));
    assert!(clock.contains("|| evidence.k < 0)"));
}
