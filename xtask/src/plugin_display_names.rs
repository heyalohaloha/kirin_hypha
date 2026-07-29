#[cfg(test)]
mod tests {
    const JUCE_CMAKE: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/CMakeLists.txt"
    ));
    const HYPHA_PRE_LIB: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../crates/hypha_pre/src/lib.rs"
    ));
    const HYPHA_POST_LIB: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../crates/hypha_post/src/lib.rs"
    ));
    const HYPHA_PRE_CARGO: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../crates/hypha_pre/Cargo.toml"
    ));
    const HYPHA_POST_CARGO: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../crates/hypha_post/Cargo.toml"
    ));
    const JUCE_PROCESSOR_CPP: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../juce_shell/src/PluginProcessor.cpp"
    ));
    const RELEASE_PACKAGE_RS: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/src/release_package.rs"
    ));
    const INSTALL_RS: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/src/install.rs"));
    const NOTARIZE_RS: &str = include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/src/notarize.rs"));
    const LS_PKG_JS: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../scripts/ls_release/build_kirin_hypha_pkg.mjs"
    ));
    const SHIP_BUNDLE_JS: &str = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../scripts/ls_release/kirin_hypha_ship_bundles.mjs"
    ));
    const BUNDLER_TOML: &str =
        include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/../bundler.toml"));

    #[test]
    fn juce_keeps_bundle_names_but_uses_role_first_plugin_names() {
        assert!(
            JUCE_CMAKE.contains("PRODUCT_NAME ${PRODUCT}"),
            "JUCE PRODUCT_NAME must remain the physical bundle/file name"
        );
        assert!(
            JUCE_CMAKE.contains("PLUGIN_NAME ${DISPLAY_NAME}"),
            "JUCE PLUGIN_NAME must be separate from PRODUCT_NAME for DAW display"
        );
        assert!(
            JUCE_CMAKE.contains(
                "add_kirin_plugin(KirinHyphaPRE  Khpr \"Kirin Hypha PRE\"  \"PRE Kirin Hypha\""
            ),
            "PRE JUCE target must keep old bundle name but display PRE first"
        );
        assert!(
            JUCE_CMAKE.contains(
                "add_kirin_plugin(KirinHyphaPOST Khpo \"Kirin Hypha POST\" \"POST Kirin Hypha\""
            ),
            "POST JUCE target must keep old bundle name but display POST first"
        );
        assert!(
            JUCE_PROCESSOR_CPP.contains(
                "return role == Role::Post ? \"POST Kirin Hypha\" : \"PRE Kirin Hypha\";"
            ),
            "JUCE runtime getName() must also be role-first for host title/plugin lists"
        );
    }

    #[test]
    fn legacy_development_egui_adapter_retains_role_first_names() {
        assert!(
            HYPHA_PRE_LIB.contains("const NAME: &'static str = \"PRE Kirin Hypha\";"),
            "egui PRE VST3 display name must put PRE before Kirin Hypha"
        );
        assert!(
            HYPHA_POST_LIB.contains("const NAME: &'static str = \"POST Kirin Hypha\";"),
            "egui POST VST3 display name must put POST before Kirin Hypha"
        );
        assert!(
            BUNDLER_TOML.contains("name = \"PRE Kirin Hypha\""),
            "bundler.toml must expose PRE first in hosts that surface the module filename"
        );
        assert!(
            BUNDLER_TOML.contains("name = \"POST Kirin Hypha\""),
            "bundler.toml must expose POST first in hosts that surface the module filename"
        );
    }

    #[test]
    fn pre_and_post_version_advance_together_to_invalidate_host_name_caches() {
        let version_line = |source: &str| {
            source
                .lines()
                .find(|line| line.trim_start().starts_with("version ="))
                .expect("package version")
                .to_string()
        };
        assert_eq!(
            version_line(HYPHA_PRE_CARGO),
            version_line(HYPHA_POST_CARGO)
        );
        assert_eq!(version_line(HYPHA_PRE_CARGO).trim(), "version = \"1.1.35\"");
    }

    #[test]
    fn ship_tooling_uses_one_manifest_and_one_behavioral_verifier() {
        let bundles = crate::ship_bundle::macos_bundles().unwrap();
        let pre_vst3 = bundles
            .iter()
            .find(|bundle| {
                bundle.role == "PRE" && bundle.kind == crate::ship_bundle::BundleKind::Vst3
            })
            .unwrap();
        assert_eq!(
            pre_vst3.source_relative.to_string_lossy(),
            "KirinHyphaPRE_artefacts/Release/VST3/Kirin Hypha PRE.vst3"
        );
        assert_eq!(
            pre_vst3.install_relative.to_string_lossy(),
            "Library/Audio/Plug-Ins/VST3/PRE Kirin Hypha.vst3"
        );
        assert_eq!(pre_vst3.executable_name, "Kirin Hypha PRE");
        assert_eq!(pre_vst3.display_name, "PRE Kirin Hypha");
        assert_eq!(
            pre_vst3.component_cid.as_deref(),
            Some("4B6972696E4879706861505245763031")
        );

        for source in [RELEASE_PACKAGE_RS, INSTALL_RS, NOTARIZE_RS] {
            assert!(
                source.contains("ship_bundle::verify_bundle_contract"),
                "every Rust ship consumer must call the shared behavioral verifier"
            );
            assert!(
                !source.contains("4B6972696E4879706861505245763031"),
                "component identities must not be duplicated outside the manifest"
            );
        }
        assert!(LS_PKG_JS.contains("loadMacShipBundleManifest"));
        assert!(LS_PKG_JS.contains("ship-bundle-verify"));
        assert!(!LS_PKG_JS.contains("4B6972696E4879706861505245763031"));
        assert!(SHIP_BUNDLE_JS.contains("config/hypha_macos_ship_bundles.json"));
        assert!(SHIP_BUNDLE_JS.contains("install path is outside the approved plug-in directory"));
    }
}
