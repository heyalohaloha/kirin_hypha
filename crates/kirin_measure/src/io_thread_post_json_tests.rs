use super::*;

/// Active 完全形 (`serialize_post_json` 出力) → PostTmpJson roundtrip。
#[test]
fn deserialize_active_full_roundtrip() {
    let result = MeasureResult {
        lufs_m: Some(-12.0),
        lufs_s: Some(-13.0),
        true_peak: Some(-0.5),
        crest: Some(10.0),
        psr: Some(7.0),
        ..Default::default()
    };
    let json = serialize_post_json(
        "post-iid-A",
        SignalState::Active,
        Some(SignalState::Active),
        &result,
        "PRE-Master",
        123.456,
    );
    let parsed: PostTmpJson = serde_json::from_str(&json).expect("deserialize ok");

    assert_eq!(parsed.v, 2);
    assert_eq!(parsed.role, "POST");
    assert_eq!(parsed.instance_id, "post-iid-A");
    assert_eq!(parsed.signal_state, "active");
    assert_eq!(parsed.pre_signal_state.as_deref(), Some("active"));
    assert!(
        parsed.t.starts_with("20") && parsed.t.ends_with('Z'),
        "ISO 8601 t: {}",
        parsed.t
    );
    assert_eq!(parsed.pair_pre_name, "PRE-Master");
    assert!((parsed.pair_claimed_at - 123.456).abs() < 1e-6);
    assert_eq!(parsed.lufs_m, Some(-12.0));
    assert_eq!(parsed.lufs_s, Some(-13.0));
    assert_eq!(parsed.true_peak, Some(-0.5));
    assert_eq!(parsed.crest, Some(10.0));
    assert_eq!(parsed.psr, Some(7.0));
    assert!(parsed.n_prime_total.is_none());
    assert!(parsed.sharpness.is_none());
    assert!(parsed.psb_summary.is_none());
}

/// B-131 (G-115-380): `"` / `\` を含む pair_pre_name が serde escape され valid JSON に
/// なり値が往復する（旧手組み生補間では不正 JSON を生成し、他 POST の
/// `scan_post_candidates_in` が parse 失敗で無言 skip → pairing 消失していた回帰）。
/// PRE 側 `serialize_pre_json_escapes_quotes_and_backslash` (B-077) と対称。
#[test]
fn serialize_post_json_escapes_quotes_and_backslash() {
    let result = MeasureResult::default();
    let name = "PRE\"x\\y";
    let json = serialize_post_json(
        "post-iid",
        SignalState::Active,
        Some(SignalState::Active),
        &result,
        name,
        1.0,
    );
    let parsed: serde_json::Value = serde_json::from_str(&json)
        .expect("B-131: special-char pair_pre_name must produce valid JSON");
    assert_eq!(parsed["pair_pre_name"].as_str(), Some(name));
}

/// B-131 (G-115-380): minimal でも `"` / `\` を含む pair_pre_name を serde escape する
/// (`serialize_post_json` と対称 / Bypassed・Inactive でも候補化されるため必須)。
#[test]
fn serialize_post_json_minimal_escapes_quotes_and_backslash() {
    let name = "PRE\"x\\y";
    let json = serialize_post_json_minimal("post-iid", SignalState::Bypassed, name, 0.0);
    let parsed: serde_json::Value = serde_json::from_str(&json)
        .expect("B-131: special-char pair_pre_name must produce valid JSON (minimal)");
    assert_eq!(parsed["pair_pre_name"].as_str(), Some(name));
}

/// B-131 (G-115-380): 日本語 pair_pre_name が JSON で保持され読み戻せる
/// (PRE 側 `serialize_pre_json_keeps_japanese_name` と対称 / serde が UTF-8 を維持)。
#[test]
fn serialize_post_json_keeps_japanese_pair_pre_name() {
    let result = MeasureResult::default();
    let name = "日本語PRE";
    let json = serialize_post_json(
        "post-iid",
        SignalState::Active,
        Some(SignalState::Active),
        &result,
        name,
        0.0,
    );
    let parsed: serde_json::Value = serde_json::from_str(&json).unwrap();
    assert_eq!(parsed["pair_pre_name"].as_str(), Some(name));
}

