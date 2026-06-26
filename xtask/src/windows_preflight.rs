use anyhow::{bail, Result};

const JUCE_CMAKE: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../juce_shell/CMakeLists.txt"
));
const CI_WORKFLOW: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../.github/workflows/ci.yml"
));
const FFI_HEADER: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h"
));
const FFI_README: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../crates/kirin_hypha_ffi/README.md"
));
const FFI_CARGO_TOML: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../crates/kirin_hypha_ffi/Cargo.toml"
));

pub fn run(args: Vec<String>) -> Result<()> {
    match args.as_slice() {
        [] => {}
        [arg] if arg == "-h" || arg == "--help" => {
            print_usage();
            return Ok(());
        }
        [arg] => bail!("unknown argument: {arg}"),
        _ => bail!("unknown arguments: {}", args.join(" ")),
    }

    verify_cmake_platform_split(JUCE_CMAKE)?;
    verify_windows_ci_job(CI_WORKFLOW)?;
    verify_ffi_staticlib_docs(FFI_HEADER, FFI_README, FFI_CARGO_TOML)?;
    eprintln!("[windows-preflight] OK: Windows VST3 path is separated from macOS release gates.");
    Ok(())
}

fn print_usage() {
    eprintln!(
        "Usage: cargo run -p xtask -- windows-preflight\n\n\
         Static preflight for starting Windows VST3 work. It verifies that JUCE CMake\n\
         and GitHub Actions keep macOS AU/codesign-era assumptions away from the\n\
         Windows path that builds VST3 only with the MSVC staticlib name."
    );
}

fn verify_cmake_platform_split(cmake: &str) -> Result<()> {
    let cmake = normalize_newlines(cmake);
    let cmake = cmake.as_str();

    require(
        cmake,
        "if(APPLE)\n    # B-092",
        "macOS deployment target must be behind APPLE",
    )?;
    require(
        cmake,
        "if(APPLE)\n    # B-082",
        "CMAKE_OSX_ARCHITECTURES must be behind APPLE",
    )?;
    require(
        cmake,
        "if(WIN32)\n    set(_KIRIN_FFI_DEFAULT \"${CMAKE_CURRENT_SOURCE_DIR}/../target/release/kirin_hypha_ffi.lib\")",
        "Windows must default to the MSVC .lib staticlib name",
    )?;
    require(
        cmake,
        "set(_KIRIN_FFI_DEFAULT \"${CMAKE_CURRENT_SOURCE_DIR}/../target/release/libkirin_hypha_ffi.a\")",
        "non-Windows must keep the existing .a staticlib default",
    )?;
    require(
        cmake,
        "if(APPLE)\n    set(KIRIN_PLUGIN_FORMATS AU VST3)\nelseif(WIN32)\n    set(KIRIN_PLUGIN_FORMATS VST3)",
        "Windows plugin formats must be VST3-only",
    )?;
    require(
        cmake,
        "if(MSVC)\n    set(KIRIN_FORCE_INCLUDE_ARGS \"/FI${CMAKE_CURRENT_LIST_DIR}/src/KirinJucePluginConfig.h\")",
        "MSVC must use /FI for the forced include header",
    )?;
    require(
        cmake,
        "set(KIRIN_FORCE_INCLUDE_ARGS -include \"${CMAKE_CURRENT_LIST_DIR}/src/KirinJucePluginConfig.h\")",
        "non-MSVC compilers must keep the existing -include forced header",
    )?;
    require(
        cmake,
        "if(WIN32)\n    set(KIRIN_RUST_NATIVE_LIBS ntdll userenv bcrypt ws2_32 advapi32)",
        "Windows must link Rust staticlib native system libraries",
    )?;
    require(
        cmake,
        "target_link_libraries(${TARGET} PRIVATE ${KIRIN_FFI_LIB} KirinHyphaData ${KIRIN_RUST_NATIVE_LIBS})",
        "plugin targets must link the Rust native library variable",
    )?;
    reject(
        cmake,
        "        FORMATS AU VST3",
        "plugin formats must not be hardcoded to macOS AU+VST3",
    )?;
    require(
        cmake,
        "FORMATS ${KIRIN_PLUGIN_FORMATS}",
        "juce_add_plugin must consume the platform format variable",
    )?;
    reject(
        cmake,
        "target_compile_options(${TARGET} PUBLIC\n        -include",
        "forced include must not be hardcoded to clang/gcc -include",
    )?;
    require(
        cmake,
        "target_compile_options(${TARGET} PUBLIC ${KIRIN_FORCE_INCLUDE_ARGS})",
        "compile options must consume the platform forced-include variable",
    )?;
    require(
        cmake,
        "if(APPLE)\n        # B-130",
        "AU resourceUsage post-build step must be APPLE-only",
    )?;
    require(
        cmake,
        "add_custom_command(TARGET ${TARGET}_AU POST_BUILD",
        "macOS AU resourceUsage post-build step must remain present for macOS",
    )?;
    Ok(())
}

