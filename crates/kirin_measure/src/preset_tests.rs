use super::*;
use std::sync::atomic::{AtomicU64, Ordering};

fn isolated_dir() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_preset_test_{pid}_{n}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn sample_region() -> Region {
    Region {
        start_sec: 151.4,
        end_sec: 151.6,
        metric: "sharpness".to_string(),
        bark_band: None,
        value: 3.2,
        delta: 0.6,
        threshold: 2.5,
        threshold_type: "absolute".to_string(),
        threshold_source: "severity_L3".to_string(),
        confidence: "ESTIMATED".to_string(),
    }
}

/// checksum が空のテンプレ。compute_preset_checksum で後から埋める。
fn new_preset_template(installation_id: &str) -> PresetFile {
    PresetFile {
        schema_version: PresetFile::SCHEMA_VERSION.to_string(),
        type_tag: PresetFile::TYPE_TAG.to_string(),
        installation_id: installation_id.to_string(),
        bounce_id: "bounce-xyz".to_string(),
        checksum: String::new(),
        regions: vec![sample_region()],
    }
}

/// 正規 preset（HMAC 埋込済み）を作成。
fn signed_preset(installation_id: &str) -> PresetFile {
    let mut p = new_preset_template(installation_id);
    p.checksum = compute_preset_checksum(&p);
    p
}

fn write_preset_file(base: &Path, ph: &str, filename: &str, preset: &PresetFile) -> PathBuf {
    let dir = preset_dir(base, ph);
    fs::create_dir_all(&dir).unwrap();
    let path = dir.join(filename);
    fs::write(&path, serde_json::to_vec(preset).unwrap()).unwrap();
    path
}

// ── スキーマ ────────────────────────────────────────────