/// B-131 (G-115-380) census-twin: instance_id も serde escape される。restore で `"` を含む
/// instance_id が materialize wall（is_path_safe_component は `"` を拒否しない）を素通っても
/// valid JSON になり、他 POST の scan が parse 失敗 → pairing 消失する同種 R-28 を防ぐ。
#[test]
fn serialize_post_json_escapes_instance_id_quote() {
    let result = MeasureResult::default();
    let iid = "post\"evil\\id";
    let json = serialize_post_json(
        iid,
        SignalState::Active,
        Some(SignalState::Active),
        &result,
        "PRE-Master",
        0.0,
    );
    let parsed: serde_json::Value = serde_json::from_str(&json)
        .expect("B-131: special-char instance_id must produce valid JSON");
    assert_eq!(parsed["instance_id"].as_str(), Some(iid));
}

/// B-131 (G-115-380) census-twin: minimal でも instance_id を serde escape する。
#[test]
fn serialize_post_json_minimal_escapes_instance_id_quote() {
    let iid = "post\"evil\\id";
    let json = serialize_post_json_minimal(iid, SignalState::Bypassed, "PRE-Mix", 0.0);
    let parsed: serde_json::Value = serde_json::from_str(&json)
        .expect("B-131: special-char instance_id must produce valid JSON (minimal)");
    assert_eq!(parsed["instance_id"].as_str(), Some(iid));
}

/// Minimal (`serialize_post_json_minimal` 出力 / Bypassed) → PostTmpJson roundtrip。
/// pre_signal_state / 計測値系は不在 → Option::None で defaulted。
#[test]
fn deserialize_minimal_roundtrip() {
    let json = serialize_post_json_minimal("post-iid-B", SignalState::Bypassed, "PRE-Mix", 99.5);
    let parsed: PostTmpJson = serde_json::from_str(&json).expect("deserialize ok");

    assert_eq!(parsed.instance_id, "post-iid-B");
    assert_eq!(parsed.signal_state, "bypassed");
    assert_eq!(parsed.pair_pre_name, "PRE-Mix");
    assert!(parsed.pre_signal_state.is_none());
    assert!(parsed.lufs_m.is_none());
    assert!(parsed.lufs_s.is_none());
    assert!(parsed.true_peak.is_none());
    assert!(parsed.crest.is_none());
    assert!(parsed.psr.is_none());
}

/// 旧 schema 互換: `pair_pre_name` field 不在 → 空文字 fallback (#[serde(default)])。
#[test]
fn deserialize_legacy_without_pair_pre_name_defaults_empty() {
    let legacy = r#"{"v":2,"role":"POST","instance_id":"old-iid","signal_state":"active","pre_signal_state":"active","t":"2026-05-04T10:00:00.000Z","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
    let parsed: PostTmpJson = serde_json::from_str(legacy).expect("legacy deserialize ok");

    assert_eq!(parsed.instance_id, "old-iid");
    assert_eq!(parsed.signal_state, "active");
    assert_eq!(
        parsed.pair_pre_name, "",
        "pair_pre_name must default to empty for legacy schema"
    );
    assert_eq!(parsed.lufs_m, Some(-14.0));
    assert!(
        parsed.lufs_s.is_none(),
        "old v2 POST JSON without lufs_s must remain readable"
    );
}

/// pair_pre_name が空文字で書込まれた場合の roundtrip (Active の POST が PRE 未選択)。
#[test]
fn deserialize_active_with_empty_pair_pre_name() {
    let result = MeasureResult::default();
    let json = serialize_post_json(
        "post-iid-C",
        SignalState::Active,
        Some(SignalState::Active),
        &result,
        "",
        0.0,
    );
    let parsed: PostTmpJson = serde_json::from_str(&json).expect("deserialize ok");

    assert_eq!(parsed.pair_pre_name, "");
    assert_eq!(parsed.pair_claimed_at, 0.0);
    assert_eq!(parsed.signal_state, "active");
    assert!(parsed.lufs_m.is_none());
}