fn verify_ffi_staticlib_docs(header: &str, readme: &str, cargo_toml: &str) -> Result<()> {
    require(
        header,
        "Windows/MSVC: target/{debug,release}/kirin_hypha_ffi.lib",
        "C ABI header must document the Windows/MSVC .lib staticlib",
    )?;
    require(
        header,
        "macOS/Linux:  target/{debug,release}/libkirin_hypha_ffi.a",
        "C ABI header must document the macOS/Linux .a staticlib",
    )?;
    require(
        readme,
        "Windows/MSVC: target/{debug,release}/kirin_hypha_ffi.lib",
        "FFI README must document the Windows/MSVC .lib staticlib",
    )?;
    require(
        readme,
        "macOS/Linux:  target/{debug,release}/libkirin_hypha_ffi.a",
        "FFI README must document the macOS/Linux .a staticlib",
    )?;
    require(
        cargo_toml,
        "kirin_hypha_ffi.lib on Windows/MSVC",
        "FFI Cargo metadata must mention the Windows/MSVC .lib staticlib",
    )?;
    require(
        cargo_toml,
        "libkirin_hypha_ffi.a on macOS/Linux",
        "FFI Cargo metadata must mention the macOS/Linux .a staticlib",
    )?;
    Ok(())
}

fn verify_windows_ci_job(workflow: &str) -> Result<()> {
    let workflow = normalize_newlines(workflow);
    let job = workflow_job(&workflow, "windows-vst3-preflight")?;
    let job_code = strip_yaml_comments(job);
    let job_code = job_code.as_str();

    require(
        job_code,
        "runs-on: windows-latest",
        "Windows preflight job must run on windows-latest",
    )?;
    require(
        job_code,
        "Build kirin_hypha_ffi staticlib (MSVC)",
        "Windows preflight job must build the MSVC staticlib",
    )?;
    require(
        job_code,
        "cargo build --release -p kirin_hypha_ffi --locked",
        "Windows preflight job must build kirin_hypha_ffi with locked deps",
    )?;
    require(
        job_code,
        "cargo run -p xtask --locked -- windows-preflight",
        "Windows preflight job must run this static guard",
    )?;
    require(
        job_code,
        "KirinHyphaPRE_VST3 KirinHyphaPOST_VST3",
        "Windows preflight job must build PRE/POST VST3 targets",
    )?;
    require(
        job_code,
        "Verify Windows VST3 artifacts",
        "Windows preflight job must verify built VST3 artifacts",
    )?;
    require(
        job_code,
        "Test-Path -LiteralPath $bundle",
        "Windows preflight job must fail when a VST3 artifact path is missing",
    )?;
    require(
        job_code,
        "KirinHyphaPRE_artefacts/Release/VST3/Kirin Hypha PRE.vst3",
        "Windows preflight job must verify the PRE VST3 artifact path",
    )?;
    require(
        job_code,
        "KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3",
        "Windows preflight job must verify the POST VST3 artifact path",
    )?;
    require(
        job_code,
        "uses: actions/upload-artifact@v7",
        "Windows preflight job must upload built VST3 artifacts",
    )?;
    require(
        job_code,
        "name: kirin-hypha-windows-vst3",
        "Windows preflight artifact must use a stable artifact name",
    )?;
    require(
        job_code,
        "if-no-files-found: error",
        "Windows preflight artifact upload must fail if bundles are missing",
    )?;

    for forbidden in [
        "auval",
        "notarize",
        "notarytool",
        "codesign",
        "xcrun",
        "stapler",
        ".component",
        "Components",
        "Developer ID",
        "LS_UPLOAD",
        "build_kirin_hypha_pkg",
        "Library/Audio/Plug-Ins",
    ] {
        reject(
            job_code,
            forbidden,
            &format!("Windows preflight job must not contain macOS release step `{forbidden}`"),
        )?;
    }

    Ok(())
}

fn strip_yaml_comments(source: &str) -> String {
    source
        .lines()
        .map(|line| line.split_once('#').map_or(line, |(code, _)| code))
        .collect::<Vec<_>>()
        .join("\n")
}

fn workflow_job<'a>(workflow: &'a str, job_name: &str) -> Result<&'a str> {
    let marker = format!("  {job_name}:\n");
    let start = workflow
        .find(&marker)
        .ok_or_else(|| anyhow::anyhow!("CI workflow missing job `{job_name}`"))?
        + marker.len();
    let tail = &workflow[start..];
    let mut end = tail.len();
    let mut byte = 0usize;
    for line in tail.split_inclusive('\n') {
        let trimmed = line.trim_end_matches('\n');
        if trimmed.starts_with("  ") && !trimmed.starts_with("    ") && trimmed.ends_with(':') {
            end = byte;
            break;
        }
        byte += line.len();
    }
    Ok(&tail[..end])
}

fn normalize_newlines(source: &str) -> String {
    source.replace("\r\n", "\n").replace('\r', "\n")
}

fn require(source: &str, needle: &str, message: &str) -> Result<()> {
    if source.contains(needle) {
        Ok(())
    } else {
        bail!("{message}: missing `{needle}`")
    }
}

fn reject(source: &str, needle: &str, message: &str) -> Result<()> {
    if source.contains(needle) {
        bail!("{message}: found `{needle}`")
    } else {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
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
    fn preflight_requires_pre_and_post_artifact_paths() {
        let bad = CI_WORKFLOW.replace(
            "KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3",
            "KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha WRONG.vst3",
        );
        let err = verify_windows_ci_job(&bad).unwrap_err();
        assert!(err
            .to_string()
            .contains("must verify the POST VST3 artifact path"));
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
}
