use super::*;

#[test]
fn ship_set_is_juce_common_shell_only() {
    let bundles = ship_bundles().unwrap();
    verify_ship_set_shape(&bundles).unwrap();
    assert!(bundles
        .iter()
        .all(|bundle| bundle.source.starts_with("juce_shell/build-universal")));
    assert_eq!(
        bundles
            .iter()
            .filter(|bundle| bundle.spec.kind == BundleKind::Au)
            .count(),
        2
    );
    assert_eq!(
        bundles
            .iter()
            .filter(|bundle| bundle.spec.kind == BundleKind::Vst3)
            .count(),
        2
    );
    assert!(bundles
        .iter()
        .filter(|bundle| bundle.spec.kind == BundleKind::Vst3)
        .all(|bundle| bundle.source.to_string_lossy().contains("/Release/VST3/")));
}

#[test]
fn release_manifest_mentions_all_four_installed_files() {
    let bundles = ship_bundles().unwrap();
    let leaf = package_leaf("1.1.1", false);
    let json = manifest_json("1.1.1", &leaf, "abc", false, "false", &bundles).unwrap();
    for file in [
        "Kirin Hypha PRE.component",
        "Kirin Hypha POST.component",
        "PRE Kirin Hypha.vst3",
        "POST Kirin Hypha.vst3",
    ] {
        assert!(json.contains(file));
    }
    assert!(json.contains("\"ship_set\": \"juce-common-shell\""));
    assert!(json.contains("\"unsigned_smoke_test\": false"));
}
