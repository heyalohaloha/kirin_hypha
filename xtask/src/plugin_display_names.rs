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
    const STAMP_VERSION_RS: &str =
        include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/src/stamp_version.rs"));
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
    fn egui_vst3_display_and_physical_names_are_role_first() {
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
        assert_eq!(version_line(HYPHA_PRE_CARGO).trim(), "version = \"1.1.26\"");
    }

    #[test]
    fn ship_tooling_stamps_and_gates_role_first_display_metadata() {
        assert!(
            STAMP_VERSION_RS.contains("display_name: \"PRE Kirin Hypha\"")
                && STAMP_VERSION_RS.contains("display_name: \"POST Kirin Hypha\""),
            "stamp-egui-version must know the role-first VST3 display names"
        );
        assert!(
            STAMP_VERSION_RS.contains("\"CFBundleDisplayName\"")
                && STAMP_VERSION_RS.contains("\"CFBundleName\""),
            "stamp-egui-version must update bundle display metadata after nih-plug bundling"
        );
        assert!(
            STAMP_VERSION_RS.contains("remove_legacy_egui_outputs")
                && STAMP_VERSION_RS.contains("Kirin Hypha PRE")
                && STAMP_VERSION_RS.contains("Kirin Hypha POST"),
            "stamp-egui-version must remove the two obsolete physical VST3 names"
        );
        assert!(
            RELEASE_PACKAGE_RS.contains("verify_display_metadata")
                && RELEASE_PACKAGE_RS.contains("\"AudioComponents:0:name\"")
                && RELEASE_PACKAGE_RS.contains("\"CFBundleDisplayName\"")
                && RELEASE_PACKAGE_RS.contains("\"CFBundleName\""),
            "release-package must fail if ship bundles lose role-first display metadata"
        );
        assert!(
            INSTALL_RS.contains("verify_display_metadata")
                && INSTALL_RS.contains("\"AudioComponents:0:name\"")
                && INSTALL_RS.contains("\"CFBundleDisplayName\"")
                && INSTALL_RS.contains("\"CFBundleName\""),
            "install must fail if DAW-bound bundles lose role-first display metadata"
        );
        assert!(
            NOTARIZE_RS.contains("verify_display_metadata")
                && NOTARIZE_RS.contains("\"AudioComponents:0:name\"")
                && NOTARIZE_RS.contains("\"CFBundleDisplayName\"")
                && NOTARIZE_RS.contains("\"CFBundleName\""),
            "notarize must fail before signing if ship bundles lose role-first display metadata"
        );
        assert!(
            LS_PKG_JS.contains("verifyDisplayMetadata")
                && LS_PKG_JS.contains("'AudioComponents:0:name'")
                && LS_PKG_JS.contains("'CFBundleDisplayName'")
                && LS_PKG_JS.contains("'CFBundleName'"),
            "LS pkg builder must fail if upload bundles lose role-first display metadata"
        );
    }
}
