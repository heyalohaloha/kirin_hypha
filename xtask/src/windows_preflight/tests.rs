use super::*;

#[test]
fn juce_cmake_keeps_windows_vst3_preflight_separate_from_macos_release() {
    verify_cmake_platform_split(JUCE_CMAKE).unwrap();
}

#[test]
fn preflight_accepts_crlf_checkout() {
    let crlf = JUCE_CMAKE.replace('\n', "\r\n");
    verify_cmake_platform_split(&crlf).unwrap();
    let crlf = CI_WORKFLOW.replace('\n', "\r\n");
    verify_windows_ci_job(&crlf).unwrap();
    verify_ffi_staticlib_docs(FFI_HEADER, FFI_README, FFI_CARGO_TOML).unwrap();
}

#[test]
fn preflight_rejects_hardcoded_au_vst3_formats() {
    let bad = JUCE_CMAKE.replace("FORMATS ${KIRIN_PLUGIN_FORMATS}", "FORMATS AU VST3");
    let err = verify_cmake_platform_split(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("plugin formats must not be hardcoded"));
}

#[test]
fn preflight_requires_windows_staticlib_name() {
    let bad = JUCE_CMAKE.replace("kirin_hypha_ffi.lib", "libkirin_hypha_ffi.a");
    let err = verify_cmake_platform_split(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("Windows must default to the MSVC .lib"));
}

#[test]
fn preflight_requires_windows_staticlib_docs() {
    let bad_header = FFI_HEADER.replace(
        "Windows/MSVC: target/{debug,release}/kirin_hypha_ffi.lib",
        "Windows/MSVC: target/{debug,release}/libkirin_hypha_ffi.a",
    );
    let err = verify_ffi_staticlib_docs(&bad_header, FFI_README, FFI_CARGO_TOML).unwrap_err();
    assert!(err
        .to_string()
        .contains("C ABI header must document the Windows/MSVC .lib"));
}

#[test]
fn preflight_requires_macos_staticlib_docs() {
    let bad_readme = FFI_README.replace(
        "macOS/Linux:  target/{debug,release}/libkirin_hypha_ffi.a",
        "macOS/Linux:  target/{debug,release}/kirin_hypha_ffi.lib",
    );
    let err = verify_ffi_staticlib_docs(FFI_HEADER, &bad_readme, FFI_CARGO_TOML).unwrap_err();
    assert!(err
        .to_string()
        .contains("FFI README must document the macOS/Linux .a"));
}

#[test]
fn preflight_requires_cargo_staticlib_docs() {
    let bad_cargo_toml = FFI_CARGO_TOML.replace(
        "kirin_hypha_ffi.lib on Windows/MSVC",
        "libkirin_hypha_ffi.a on Windows/MSVC",
    );
    let err = verify_ffi_staticlib_docs(FFI_HEADER, FFI_README, &bad_cargo_toml).unwrap_err();
    assert!(err
        .to_string()
        .contains("FFI Cargo metadata must mention the Windows/MSVC .lib"));
}

#[test]
fn preflight_requires_msvc_force_include_flag() {
    let bad = JUCE_CMAKE.replace(
        "/FI${CMAKE_CURRENT_LIST_DIR}/src/KirinJucePluginConfig.h",
        "-include ${CMAKE_CURRENT_LIST_DIR}/src/KirinJucePluginConfig.h",
    );
    let err = verify_cmake_platform_split(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("MSVC must use /FI for the forced include header"));
}

#[test]
fn preflight_requires_windows_rust_native_libs() {
    let bad = JUCE_CMAKE.replace(
        "set(KIRIN_RUST_NATIVE_LIBS ntdll userenv bcrypt ws2_32 advapi32)",
        "set(KIRIN_RUST_NATIVE_LIBS)",
    );
    let err = verify_cmake_platform_split(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("Windows must link Rust staticlib native system libraries"));
}

