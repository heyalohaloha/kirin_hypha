use crate::windows_vst3_layout;
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
    eprintln!("[windows-preflight] OK: Windows VST3 build, installer, signing, and verification gates are present.");
    Ok(())
}

fn print_usage() {
    eprintln!(
        "Usage: cargo run -p xtask -- windows-preflight\n\n\
         Static preflight for starting Windows VST3 work. It verifies that JUCE CMake\n\
         and GitHub Actions keep Apple release assumptions away from the Windows\n\
         VST3 path while requiring installer, Authenticode, and uninstall gates."
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
        "set(KIRIN_FORCE_INCLUDE_ARGS \"/FI${CMAKE_CURRENT_LIST_DIR}/src/KirinJucePluginConfig.h\")\n    set(KIRIN_SOURCE_ENCODING_ARGS /utf-8)",
        "MSVC must compile UTF-8 Kirin and JUCE sources as UTF-8",
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
        "target_compile_options(${TARGET} PUBLIC\n        ${KIRIN_FORCE_INCLUDE_ARGS}\n        ${KIRIN_SOURCE_ENCODING_ARGS})",
        "compile options must consume the platform forced-include and source-encoding variables",
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
        "Validate Windows VST3 audio transparency",
        "Windows preflight job must execute the PRE/POST bit-transparency host contract",
    )?;
    require(
        job_code,
        "--target KirinAudioTransparencyContractTests",
        "Windows preflight job must build the audio transparency host contract",
    )?;
    require(
        job_code,
        "if ($LASTEXITCODE -ne 0) { throw \"audio transparency contract failed",
        "Windows preflight job must fail when the audio transparency contract fails",
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
        "Test-Path -LiteralPath $binary -PathType Leaf",
        "Windows preflight job must fail when a VST3 binary path is missing",
    )?;
    require(
        job_code,
        "$binaryInfo.Length -le 0",
        "Windows preflight job must reject empty VST3 binaries",
    )?;
    require(
        job_code,
        "Validate Windows VST3 with pluginval",
        "Windows preflight job must validate built VST3 artifacts with pluginval",
    )?;
    require(
        job_code,
        "gh release verify-asset is-6_7_3 'innosetup-6.7.3.exe' --repo jrsoftware/issrc",
        "Windows preflight job must verify Inno Setup against its pinned release attestation",
    )?;
    require(
        job_code,
        "Build Windows installer and sign all executable surfaces",
        "Windows preflight job must build the primary Windows installer",
    )?;
    require(
        job_code,
        "node scripts/windows/build-installer.mjs",
        "Windows preflight job must run the Windows installer build script",
    )?;
    require(
        job_code,
        "--signing $env:WINDOWS_SIGNING",
        "Windows installer build must select signed or unsigned mode explicitly",
    )?;
    require(
        job_code,
        "Verify Windows installer install, same-version reinstall, signatures, and uninstall",
        "Windows preflight job must verify the installed payload and uninstaller",
    )?;
    require(
        job_code,
        "scripts/windows/verify-installer.ps1",
        "Windows preflight job must run the installer verification script",
    )?;
    require(
        job_code,
        "name: kirin-hypha-windows-installer",
        "Windows primary installer must use a stable artifact name",
    )?;
    require(
        job_code,
        "dist/WINDOWS_CI/Kirin-Hypha-*-Windows-x64-Setup.exe",
        "Windows primary artifact must include the Setup executable",
    )?;
    require(
        job_code,
        "ESIGNER_TOTP_SECRET: ${{ secrets.ESIGNER_TOTP_SECRET }}",
        "signed Windows builds must obtain eSigner credentials only from repository secrets",
    )?;
    require(
        job_code,
        "Package fallback Windows VST3 ZIP",
        "Windows preflight job must retain the recovery ZIP as a fallback",
    )?;
    require(
        job_code,
        "--payload-signing $env:WINDOWS_SIGNING",
        "fallback ZIP must record whether its embedded payload is signed",
    )?;
    verify_ci_uses_layout_artifact_paths(job_code)?;
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

fn verify_ci_uses_layout_artifact_paths(job_code: &str) -> Result<()> {
    let verify_step = section_between(
        job_code,
        "Verify Windows VST3 artifacts",
        "Upload Windows VST3 artifacts",
    )?;
    let upload_step = section_after(job_code, "Upload Windows VST3 artifacts")?;

    for path in windows_vst3_layout::windows_vst3_artifact_paths() {
        require(
            verify_step,
            &path,
            "Windows preflight job must verify the layout artifact path",
        )?;
        require(
            upload_step,
            &path,
            "Windows preflight job must upload the layout artifact path",
        )?;
    }
    for path in windows_vst3_layout::windows_vst3_binary_paths() {
        require(
            verify_step,
            &path,
            "Windows preflight job must verify the layout binary path",
        )?;
    }
    Ok(())
}

fn section_between<'a>(source: &'a str, start_marker: &str, end_marker: &str) -> Result<&'a str> {
    let after_start = section_after(source, start_marker)?;
    let end = after_start
        .find(end_marker)
        .ok_or_else(|| anyhow::anyhow!("missing section marker `{end_marker}`"))?;
    Ok(&after_start[..end])
}

fn section_after<'a>(source: &'a str, marker: &str) -> Result<&'a str> {
    source
        .split_once(marker)
        .map(|(_, tail)| tail)
        .ok_or_else(|| anyhow::anyhow!("missing section marker `{marker}`"))
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
mod tests;
