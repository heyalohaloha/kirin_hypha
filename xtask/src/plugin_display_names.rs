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
    }

    #[test]
    fn egui_vst3_display_names_are_role_first_while_bundle_names_stay_stable() {
        assert!(
            HYPHA_PRE_LIB.contains("const NAME: &'static str = \"PRE Kirin Hypha\";"),
            "egui PRE VST3 display name must put PRE before Kirin Hypha"
        );
        assert!(
            HYPHA_POST_LIB.contains("const NAME: &'static str = \"POST Kirin Hypha\";"),
            "egui POST VST3 display name must put POST before Kirin Hypha"
        );
        assert!(
            BUNDLER_TOML.contains("name = \"Kirin Hypha PRE\""),
            "bundler.toml must keep the existing PRE bundle/file name"
        );
        assert!(
            BUNDLER_TOML.contains("name = \"Kirin Hypha POST\""),
            "bundler.toml must keep the existing POST bundle/file name"
        );
    }
}