#[test]
fn preflight_rejects_unconditional_dash_include() {
    let bad = JUCE_CMAKE.replace(
        "target_compile_options(${TARGET} PUBLIC ${KIRIN_FORCE_INCLUDE_ARGS})",
        "target_compile_options(${TARGET} PUBLIC\n        -include \"${CMAKE_CURRENT_LIST_DIR}/src/KirinJucePluginConfig.h\")",
    );
    let err = verify_cmake_platform_split(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("forced include must not be hardcoded"));
}

#[test]
fn preflight_requires_windows_ci_job_static_guard() {
    let bad = CI_WORKFLOW.replace(
        "cargo run -p xtask --locked -- windows-preflight",
        "cargo run -p xtask --locked -- diagnose-watch",
    );
    let err = verify_windows_ci_job(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("Windows preflight job must run this static guard"));
}

#[test]
fn preflight_rejects_static_guard_mentioned_only_in_comment() {
    let bad = CI_WORKFLOW.replace(
        "run: cargo run -p xtask --locked -- windows-preflight",
        "# run: cargo run -p xtask --locked -- windows-preflight\n        run: cargo run -p xtask --locked -- diagnose-watch",
    );
    let err = verify_windows_ci_job(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("Windows preflight job must run this static guard"));
}

#[test]
fn preflight_requires_windows_artifact_presence_check() {
    let bad = CI_WORKFLOW.replace("Test-Path -LiteralPath $bundle", "Write-Host $bundle");
    let err = verify_windows_ci_job(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("must fail when a VST3 artifact path is missing"));
}

#[test]
fn preflight_requires_windows_binary_presence_check() {
    let bad = CI_WORKFLOW.replace(
        "Test-Path -LiteralPath $binary -PathType Leaf",
        "Write-Host $binary",
    );
    let err = verify_windows_ci_job(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("must fail when a VST3 binary path is missing"));
}

#[test]
fn preflight_requires_nonempty_windows_binary_check() {
    let bad = CI_WORKFLOW.replace("$binaryInfo.Length -le 0", "$false");
    let err = verify_windows_ci_job(&bad).unwrap_err();
    assert!(err.to_string().contains("must reject empty VST3 binaries"));
}

#[test]
fn preflight_requires_pre_and_post_artifact_paths() {
    let bad = CI_WORKFLOW.replace(
        "KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3",
        "KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha WRONG.vst3",
    );
    let err = verify_windows_ci_job(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("must verify the layout artifact path"));
}

#[test]
fn preflight_requires_pre_and_post_binary_paths() {
    let bad = CI_WORKFLOW.replace(
        "KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3/Contents/x86_64-win/Kirin Hypha POST.vst3",
        "KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3/Contents/x86_64-win/Kirin Hypha WRONG.vst3",
    );
    let err = verify_windows_ci_job(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("must verify the layout binary path"));
}

#[test]
fn preflight_requires_upload_paths_to_match_layout_paths() {
    let bad = CI_WORKFLOW.replace(
        "            juce_shell/build-windows/KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3",
        "            juce_shell/build-windows/KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha WRONG.vst3",
    );
    let err = verify_windows_ci_job(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("must upload the layout artifact path"));
}

#[test]
fn preflight_requires_windows_artifact_upload_error_on_missing_files() {
    let bad = CI_WORKFLOW.replace("if-no-files-found: error", "if-no-files-found: warn");
    let err = verify_windows_ci_job(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("artifact upload must fail if bundles are missing"));
}

#[test]
fn preflight_rejects_macos_release_step_in_windows_ci_job() {
    let bad = CI_WORKFLOW.replace(
        "Build JUCE VST3 shell (Windows)",
        "Build JUCE VST3 shell (Windows)\n      - name: notarize",
    );
    let err = verify_windows_ci_job(&bad).unwrap_err();
    assert!(err
        .to_string()
        .contains("must not contain macOS release step"));
}
