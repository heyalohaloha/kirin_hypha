use super::*;

#[test]
fn cargo_library_path_outside_src_is_scanned_as_the_real_entry() {
    let root = std::env::temp_dir().join(format!("ffi-metadata-entry-{}", std::process::id()));
    std::fs::create_dir(&root).expect("unique metadata fixture");
    let root = root.canonicalize().unwrap();
    struct Cleanup(PathBuf);
    impl Drop for Cleanup {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }
    let _cleanup = Cleanup(root.clone());
    std::fs::create_dir(root.join("other")).unwrap();
    std::fs::write(
        root.join("Cargo.toml"),
        concat!(
            "[package]\nname = \"kirin_hypha_ffi\"\nversion = \"0.0.0\"\nedition = \"2021\"\n",
            "[lib]\npath = \"other/entry.txt\"\ncrate-type = [\"staticlib\"]\n",
            "[workspace]\n"
        ),
    )
    .unwrap();
    std::fs::write(
        root.join("other/entry.txt"),
        "#[path = \"module.rs\"] mod child;",
    )
    .unwrap();
    std::fs::write(
        root.join("other/module.rs"),
        "pub extern \"C\" fn needs_guard() {}",
    )
    .unwrap();
    let metadata = read_metadata(&root, false);
    let crates = shipping_crates(&metadata).unwrap();
    let roots = library_roots(&metadata, &crates[0]).unwrap();
    assert_eq!(roots, [root.join("other/entry.txt")]);
    let files = super::super::sources::shipping_sources(&root, &crates[0].dir, &roots).unwrap();
    assert_eq!(files.len(), 2);
    assert!(files.contains(&root.join("other/entry.txt").canonicalize().unwrap()));
    assert!(files.contains(&root.join("other/module.rs").canonicalize().unwrap()));

    let entries: Vec<_> = files
        .iter()
        .flat_map(|file| super::super::scan(&std::fs::read_to_string(file).unwrap()).entries)
        .collect();
    assert!(
        entries
            .iter()
            .any(|entry| entry.name == "needs_guard" && !entry.guarded),
        "the newly covered source must feed the actual guard, not only the inventory"
    );

    for invalid in [
        serde_json::Value::Null,
        serde_json::json!([]),
        serde_json::json!([{"kind": ["bin"], "src_path": "ignored.rs"}]),
        serde_json::json!([{"kind": ["lib"], "src_path": "relative.rs"}]),
    ] {
        let mut altered = metadata.clone();
        altered["packages"][0]["targets"] = invalid;
        assert!(library_roots(&altered, &crates[0]).is_err());
    }
}
