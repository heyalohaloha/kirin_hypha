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
    let entries = bundle_entries(&bundles).unwrap();
    let json = release_package_metadata::manifest_json(
        "1.1.1", &leaf, "abc", false, "false", false, &entries,
    )
    .unwrap();
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

#[test]
fn aax_opt_in_adds_both_roles_to_metadata_and_install_text() {
    let bundles = ship_bundles().unwrap();
    let aax_bundles = aax_distribution::bundles().unwrap();
    let mut entries = bundle_entries(&bundles).unwrap();
    entries.extend(aax_distribution::metadata_entries(&aax_bundles).unwrap());
    let json = release_package_metadata::manifest_json(
        "1.1.1",
        &package_leaf("1.1.1", false),
        "abc",
        false,
        "false",
        true,
        &entries,
    )
    .unwrap();
    assert!(json.contains("\"ship_set\": \"juce-common-shell+aax\""));
    assert!(json.contains("\"aax_included\": true"));
    assert!(json.contains("Kirin Hypha PRE.aaxplugin"));
    assert!(json.contains("Kirin Hypha POST.aaxplugin"));

    let install = release_package_metadata::install_text("1.1.1", true);
    assert!(install.contains("/Library/Application Support/Avid/Audio/Plug-Ins/"));
    assert!(install.contains("AAX/Kirin Hypha PRE.aaxplugin"));
    assert!(install.contains("AAX/Kirin Hypha POST.aaxplugin"));
}
