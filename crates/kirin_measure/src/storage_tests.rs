use super::*;
use std::sync::atomic::{AtomicUsize, Ordering};

static TEST_COUNTER: AtomicUsize = AtomicUsize::new(0);

/// テスト用隔離ディレクトリ。各テストが独立した一時ディレクトリを使う。
fn isolated_paths() -> (StoragePaths, PathBuf) {
    let n = TEST_COUNTER.fetch_add(1, Ordering::SeqCst);
    let pid = std::process::id();
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let root = std::env::temp_dir()
        .join("kirin_hypha_test")
        .join(format!("{}-{}-{}", pid, now, n));
    let _ = fs::remove_dir_all(&root);
    fs::create_dir_all(&root).unwrap();
    (StoragePaths::with_root(&root), root)
}

fn hc(a: &str, b: &str, c: &str) -> HardwareComponents {
    HardwareComponents {
        iop: a.to_string(),
        sn: b.to_string(),
        bd: c.to_string(),
    }
}

#[test]
fn platform_paths_macos_fixture_preserves_current_storage_layout() {
    let paths = PlatformPaths::for_macos("/Users/daisuke", "/tmp");
    let expected_root = PathBuf::from("/Users/daisuke")
        .join("Library")
        .join("Application Support")
        .join("Kirin OS");

    assert_eq!(paths.kind, PlatformKind::MacOS);
    assert_eq!(paths.storage.kirin_os_root, expected_root);
    assert_eq!(
        paths.storage.plugin_data_dir(),
        expected_root.join("plugin_data")
    );
    assert_eq!(
        paths.storage.primary_path(),
        expected_root.join("identity.json")
    );
    assert_eq!(
        paths.storage.secondary_path(),
        expected_root
            .join("plugin_data")
            .join(".identity_backup.json")
    );
    assert_eq!(paths.kirin_tmp_root, PathBuf::from("/tmp").join("kirin"));
}

#[test]
fn platform_paths_windows_fixture_splits_appdata_and_localappdata() {
    let appdata = PathBuf::from(r"C:\Users\daisuke\AppData\Roaming");
    let local_appdata = PathBuf::from(r"C:\Users\daisuke\AppData\Local");
    let temp = PathBuf::from(r"C:\Users\daisuke\AppData\Local\Temp");
    let paths = PlatformPaths::for_windows(appdata.clone(), local_appdata.clone(), temp.clone());
    let expected_identity_root = appdata.join("Kirin OS");
    let expected_plugin_data = local_appdata.join("Kirin OS").join("plugin_data");

    assert_eq!(paths.kind, PlatformKind::Windows);
    assert_eq!(paths.storage.kirin_os_root, expected_identity_root);
    assert_eq!(paths.storage.plugin_data_dir(), expected_plugin_data);
    assert_eq!(
        paths.storage.primary_path(),
        expected_identity_root.join("identity.json")
    );
    assert_eq!(
        paths.storage.secondary_path(),
        expected_plugin_data.join(".identity_backup.json")
    );
    assert_eq!(paths.kirin_tmp_root, temp.join("kirin"));
}

