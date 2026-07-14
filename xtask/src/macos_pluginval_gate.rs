const CI_WORKFLOW: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../.github/workflows/ci.yml"
));
const PLUGINVAL_SCRIPT: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../scripts/validate_macos_pluginval.sh"
));
const README: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/../README.md"));

#[test]
fn macos_ci_runs_pluginval_before_manual_daw_validation() {
    let auval_job = section_after(CI_WORKFLOW, "  auval-arm64:\n");
    require(
        auval_job,
        "Build macOS common-shell AU + VST3 bundles (arm64)",
        "macOS CI must build the common JUCE shell before pluginval",
    );
    require(
        auval_job,
        "Validate macOS ship-set VST3 with pluginval",
        "macOS CI must validate the VST3 bundles that Studio One and installers load",
    );
    require(
        auval_job,
        "scripts/validate_macos_pluginval.sh juce_shell/build-arm64",
        "macOS CI must use the shared local pluginval script on the ship-set directory",
    );
    require(
        auval_job,
        "Upload macOS pluginval logs",
        "macOS CI must retain pluginval logs for diagnosis",
    );

    let build_idx = auval_job
        .find("Build macOS common-shell AU + VST3 bundles (arm64)")
        .expect("macOS CI must build VST3 ship-set bundles");
    let pluginval_idx = auval_job
        .find("Validate macOS ship-set VST3 with pluginval")
        .expect("macOS CI must run pluginval");
    let auval_idx = auval_job
        .find("auval PRE")
        .expect("macOS CI must keep AU validation");
    assert!(
        build_idx < pluginval_idx && pluginval_idx < auval_idx,
        "pluginval should run after ship-set VST3 build and before AU install/auval"
    );
}

#[test]
fn macos_pluginval_script_is_a_reusable_strict_preflight_gate() {
    require(
        PLUGINVAL_SCRIPT,
        "BUILD_DIR=\"${1:-${KIRIN_MACOS_VST3_DIR:-juce_shell/build-universal}}\"",
        "script must default to the JUCE common-shell build directory",
    );
    require(
        PLUGINVAL_SCRIPT,
        "KirinHyphaPRE_artefacts/Release/VST3/Kirin Hypha PRE.vst3",
        "script must validate the PRE ship-set VST3 by exact path",
    );
    require(
        PLUGINVAL_SCRIPT,
        "KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3",
        "script must validate the POST ship-set VST3 by exact path",
    );
    require(
        PLUGINVAL_SCRIPT,
        "COMPONENT_CIDS=(",
        "script must pin the existing VST3 component identities",
    );
    require(
        PLUGINVAL_SCRIPT,
        "CFBundleDisplayName CFBundleName",
        "script must verify role-first display names before pluginval",
    );
    require(
        PLUGINVAL_SCRIPT,
        "PRE Kirin Hypha",
        "script must verify the PRE role-first display name",
    );
    require(
        PLUGINVAL_SCRIPT,
        "POST Kirin Hypha",
        "script must verify the POST role-first display name",
    );
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
        "\"$TIMEOUT_MS\" -lt 1",
        "script must reject a zero timeout instead of passing it through to pluginval",
    );
    require(
        PLUGINVAL_SCRIPT,
        "XATTR_TARGET=\"$PLUGINVAL_EXE\"",
        "script must default quarantine removal to the executable path only",
    );
    require(
        PLUGINVAL_SCRIPT,
        "*.app/Contents/MacOS/*",
        "script must expand quarantine removal only when the executable is inside an app bundle",
    );
    require(
        PLUGINVAL_SCRIPT,
        "PLUGINVAL_RUNTIME_DIR",
        "script must isolate plugin runtime writes away from the user's Kirin OS data",
    );
    require(
        PLUGINVAL_SCRIPT,
        "RUNTIME_DIR=\"$ROOT/$RUNTIME_DIR\"",
        "script must make the isolated plugin runtime directory absolute",
    );
    require(
        PLUGINVAL_SCRIPT,
        "env HOME=\"$RUN_HOME\" TMPDIR=\"$RUN_TMP\"",
        "script must run pluginval with isolated HOME and TMPDIR",
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
}

#[test]
fn readme_exposes_the_local_pluginval_gate() {
    require(
        README,
        "scripts/validate_macos_pluginval.sh juce_shell/build-universal",
        "README must tell maintainers how to run the local macOS pluginval gate",
    );
    require(
        README,
        "JUCE common-shell PRE/POST VST3 bundles",
        "README must make clear that pluginval checks the actual common-shell ship set",
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
