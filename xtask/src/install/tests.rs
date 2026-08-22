use super::*;

#[test]
fn bundles_returns_four_au_and_vst3() {
    let bundles = bundles(Path::new(".")).unwrap();
    assert_eq!(bundles.len(), 4);
    let names: Vec<_> = bundles
        .iter()
        .map(|bundle| bundle.file().unwrap())
        .collect();
    for expected in [
        "Kirin Hypha PRE.component",
        "Kirin Hypha POST.component",
        "PRE Kirin Hypha.vst3",
        "POST Kirin Hypha.vst3",
    ] {
        assert!(names.contains(&expected));
    }
}

#[test]
fn role_first_vst3_migration_removes_only_the_two_legacy_vst3_names() {
    let bundles = bundles(Path::new(".")).unwrap();
    let legacy: Vec<_> = bundles
        .iter()
        .flat_map(|bundle| {
            bundle
                .spec
                .legacy_install_relative
                .iter()
                .map(|path| path.file_name().unwrap().to_string_lossy().into_owned())
        })
        .collect();
    assert_eq!(
        legacy,
        vec![
            "Kirin Hypha PRE.vst3".to_string(),
            "Kirin Hypha POST.vst3".to_string()
        ]
    );
    assert!(bundles
        .iter()
        .filter(|bundle| bundle.spec.kind == BundleKind::Au)
        .all(|bundle| bundle.spec.legacy_install_relative.is_empty()));
}

#[test]
fn role_first_vst3_bundle_names_are_distinct_from_declared_executables() {
    let bundles = bundles(Path::new(".")).unwrap();
    let vst3: Vec<_> = bundles
        .iter()
        .filter(|bundle| bundle.spec.kind == BundleKind::Vst3)
        .collect();
    assert_eq!(vst3.len(), 2);
    for bundle in vst3 {
        assert_ne!(
            bundle
                .spec
                .install_relative
                .file_stem()
                .and_then(|name| name.to_str()),
            Some(bundle.spec.executable_name.as_str())
        );
        assert_eq!(
            bundle.source.file_stem().and_then(|name| name.to_str()),
            Some(bundle.spec.executable_name.as_str())
        );
    }
}

#[test]
fn macos_system_destinations_are_manifest_derived() {
    for bundle in bundles(Path::new(".")).unwrap() {
        let destination = bundle.system_dest();
        match bundle.spec.kind {
            BundleKind::Au => {
                assert!(destination.starts_with("/Library/Audio/Plug-Ins/Components"))
            }
            BundleKind::Vst3 => {
                assert!(destination.starts_with("/Library/Audio/Plug-Ins/VST3"))
            }
        }
    }
}

#[test]
fn all_source_paths_use_the_common_juce_build() {
    for bundle in bundles(Path::new(".")).unwrap() {
        let source_components: Vec<_> = bundle.source.iter().collect();
        let contains_pair = |first: &str, second: &str| {
            source_components
                .windows(2)
                .any(|parts| parts[0] == first && parts[1] == second)
        };
        assert!(
            contains_pair("juce_shell", "build-universal"),
            "{}",
            bundle.source.display()
        );
        match bundle.spec.kind {
            BundleKind::Au => assert!(contains_pair("Release", "AU")),
            BundleKind::Vst3 => {
                assert!(contains_pair("Release", "VST3"));
                assert!(!contains_pair("target", "bundled"));
            }
        }
    }
}

#[test]
fn team_id_is_the_developer_id() {
    assert_eq!(TEAM_ID, "7N8BSMA684");
}

#[test]
fn sudo_args_adds_askpass_flag_only_when_requested() {
    assert_eq!(
        sudo_args_for(&["rm", "-rf", "/tmp/x"], false),
        vec!["rm", "-rf", "/tmp/x"]
    );
    assert_eq!(
        sudo_args_for(&["rm", "-rf", "/tmp/x"], true),
        vec!["-A", "rm", "-rf", "/tmp/x"]
    );
}

#[test]
fn user_dest_resolves_under_home_per_format() {
    let original = std::env::var_os("HOME");
    unsafe {
        std::env::set_var("HOME", "/tmp/fake-home");
    }
    for bundle in bundles(Path::new(".")).unwrap() {
        let destination = bundle.user_dest().expect("HOME set");
        assert!(destination.starts_with("/tmp/fake-home/Library/Audio/Plug-Ins"));
        assert_eq!(
            destination.file_name().and_then(|name| name.to_str()),
            Some(bundle.file().unwrap())
        );
    }
    unsafe {
        match original {
            Some(value) => std::env::set_var("HOME", value),
            None => std::env::remove_var("HOME"),
        }
    }
}

#[test]
fn run_rejects_missing_release_flag() {
    let error = run(vec![]).expect_err("must require --release");
    assert!(format!("{error}").contains("--release"));
}

#[test]
fn verify_only_also_requires_release_flag() {
    let error = run(vec!["--verify-only".into()]).expect_err("must require --release");
    assert!(format!("{error}").contains("--release"));
}

#[test]
fn run_rejects_unknown_argument() {
    let error = run(vec!["--unknown".into(), "--release".into()])
        .expect_err("unknown arg must be rejected");
    assert!(format!("{error}").contains("unknown argument"));
}

#[test]
fn verify_signed_bails_on_unsigned() {
    let tmp = std::env::temp_dir().join(format!(
        "kirin_install_unsigned_{}_{}",
        std::process::id(),
        rand::random::<u64>()
    ));
    fs::create_dir_all(&tmp).unwrap();
    let error = verify_signed(&tmp).expect_err("unsigned must be rejected");
    let message = format!("{error}");
    assert!(
        message.contains("Developer-ID") || message.contains(TEAM_ID),
        "error must mention signing requirement, got: {message}"
    );
    let _ = fs::remove_dir_all(&tmp);
}

#[test]
fn remove_user_level_is_idempotent_when_absent() {
    let tmp = std::env::temp_dir().join(format!(
        "kirin_install_idem_{}_{}",
        std::process::id(),
        rand::random::<u64>()
    ));
    remove_user_level(&tmp.join("Kirin Hypha PRE.component")).expect("absent must be ok");
}

#[test]
fn remove_user_level_actually_removes_existing() {
    let tmp = std::env::temp_dir().join(format!(
        "kirin_install_remove_{}_{}",
        std::process::id(),
        rand::random::<u64>()
    ));
    let bundle = tmp.join("Kirin Hypha PRE.component");
    fs::create_dir_all(&bundle).unwrap();
    remove_user_level(&bundle).expect("removal ok");
    assert!(!bundle.exists(), "bundle must be gone");
    let _ = fs::remove_dir_all(&tmp);
}
