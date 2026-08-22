use super::*;

#[cfg(target_os = "macos")]
fn pre_vst3() -> MacBundleSpec {
    macos_bundles()
        .unwrap()
        .into_iter()
        .find(|spec| spec.role == "PRE" && spec.kind == BundleKind::Vst3)
        .unwrap()
}

#[test]
fn manifest_has_one_pre_and_post_for_each_format() {
    let bundles = macos_bundles().unwrap();
    assert_eq!(bundles.len(), 4);
    for role in ["PRE", "POST"] {
        for kind in [BundleKind::Au, BundleKind::Vst3] {
            assert_eq!(
                bundles
                    .iter()
                    .filter(|spec| spec.role == role && spec.kind == kind)
                    .count(),
                1
            );
        }
    }
}

#[test]
fn manifest_rejects_install_paths_outside_approved_plugin_directories() {
    let mut manifest: ShipManifest = serde_json::from_str(MANIFEST_SOURCE).unwrap();
    manifest.bundles[2].install_relative =
        PathBuf::from("../../Library/unsafe/PRE Kirin Hypha.vst3");
    let error = validate_manifest(&manifest).unwrap_err().to_string();
    assert!(error.contains("safe relative path"));
}

#[cfg(target_os = "macos")]
mod macos {
    use super::*;
    use std::io::Write;

    struct TestDir(PathBuf);

    impl TestDir {
        fn new() -> Self {
            let path = std::env::temp_dir().join(format!(
                "kirin_ship_bundle_test_{}_{}",
                std::process::id(),
                rand::random::<u64>()
            ));
            fs::create_dir_all(&path).unwrap();
            Self(path)
        }

        fn path(&self) -> &Path {
            &self.0
        }
    }

    impl Drop for TestDir {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn fake_vst3(
        root: &Path,
        outer_name: &str,
        declared_executable: &str,
        actual_executable: Option<&str>,
        bytes: &[u8],
    ) -> PathBuf {
        let spec = pre_vst3();
        let bundle = root.join(outer_name);
        fs::create_dir_all(bundle.join("Contents/MacOS")).unwrap();
        fs::create_dir_all(bundle.join("Contents/Resources")).unwrap();
        fs::write(
            bundle.join("Contents/Info.plist"),
            format!(
                r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>{declared_executable}</string>
<key>CFBundleDisplayName</key><string>{}</string>
<key>CFBundleName</key><string>{}</string>
</dict></plist>
"#,
                spec.executable_name, spec.executable_name
            ),
        )
        .unwrap();
        fs::write(
            bundle.join("Contents/Resources/moduleinfo.json"),
            format!(
                r#"{{"Classes":[{{"CID": "{}", "Name": "{}"}}]}}"#,
                spec.component_cid.unwrap(),
                spec.display_name
            ),
        )
        .unwrap();
        if let Some(executable) = actual_executable {
            let mut file = File::create(bundle.join("Contents/MacOS").join(executable)).unwrap();
            file.write_all(bytes).unwrap();
        }
        bundle
    }

    #[test]
    fn role_first_outer_bundle_uses_declared_juce_executable_for_copy_verification() {
        let tmp = TestDir::new();
        let spec = pre_vst3();
        let bytes = b"binary payload with PRE Kirin Hypha display identity";
        let source = fake_vst3(
            tmp.path(),
            "Kirin Hypha PRE.vst3",
            &spec.executable_name,
            Some(&spec.executable_name),
            bytes,
        );
        let installed = fake_vst3(
            tmp.path(),
            "PRE Kirin Hypha.vst3",
            &spec.executable_name,
            Some(&spec.executable_name),
            bytes,
        );

        assert!(!installed.join("Contents/MacOS/PRE Kirin Hypha").exists());
        verify_binary_copy(&source, &installed, &spec).unwrap();
        verify_bundle_contract(&installed, &spec).unwrap();
    }

    #[test]
    fn wrong_declared_executable_is_rejected_before_file_probes() {
        let tmp = TestDir::new();
        let spec = pre_vst3();
        let bundle = fake_vst3(
            tmp.path(),
            "PRE Kirin Hypha.vst3",
            "PRE Kirin Hypha",
            Some("PRE Kirin Hypha"),
            b"PRE Kirin Hypha",
        );
        let error = resolve_executable(&bundle, &spec).unwrap_err().to_string();
        assert!(error.contains("CFBundleExecutable"));
        assert!(error.contains("expected Kirin Hypha PRE"));
    }

    #[test]
    fn missing_declared_executable_is_rejected() {
        let tmp = TestDir::new();
        let spec = pre_vst3();
        let bundle = fake_vst3(
            tmp.path(),
            "PRE Kirin Hypha.vst3",
            &spec.executable_name,
            None,
            b"",
        );
        let error = resolve_executable(&bundle, &spec).unwrap_err().to_string();
        assert!(error.contains("is not a file"));
    }

    #[test]
    fn same_size_but_different_binaries_are_rejected() {
        let tmp = TestDir::new();
        let spec = pre_vst3();
        let source = fake_vst3(
            tmp.path(),
            "source.vst3",
            &spec.executable_name,
            Some(&spec.executable_name),
            b"PRE Kirin Hypha AAA",
        );
        let installed = fake_vst3(
            tmp.path(),
            "installed.vst3",
            &spec.executable_name,
            Some(&spec.executable_name),
            b"PRE Kirin Hypha BBB",
        );
        let error = verify_binary_copy(&source, &installed, &spec)
            .unwrap_err()
            .to_string();
        assert!(error.contains("binary mismatch after install"));
    }
}