#[test]
fn stage4_fresh_generation_writes_both() {
    let (paths, root) = isolated_paths();
    let loaded = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    assert_eq!(loaded.status, LoadStatus::FreshlyGenerated);
    assert_eq!(loaded.identity.license, License::Os);
    assert!(paths.primary_path().exists());
    assert!(paths.secondary_path().exists());
    let primary = read_identity(&paths.primary_path()).unwrap();
    let secondary = read_identity(&paths.secondary_path()).unwrap();
    assert_eq!(primary.installation_id, secondary.installation_id);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn stage1_primary_ok_returns_same_identity() {
    let (paths, root) = isolated_paths();
    let first = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    let second = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    assert_eq!(second.status, LoadStatus::PrimaryOk);
    assert_eq!(
        first.identity.installation_id,
        second.identity.installation_id
    );
    let _ = fs::remove_dir_all(root);
}

#[test]
fn stage2_secondary_restores_primary() {
    let (paths, root) = isolated_paths();
    let first = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    // 一次削除
    fs::remove_file(paths.primary_path()).unwrap();
    let second = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    assert_eq!(second.status, LoadStatus::RecoveredFromSecondary);
    assert_eq!(
        first.identity.installation_id,
        second.identity.installation_id
    );
    // 一次復元確認
    assert!(paths.primary_path().exists());
    let _ = fs::remove_dir_all(root);
}

#[test]
fn stage3_does_not_scan_plugin_history_for_identity() {
    let (paths, root) = isolated_paths();
    let first = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    let original_id = first.identity.installation_id.clone();

    // 新構造: plugin_data/{project_hash}/{instance_id}/pre/*.json
    let pre_dir = paths
        .plugin_data_dir()
        .join("ph-test")
        .join("iid-test")
        .join("pre");
    fs::create_dir_all(&pre_dir).unwrap();
    let pre_file = pre_dir.join("20260417T120000.json");
    let json = format!(r#"{{"installation_id":"{}","other":"data"}}"#, original_id);
    fs::write(&pre_file, json).unwrap();

    // 一次と二次を削除 → 段階 3 経路を強制
    fs::remove_file(paths.primary_path()).unwrap();
    fs::remove_file(paths.secondary_path()).unwrap();

    let second = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    assert_eq!(second.status, LoadStatus::FreshlyGenerated);
    assert_ne!(second.identity.installation_id, original_id);
    // 一次・二次は新しい同一 identity で復元済み。
    assert!(paths.primary_path().exists());
    assert!(paths.secondary_path().exists());
    let _ = fs::remove_dir_all(root);
}

#[test]
fn stage3_is_independent_of_plugin_history_mtime() {
    let (paths, root) = isolated_paths();
    let pre_dir = paths
        .plugin_data_dir()
        .join("p1")
        .join("iid-newest")
        .join("pre");
    fs::create_dir_all(&pre_dir).unwrap();
    let old_file = pre_dir.join("a.json");
    let new_file = pre_dir.join("b.json");
    fs::write(&old_file, r#"{"installation_id":"old-id"}"#).unwrap();
    std::thread::sleep(std::time::Duration::from_millis(50));
    fs::write(&new_file, r#"{"installation_id":"new-id"}"#).unwrap();

    let loaded = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    assert_eq!(loaded.status, LoadStatus::FreshlyGenerated);
    assert_ne!(loaded.identity.installation_id, "old-id");
    assert_ne!(loaded.identity.installation_id, "new-id");
    let _ = fs::remove_dir_all(root);
}

#[test]
fn different_machine_detected() {
    let (paths, root) = isolated_paths();
    let _ = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    // 別マシンの 3 要素で再起動
    let second = load_or_recover(&paths, hc("X", "Y", "Z"), License::Os).unwrap();
    assert_eq!(second.status, LoadStatus::DifferentMachine);
    assert!(!second.status.allow_measurement());
    let _ = fs::remove_dir_all(root);
}

#[test]
fn two_of_three_matches_counts_as_same() {
    let (paths, root) = isolated_paths();
    let _ = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    // 2 要素一致 (bd のみ変化) → Same
    let second = load_or_recover(&paths, hc("A", "B", "CHANGED"), License::Os).unwrap();
    assert_eq!(second.status, LoadStatus::PrimaryOk);
    assert!(second.status.allow_measurement());
    let _ = fs::remove_dir_all(root);
}

#[test]
fn insufficient_components_permissive_continues() {
    let (paths, root) = isolated_paths();
    // 1 要素のみで登録
    let _ = load_or_recover(&paths, hc("A", "", ""), License::Os).unwrap();
    // 同じ 1 要素で再起動 → Insufficient
    let second = load_or_recover(&paths, hc("A", "", ""), License::Os).unwrap();
    assert_eq!(second.status, LoadStatus::Insufficient);
    assert!(second.status.allow_measurement());
    let _ = fs::remove_dir_all(root);
}

#[test]
fn atomic_write_does_not_leave_tmp() {
    let (paths, root) = isolated_paths();
    let _ = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    assert_eq!(
        crate::atomic_file::remove_temp_siblings(&paths.primary_path()).unwrap(),
        0
    );
    let _ = fs::remove_dir_all(root);
}

#[test]
fn identity_cache_detects_mtime_change() {
    let (paths, root) = isolated_paths();
    let loaded = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    assert_eq!(loaded.identity.license, License::Os);

    let mut cache = IdentityCache::new(paths.clone(), loaded.identity.clone());

    // 一次を license="sense" に書き換える
    let mut modified = loaded.identity.clone();
    modified.license = License::Sense;
    // mtime を確実に変化させる
    std::thread::sleep(std::time::Duration::from_millis(20));
    write_identity_atomic(&paths.primary_path(), &modified).unwrap();

    // キャッシュ経由で再取得 → Sense が返る
    assert_eq!(cache.current_license(), License::Sense);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn stage2_corrupt_primary_falls_through() {
    let (paths, root) = isolated_paths();
    let first = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    // 一次を壊す
    fs::write(paths.primary_path(), "not a json").unwrap();
    let second = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
    assert_eq!(second.status, LoadStatus::RecoveredFromSecondary);
    assert_eq!(
        first.identity.installation_id,
        second.identity.installation_id
    );
    let _ = fs::remove_dir_all(root);
}

// ── load_installation_id_from（サブ3-A-2）─────────────────────────

fn isolated_id_path(name: &str) -> PathBuf {
    let n = TEST_COUNTER.fetch_add(1, Ordering::SeqCst);
    let pid = std::process::id();
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let root = std::env::temp_dir()
        .join("kirin_hypha_id_test")
        .join(format!("{}-{}-{}", pid, now, n));
    let _ = fs::remove_dir_all(&root);
    fs::create_dir_all(&root).unwrap();
    root.join(name)
}

#[test]
fn load_installation_id_reads_valid_field() {
    let path = isolated_id_path("identity.json");
    fs::write(&path, r#"{"installation_id": "abc-123-uuid"}"#).unwrap();
    assert_eq!(
        load_installation_id_from(&path),
        Some("abc-123-uuid".to_string())
    );
}

#[test]
fn load_installation_id_reads_from_full_schema() {
    let path = isolated_id_path("identity.json");
    let full = r#"{
            "schema_version": "1.0",
            "installation_id": "full-schema-uuid",
            "hardware_id": "x",
            "hardware_components": {"iop": "a", "sn": "b", "bd": "c"},
            "machine_signature": "x",
            "license": "os",
            "created_at": "2026-04-19T00:00:00Z",
            "last_verified_at": "2026-04-19T00:00:00Z"
        }"#;
    fs::write(&path, full).unwrap();
    assert_eq!(
        load_installation_id_from(&path),
        Some("full-schema-uuid".to_string())
    );
}

#[test]
fn load_installation_id_returns_none_on_missing_file() {
    let path = isolated_id_path("does_not_exist.json");
    assert_eq!(load_installation_id_from(&path), None);
}

#[test]
fn load_installation_id_returns_none_on_invalid_json() {
    let path = isolated_id_path("identity.json");
    fs::write(&path, "not a json").unwrap();
    assert_eq!(load_installation_id_from(&path), None);
}

#[test]
fn load_installation_id_returns_none_on_missing_field() {
    let path = isolated_id_path("identity.json");
    fs::write(&path, r#"{"license": "os"}"#).unwrap();
    assert_eq!(load_installation_id_from(&path), None);
}

#[test]
fn load_installation_id_returns_none_on_empty_field() {
    let path = isolated_id_path("identity.json");
    fs::write(&path, r#"{"installation_id": ""}"#).unwrap();
    assert_eq!(load_installation_id_from(&path), None);
}

#[test]
fn load_installation_id_returns_none_on_non_string_field() {
    // Guardrail: future schema change storing installation_id as number must fail safely.
    let path = isolated_id_path("identity.json");
    fs::write(&path, r#"{"installation_id": 42}"#).unwrap();
    assert_eq!(load_installation_id_from(&path), None);
}

// ── cleanup_legacy_v1 (1a-6 / Q4) ─────────────────────────────────────

#[test]
fn cleanup_legacy_v1_removes_default_mix_and_writes_flag() {
    let (paths, root) = isolated_paths();
    // 旧構造を擬似的に作成
    let pd = paths.plugin_data_dir();
    let legacy_pre = pd.join("default").join("MIX").join("pre");
    let legacy_post = pd.join("default").join("MIX").join("post");
    let legacy_preset = pd.join("default").join("preset");
    fs::create_dir_all(&legacy_pre).unwrap();
    fs::create_dir_all(&legacy_post).unwrap();
    fs::create_dir_all(&legacy_preset).unwrap();
    fs::write(legacy_pre.join("a.json"), b"{}").unwrap();
    fs::write(legacy_post.join("b.json"), b"{}").unwrap();
    fs::write(legacy_preset.join("c.json"), b"{}").unwrap();

    let report = cleanup_legacy_v1(&paths);
    assert!(report.ran);
    assert_eq!(report.errors, 0);
    assert!(
        report.removed >= 2,
        "MIX + preset must be removed: {report:?}"
    );

    // 旧構造が消えている
    assert!(!pd.join("default").join("MIX").exists());
    assert!(!pd.join("default").join("preset").exists());
    // flag が書かれている
    assert!(paths.kirin_os_root.join(CLEANUP_V1_DONE_FILENAME).exists());

    let _ = fs::remove_dir_all(root);
}

#[test]
fn cleanup_legacy_v1_is_idempotent_via_flag() {
    let (paths, root) = isolated_paths();
    // 初回（旧構造なし）
    let r1 = cleanup_legacy_v1(&paths);
    assert!(r1.ran);
    // 2 回目: flag が立っているのでスキップ
    let r2 = cleanup_legacy_v1(&paths);
    assert!(!r2.ran, "second run must be skipped: {r2:?}");
    assert_eq!(r2.removed, 0);
    let _ = fs::remove_dir_all(root);
}

#[test]
fn cleanup_legacy_v1_preserves_new_structure() {
    let (paths, root) = isolated_paths();
    let pd = paths.plugin_data_dir();
    // 新構造（残しておくべき）
    let new_pre = pd.join("ph-new").join("iid-A").join("pre");
    fs::create_dir_all(&new_pre).unwrap();
    fs::write(new_pre.join("keep.json"), b"{}").unwrap();
    // 旧構造（消えるべき）
    let legacy = pd.join("default").join("MIX").join("pre");
    fs::create_dir_all(&legacy).unwrap();
    fs::write(legacy.join("drop.json"), b"{}").unwrap();

    cleanup_legacy_v1(&paths);

    assert!(
        new_pre.join("keep.json").exists(),
        "new structure preserved"
    );
    assert!(!legacy.exists(), "legacy structure removed");
    let _ = fs::remove_dir_all(root);
}
