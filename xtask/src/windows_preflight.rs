use anyhow::{bail, Result};

const JUCE_CMAKE: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../juce_shell/CMakeLists.txt"
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
    eprintln!("[windows-preflight] OK: JUCE CMake is VST3-only on Windows and keeps macOS release gates under APPLE.");
    Ok(())
}

fn print_usage() {
    eprintln!(
        "Usage: cargo run -p xtask -- windows-preflight\n\n\
         Static preflight for starting Windows VST3 work. It verifies that JUCE CMake\n\
         keeps macOS AU/codesign-era assumptions behind APPLE gates and exposes a\n\
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
}
