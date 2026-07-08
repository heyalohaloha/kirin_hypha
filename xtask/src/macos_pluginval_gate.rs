const CI_WORKFLOW: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../.github/workflows/ci.yml"
));
const PLUGINVAL_SCRIPT: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../scripts/validate_juce_pluginval_macos.sh"
));
const README: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/../README.md"));

#[test]
fn macos_ci_runs_pluginval_before_manual_daw_validation() {
    let auval_job = section_after(CI_WORKFLOW, "  auval-arm64:\n");
    require(
        auval_job,
        "Validate macOS JUCE VST3 with pluginval",
        "macOS CI must validate JUCE VST3 bundles with pluginval",
    );
    require(
        auval_job,
        "scripts/validate_juce_pluginval_macos.sh juce_shell/build-arm64",
        "macOS CI must use the shared local pluginval script",
    );
    require(
        auval_job,
        "Upload macOS pluginval logs",
        "macOS CI must retain pluginval logs for diagnosis",
    );

    let build_idx = auval_job
        .find("Configure + build JUCE shells")
        .expect("macOS CI must build JUCE shells");
    let pluginval_idx = auval_job
        .find("Validate macOS JUCE VST3 with pluginval")
        .expect("macOS CI must run pluginval");
    let auval_idx = auval_job
        .find("auval PRE")
        .expect("macOS CI must keep AU validation");
    assert!(
        build_idx < pluginval_idx && pluginval_idx < auval_idx,
        "pluginval should run after JUCE build and before AU install/auval"
    );
}

#[test]
fn macos_pluginval_script_is_a_reusable_strict_preflight_gate() {
    require(
        PLUGINVAL_SCRIPT,
        "PLUGINVAL_VERSION=\"${PLUGINVAL_VERSION:-v1.0.4}\"",
        "pluginval version must be pinned by default for reproducible CI",
    );
    require(
        PLUGINVAL_SCRIPT,
        "PLUGINVAL_STRICTNESS_LEVEL:-5",
        "pluginval strictness level 5 must be the default gate",
    );
    require(
        PLUGINVAL_SCRIPT,
        "releases/download/${PLUGINVAL_VERSION}/pluginval_macOS.zip",
        "script must download the official macOS pluginval binary",
    );
    require(
        PLUGINVAL_SCRIPT,
        "--validate-in-process",
        "script must use pluginval's in-process validation mode on macOS",
    );
    require(
        PLUGINVAL_SCRIPT,
        "PLUGINVAL_TIMEOUT_MS:-120000",
        "script must use a bounded timeout suitable for CI",
    );
    require(
        PLUGINVAL_SCRIPT,
        "--vst3validator \"$VST3_VALIDATOR_BIN\"",
        "script must allow Steinberg VST3 validator injection when available",
    );
    require(
        PLUGINVAL_SCRIPT,
        "--output-dir \"$OUTPUT_DIR\"",
        "script must write pluginval logs to a retained output directory",
    );
    require(
        PLUGINVAL_SCRIPT,
        "expected exactly 2 JUCE VST3 bundles",
        "script must fail if PRE/POST VST3 artifacts are missing or duplicated",
    );
}

#[test]
fn readme_exposes_the_local_pluginval_gate() {
    require(
        README,
        "scripts/validate_juce_pluginval_macos.sh juce_shell/build-universal",
        "README must tell maintainers how to run the local macOS pluginval gate",
    );
    require(
        README,
        "Studio One",
        "README should frame pluginval as the gate before manual DAW validation",
    );
}

fn section_after<'a>(source: &'a str, marker: &str) -> &'a str {
    source
        .split_once(marker)
        .map(|(_, tail)| tail)
        .unwrap_or_else(|| panic!("missing section marker `{marker}`"))
}

fn require(source: &str, needle: &str, message: &str) {
    assert!(source.contains(needle), "{message}: missing `{needle}`");
}