#[test]
fn deserialize_active_with_phase_d_fields() {
    let result = MeasureResult {
        lufs_m: Some(-10.0),
        true_peak: Some(-0.3),
        crest: Some(11.0),
        psr: Some(7.5),
        n_prime_total: Some(0.42),
        sharpness: Some(1.85),
        psb_summary: Some(crate::PsbSummary {
            low: 0.10,
            mid: 0.20,
            high: 0.30,
        }),
        ..Default::default()
    };
    let json = serialize_post_json(
        "post-iid-D",
        SignalState::Active,
        Some(SignalState::Active),
        &result,
        "PRE-D",
        555.0,
    );
    let parsed: PostTmpJson = serde_json::from_str(&json).expect("deserialize ok");

    assert_eq!(parsed.pair_pre_name, "PRE-D");
    assert_eq!(parsed.n_prime_total, Some(0.42));
    assert_eq!(parsed.sharpness, Some(1.85));
    let psb = parsed.psb_summary.expect("psb_summary present");
    assert!((psb.low - 0.10).abs() < 1e-6);
    assert!((psb.mid - 0.20).abs() < 1e-6);
    assert!((psb.high - 0.30).abs() < 1e-6);
}

/// signal_state や instance_id 等 必須 field 不在 → deserialize エラー。
#[test]
fn deserialize_missing_required_field_errors() {
    let bad = r#"{"v":2,"role":"POST","signal_state":"active","t":"2026-05-04T10:00:00.000Z"}"#;
    let res: Result<PostTmpJson, _> = serde_json::from_str(bad);
    assert!(res.is_err(), "instance_id 不在は err");
}

// ── W-281 / G-115-249 / A-5: pair_claimed_at schema 拡張 テスト ─────────

/// (A-5 i) serialize 出力 (full + minimal) に "pair_claimed_at" リテラル含有。
#[test]
fn post_json_serialize_includes_pair_claimed_at() {
    let json_full = serialize_post_json(
        "post-iid",
        SignalState::Active,
        Some(SignalState::Active),
        &MeasureResult::default(),
        "PRE-X",
        42.0,
    );
    assert!(
        json_full.contains(r#""pair_claimed_at":42"#),
        "full: {}",
        json_full
    );

    let json_min = serialize_post_json_minimal("post-iid", SignalState::Bypassed, "PRE-X", 0.0);
    assert!(
        json_min.contains(r#""pair_claimed_at":0"#),
        "min: {}",
        json_min
    );
}

#[test]
fn post_json_serialize_includes_daw_session_id() {
    let json_full = serialize_post_json_with_daw(
        "post-iid",
        SignalState::Active,
        Some(SignalState::Active),
        &MeasureResult::default(),
        "PRE-X",
        42.0,
        "daw-A",
    );
    assert!(json_full.contains(r#""daw_session_id":"daw-A""#));
    assert!(json_full.contains(&format!(
        r#""host_process_id":{}"#,
        crate::current_host_process_id()
    )));

    let json_min = serialize_post_json_minimal_with_daw(
        "post-iid",
        SignalState::Bypassed,
        "PRE-X",
        0.0,
        "daw-A",
    );
    assert!(json_min.contains(r#""daw_session_id":"daw-A""#));
    assert!(json_min.contains(&format!(
        r#""host_process_id":{}"#,
        crate::current_host_process_id()
    )));
}

/// (A-5 ii) 旧 schema (pair_claimed_at field 不在) deserialize → default=0.0。
#[test]
fn post_json_deserialize_legacy_without_pair_claimed_at() {
    let legacy = r#"{"v":2,"role":"POST","instance_id":"legacy-iid","signal_state":"active","pre_signal_state":"active","t":"2026-05-04T10:00:00.000Z","pair_pre_name":"PRE-Legacy","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
    let parsed: PostTmpJson = serde_json::from_str(legacy).expect("legacy deserialize ok");

    assert_eq!(parsed.pair_pre_name, "PRE-Legacy");
    assert_eq!(
        parsed.pair_claimed_at, 0.0,
        "pair_claimed_at must default to 0.0 for legacy schema"
    );
    assert_eq!(
        parsed.daw_session_id, "",
        "daw_session_id must default to empty for legacy schema"
    );
    assert_eq!(
        parsed.host_process_id, 0,
        "host_process_id must default to 0 for legacy schema"
    );
}