#[test]
fn preset_roundtrip_with_type_field_rename() {
    let p = signed_preset("iid-1");
    let json = serde_json::to_string(&p).unwrap();
    // JSON キーは `type`（rename 確認）
    assert!(
        json.contains(r#""type":"kirin_plugin_preset""#),
        "type key must be rendered: {json}"
    );
    let back: PresetFile = serde_json::from_str(&json).unwrap();
    assert_eq!(back, p);
}

#[test]
fn region_serializes_bark_band_null() {
    let r = sample_region(); // bark_band = None
    let json = serde_json::to_string(&r).unwrap();
    assert!(json.contains(r#""bark_band":null"#), "null: {json}");
}

// ── 検証 ────────────────────────────────────────────────

#[test]
fn valid_preset_verifies() {
    let p = signed_preset("iid-own");
    assert_eq!(verify_preset(&p, "iid-own"), Ok(()));
}

#[test]
fn installation_id_mismatch_rejected() {
    let p = signed_preset("iid-other");
    assert_eq!(
        verify_preset(&p, "iid-own"),
        Err(VerifyError::InstallationIdMismatch)
    );
}

#[test]
fn tampered_checksum_rejected() {
    let mut p = signed_preset("iid-own");
    p.checksum = "0".repeat(64);
    assert_eq!(
        verify_preset(&p, "iid-own"),
        Err(VerifyError::ChecksumMismatch)
    );
}

#[test]
fn tampered_region_value_fails_checksum() {
    let mut p = signed_preset("iid-own");
    p.regions[0].value = 999.0; // 改ざん
    assert_eq!(
        verify_preset(&p, "iid-own"),
        Err(VerifyError::ChecksumMismatch)
    );
}

#[test]
fn wrong_schema_version_rejected() {
    let mut p = new_preset_template("iid-own");
    p.schema_version = "2.0".to_string();
    p.checksum = compute_preset_checksum(&p);
    match verify_preset(&p, "iid-own") {
        Err(VerifyError::SchemaInvalid(msg)) => {
            assert!(msg.contains("schema_version"), "msg: {msg}")
        }
        other => panic!("expected SchemaInvalid, got {other:?}"),
    }
}

#[test]
fn wrong_type_tag_rejected() {
    let mut p = new_preset_template("iid-own");
    p.type_tag = "other_type".to_string();
    p.checksum = compute_preset_checksum(&p);
    match verify_preset(&p, "iid-own") {
        Err(VerifyError::SchemaInvalid(msg)) => assert!(msg.contains("type"), "msg: {msg}"),
        other => panic!("expected SchemaInvalid, got {other:?}"),
    }
}

// ── FS 走査（R-28 沈黙ゲート）────────────────────────────

#[test]
fn scan_empty_dir_returns_empty_vec() {
    let base = isolated_dir();
    let v = scan_valid_presets(&base, "ph", "iid-own");
    assert!(v.is_empty());
}

#[test]
fn scan_returns_only_verified_presets() {
    let base = isolated_dir();
    // 有効
    let good = signed_preset("iid-own");
    write_preset_file(&base, "ph", "a.json", &good);
    // HMAC NG
    let mut tampered = signed_preset("iid-own");
    tampered.regions[0].value = 99.0;
    write_preset_file(&base, "ph", "b.json", &tampered);
    // installation_id NG
    let foreign = signed_preset("iid-other-machine");
    write_preset_file(&base, "ph", "c.json", &foreign);

    let v = scan_valid_presets(&base, "ph", "iid-own");
    assert_eq!(v.len(), 1, "only the good preset passes both gates");
    assert_eq!(v[0], good);
}

#[test]
fn scan_ignores_corrupt_json_silently() {
    let base = isolated_dir();
    let dir = preset_dir(&base, "ph");
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join("broken.json"), b"{{not json").unwrap();

    let good = signed_preset("iid-own");
    write_preset_file(&base, "ph", "ok.json", &good);

    let v = scan_valid_presets(&base, "ph", "iid-own");
    assert_eq!(v.len(), 1);
}

#[test]
fn scan_ignores_tmp_and_non_json_files() {
    let base = isolated_dir();
    let dir = preset_dir(&base, "ph");
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join("notes.txt"), b"hi").unwrap();
    fs::write(dir.join("in_progress.json.tmp"), b"{}").unwrap();
    let good = signed_preset("iid-own");
    write_preset_file(&base, "ph", "ok.json", &good);

    let v = scan_valid_presets(&base, "ph", "iid-own");
    assert_eq!(v.len(), 1);
}

#[test]
fn scan_returns_sorted_order() {
    let base = isolated_dir();
    let g1 = signed_preset("iid-own");
    let g2 = signed_preset("iid-own");
    write_preset_file(&base, "ph", "z.json", &g1);
    write_preset_file(&base, "ph", "a.json", &g2);
    let v = scan_valid_presets(&base, "ph", "iid-own");
    // ファイル名 "a.json" < "z.json" → a が先
    assert_eq!(v.len(), 2);
    // 両方内容同じなので identity では比較不能。順序を作るため a の bounce_id を変更
    // → 下記別テストで詳細検証
}

#[test]
fn scan_sort_stable_by_filename() {
    let base = isolated_dir();
    let mut p1 = new_preset_template("iid-own");
    p1.bounce_id = "FIRST".to_string();
    p1.checksum = compute_preset_checksum(&p1);
    let mut p2 = new_preset_template("iid-own");
    p2.bounce_id = "SECOND".to_string();
    p2.checksum = compute_preset_checksum(&p2);
    write_preset_file(&base, "ph", "b.json", &p2);
    write_preset_file(&base, "ph", "a.json", &p1);
    let v = scan_valid_presets(&base, "ph", "iid-own");
    assert_eq!(v[0].bounce_id, "FIRST");
    assert_eq!(v[1].bounce_id, "SECOND");
}

#[test]
fn scan_missing_dir_is_silent() {
    // project_hash ディレクトリ自体が存在しないケース（新規プロジェクト等）
    let base = isolated_dir();
    let v = scan_valid_presets(&base, "no_such_ph", "iid-own");
    assert!(v.is_empty());
}

// ── 解消判定（改善 I）────────────────────────────────────

#[test]
fn region_resolved_absolute_below_threshold() {
    let mut r = sample_region();
    r.threshold_type = "absolute".into();
    r.threshold = 2.5;
    // value < threshold → 解消
    assert!(region_resolved(&r, 2.4));
    // value == threshold → 未解消（厳密 <）
    assert!(!region_resolved(&r, 2.5));
    // value > threshold → 未解消
    assert!(!region_resolved(&r, 3.0));
}

#[test]
fn region_resolved_delta_absolute_value() {
    let mut r = sample_region();
    r.threshold_type = "delta".into();
    r.threshold = 1.0;
    assert!(region_resolved(&r, 0.5));
    assert!(region_resolved(&r, -0.5));
    assert!(!region_resolved(&r, 1.0));
    assert!(!region_resolved(&r, -1.5));
}

#[test]
fn region_resolved_unknown_type_never_claims_resolution() {
    let mut r = sample_region();
    r.threshold_type = "something_new".into();
    r.threshold = 1.0;
    assert!(!region_resolved(&r, 0.0));
}

#[test]
fn region_resolved_nan_or_inf_returns_false() {
    let r = sample_region();
    assert!(!region_resolved(&r, f64::NAN));
    assert!(!region_resolved(&r, f64::INFINITY));
    assert!(!region_resolved(&r, f64::NEG_INFINITY));
}

// ── 統合 ────────────────────────────────────────────────

#[test]
fn full_silence_gate_all_invalid_means_led_off() {
    // R-28: 全部不合格 → 空 Vec → LED 消灯判定
    let base = isolated_dir();
    let foreign = signed_preset("other-machine");
    write_preset_file(&base, "ph", "a.json", &foreign);
    let mut tampered = signed_preset("iid-own");
    tampered.checksum = "0".repeat(64);
    write_preset_file(&base, "ph", "b.json", &tampered);
    fs::write(preset_dir(&base, "ph").join("c.json"), b"garbage").unwrap();

    let v = scan_valid_presets(&base, "ph", "iid-own");
    assert!(v.is_empty(), "silence gate must reject all");
}
