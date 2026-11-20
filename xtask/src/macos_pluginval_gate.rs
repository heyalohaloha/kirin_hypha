const CI_WORKFLOW: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../.github/workflows/ci.yml"
));
const PLUGINVAL_SCRIPT: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../scripts/validate_macos_pluginval.sh"
));
const BUILD_GUIDE: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../docs/manual/building_from_source.md"
));

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
        "node scripts/ls_release/kirin_hypha_ship_bundles.mjs",
        "script must resolve VST3 paths from the canonical ship bundle manifest",
    );
    require(
        PLUGINVAL_SCRIPT,
        "STAGE_ROOT=\"$RUNTIME_DIR/ship-set\"",
        "script must build an isolated copy of the exact installed layout",
    );
    require(
        PLUGINVAL_SCRIPT,
        "ditto \"$SOURCE_BUNDLE\" \"$STAGED_BUNDLE\"",
        "script must preserve each signed bundle while applying its installed outer name",
    );
    require(
        PLUGINVAL_SCRIPT,
        "ship-bundle-verify",
        "script must use the shared behavioral bundle contract before pluginval",
    );
    require(
        PLUGINVAL_SCRIPT,
        "--installed-root \"$STAGE_ROOT\"",
        "shared verification must inspect the staged installed-layout paths",
    );
    require(
        PLUGINVAL_SCRIPT,
        "--format vst3",
        "shared verification must be limited to the two VST3 bundles for this gate",
    );
    require(
        PLUGINVAL_SCRIPT,
        "\"${#BUNDLES[@]}\" -ne 2",
        "script must fail closed unless PRE and POST both resolve",
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
        "node scripts/pluginval_archive.mjs url \"$PLUGINVAL_VERSION\" macOS",
        "script must resolve the official macOS archive through the reviewed pin",
    );
    // Exit status and forbidden downstream execution are checked by the
    // isolated shell fixtures below, not by ordering unbound string literals.
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
        "KIRIN_HYPHA_TEST_STORAGE_ROOT=\"$RUNTIME_DIR\"",
        "JUCE and Rust must share pluginval's isolated runtime root",
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
fn build_guide_exposes_the_local_pluginval_gate() {
    require(
        BUILD_GUIDE,
        "scripts/validate_macos_pluginval.sh target/hypha-build/macos-universal-no-aax",
        "the build guide must tell maintainers how to run the local macOS pluginval gate",
    );
    require(
        BUILD_GUIDE,
        "exact role-first installed layout",
        "the build guide must make clear that pluginval checks the host-visible installed layout",
    );
    require(
        BUILD_GUIDE,
        "run the VST3 gate before manual DAW validation",
        "the build guide should frame pluginval as the gate before manual DAW validation",
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

#[cfg(unix)]
fn gate_fixture(case: &str) -> serde_json::Value {
    let output = std::process::Command::new("node")
        .arg(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../scripts/macos_pluginval_fixture.mjs"
        ))
        .arg(case)
        .output()
        .expect("run isolated macOS shell fixture");
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    serde_json::from_slice(&output.stdout).expect("fixture receipt")
}

#[cfg(unix)]
fn operation_count(receipt: &serde_json::Value, operation: &str) -> usize {
    receipt["events"]
        .as_array()
        .unwrap()
        .iter()
        .filter(|event| event["operation"] == operation)
        .count()
}

#[test]
#[cfg(unix)]
fn macos_pluginval_ci_override_cannot_reach_the_tool() {
    let receipt = gate_fixture("ci-override");
    assert_eq!(receipt["status"], 1);
    assert!(receipt["events"].as_array().unwrap().is_empty());
    assert!(receipt["stderr"]
        .as_str()
        .unwrap()
        .contains("CI requires a pinned archive"));
}

#[test]
#[cfg(unix)]
fn macos_pluginval_local_override_is_only_a_diagnostic() {
    let receipt = gate_fixture("local-override");
    assert_eq!(receipt["status"], 2);
    assert_eq!(operation_count(&receipt, "execute"), 2);
    assert_eq!(operation_count(&receipt, "extract"), 0);
    assert_eq!(operation_count(&receipt, "verify"), 0);
}

#[test]
#[cfg(unix)]
fn macos_pluginval_corrupt_zip_cannot_reach_extraction_or_execution() {
    for case in ["bad-cache", "bad-download"] {
        let receipt = gate_fixture(case);
        assert_eq!(receipt["status"], 1, "{case}");
        assert_eq!(operation_count(&receipt, "verify"), 1);
        assert_eq!(operation_count(&receipt, "extract"), 0);
        assert_eq!(operation_count(&receipt, "execute"), 0);
        assert_eq!(operation_count(&receipt, "bundle-check"), 0);
    }
}
