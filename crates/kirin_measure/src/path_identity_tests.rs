use super::*;
use std::path::Path; // B-133: absolute 検査を production から外したため test 局所 import に移動

fn fresh_events() {
    let _ = drain_path_events();
}

// ── wall: path-safety 判定 ───────────────────────────────────────────────
#[test]
fn safe_components_pass_through_unchanged() {
    // valid UUID / 無害な非UUID literal（parity の "iid-b058-fixed" 相当）は不変。
    for s in [
        "a1b2c3d4-0000-4000-8000-000000000000",
        "iid-b058-fixed",
        "pre",
        "ph-A",
        "not-a-uuid",
    ] {
        assert!(is_path_safe_component(s), "{s} は safe");
        assert_eq!(
            guard_path_component(s, "t"),
            Cow::Borrowed(s),
            "{s} は wall を素通し"
        );
    }
}

#[test]
fn unsafe_components_are_quarantined_within_base() {
    // 末尾 2 件: 予約 marker `_q_` を含む値（cap-bypass 詐称）も unsafe 扱い→決定的 quarantine。
    for s in [
        "/tmp/x",
        "../../../../tmp/x",
        "..",
        "a/b",
        "a\\b",
        ".",
        "",
        "_q_evil",
        "x_q_y",
    ] {
        assert!(!is_path_safe_component(s), "{s:?} は unsafe");
        let g = guard_path_component(s, "t");
        assert!(g.starts_with(QUARANTINE_PREFIX), "{s:?} → quarantine: {g}");
        // quarantine 名は単一 path component で traversal しない（区切り/`..`/絶対なし）。
        assert!(
            !g.contains('/') && !g.contains('\\') && &*g != ".." && !Path::new(&*g).is_absolute(),
            "quarantine 名は base 内 single component: {g}"
        );
        // 詐称防止: quarantine 名は `_q_` + 16 hex の正準形（攻撃値そのものを名前にしない）。
        assert_eq!(
            g.len(),
            QUARANTINE_PREFIX.len() + 16,
            "quarantine 名は正準長: {g}"
        );
        assert!(g[QUARANTINE_PREFIX.len()..]
            .bytes()
            .all(|b| b.is_ascii_hexdigit()));
    }
}

// ── B-133 (G-115-383): allowlist char-policy hardening ──────────────────
#[test]
fn b133_newly_rejected_chars_are_quarantined() {
    // 旧 denylist が許していた shell / HTML / display メタ文字・'.'・'_'・空白・非ASCII は
    // allowlist（[A-Za-z0-9-] 1 点）で一貫拒否され、guard で base 内・正準長の quarantine へ畳まれる。
    // いずれも旧 denylist（'/' '\\' 制御 '.' '..' '_q_' 絶対のみ拒否）では safe 扱いだった = hardening 本体。
    for s in [
        "a\"b",
        "a<b",
        "a>b",
        "a&b",
        "a;b",
        "a|b",
        "a$b",
        "a b",
        "a.b",
        "a_b",
        "a`b",
        "a'b",
        "a(b",
        "a)b",
        "a#b",
        "a%b",
        "a=b",
        "日本語Snare",
    ] {
        assert!(
            !is_path_safe_component(s),
            "{s:?} は B-133 allowlist で unsafe であるべき"
        );
        let g = guard_path_component(s, "b133");
        assert!(g.starts_with(QUARANTINE_PREFIX), "{s:?} → quarantine: {g}");
        assert_eq!(
            g.len(),
            QUARANTINE_PREFIX.len() + 16,
            "quarantine は正準長: {g}"
        );
        assert!(
            g[QUARANTINE_PREFIX.len()..]
                .bytes()
                .all(|b| b.is_ascii_hexdigit()),
            "quarantine 名は _q_ + 16 hex: {g}"
        );
    }
}

#[test]
fn b133_legitimate_ids_not_false_rejected() {
    // false-rejection ガード: 正規 Uuid v4 由来 instance_id は allowlist を素通し（誤拒否しない）。
    // guard は借用無改変・materialize は不変。32 本の fresh UUID で誤拒否ゼロを確認。
    let mut n = 0u32;
    for _ in 0..32 {
        let uuid = Uuid::new_v4().to_string();
        assert!(is_path_safe_component(&uuid), "正規 UUID は safe: {uuid}");
        assert_eq!(
            guard_path_component(&uuid, "b133"),
            Cow::Borrowed(uuid.as_str()),
            "正規 UUID は wall 素通し（誤拒否なし）: {uuid}"
        );
        assert_eq!(
            materialize_observation_id(&uuid, "b133"),
            uuid,
            "正規 UUID は materialize 不変: {uuid}"
        );
        if n == 0 {
            eprintln!("[b133 false-rejection guard] sample fresh uuid={uuid} → is_path_safe=true / guard=Borrowed / materialize 不変");
        }
        n += 1;
    }
    // parity literal id（hex+hyphen でない英数+hyphen literal）も素通し（既存 id テスト不変）。
    for s in [
        "iid-b058-fixed",
        "pre",
        "ph-A",
        "not-a-uuid",
        "a1b2c3d4-0000-4000-8000-000000000000",
    ] {
        assert!(is_path_safe_component(s), "parity literal は safe: {s}");
        assert_eq!(
            guard_path_component(s, "b133"),
            Cow::Borrowed(s),
            "parity literal 素通し: {s}"
        );
    }
    eprintln!("[b133 false-rejection guard] {n}/32 fresh Uuid::new_v4 pass-through（誤拒否ゼロ）+ parity literal 5 種 pass-through OK");
}

#[test]
fn quarantine_is_deterministic() {
    // C3: 同一 raw → 同一 quarantine 名（reserve/release/count coherent）。
    assert_eq!(quarantine_name("../../etc"), quarantine_name("../../etc"));
    assert_ne!(quarantine_name("../../etc"), quarantine_name("/tmp/x"));
}

// ── materialize: 観測 family ─────────────────────────────────────────────
#[test]
fn materialize_preserves_safe_and_news_up_unsafe() {
    fresh_events();
    // empty → silent new_v4（event なし）。
    let m = materialize_observation_id("", "t");
    assert!(Uuid::parse_str(&m).is_ok(), "empty → valid new uuid");
    assert!(drain_path_events().is_empty(), "empty は silent");
    // safe literal → 不変（parity 不変の肝）。
    assert_eq!(
        materialize_observation_id("iid-b058-fixed", "t"),
        "iid-b058-fixed"
    );
    assert!(drain_path_events().is_empty(), "safe は event なし");
    // unsafe → fresh new_v4 + event。
    let m = materialize_observation_id("../../../../tmp/x", "t");
    assert!(
        Uuid::parse_str(&m).is_ok(),
        "unsafe → valid new uuid (§7② 観測継続)"
    );
    assert!(
        !is_quarantine_component(&m),
        "観測 family は quarantine でなく clean new uuid"
    );
    assert!(
        !drain_path_events().is_empty(),
        "unsafe は invalid-identity event surface"
    );
}

// ── D4: 長さ上限 + 制御文字 ───────────────────────────────────────────────
#[test]
fn d4_overlength_and_control_chars_rejected() {
    // valid UUID（36 / printable）は通過。
    assert!(is_path_safe_component(
        "a1b2c3d4-0000-4000-8000-000000000000"
    ));
    // 境界: ちょうど MAX は safe / MAX+1 は unsafe。
    assert!(is_path_safe_component(&"a".repeat(MAX_COMPONENT_LEN)));
    let long = "a".repeat(MAX_COMPONENT_LEN + 1);
    assert!(!is_path_safe_component(&long), "overlength は unsafe");
    assert!(guard_path_component(&long, "t").starts_with(QUARANTINE_PREFIX));
    // 制御文字（改行/タブ/null/bell）→ unsafe → quarantine。
    for s in ["a\nb", "a\tb", "a\0b", "\u{7}bell"] {
        assert!(!is_path_safe_component(s), "{s:?} は制御文字含み unsafe");
        assert!(guard_path_component(s, "t").starts_with(QUARANTINE_PREFIX));
    }
}

// ── restore 受領点 materialize（empty 保持）──────────────────────────────
#[test]
fn materialize_restore_field_preserves_empty_and_safe() {
    fresh_events();
    // empty は空のまま（「未設定→enable 生成」契約）。event なし。
    assert_eq!(materialize_restore_field("", "t", None), "");
    assert!(drain_path_events().is_empty(), "empty は silent");
    // safe literal は不変（parity）。
    assert_eq!(
        materialize_restore_field("iid-b058-fixed", "t", None),
        "iid-b058-fixed"
    );
    assert!(drain_path_events().is_empty());
    // unsafe → fresh new_v4 + event。
    let m = materialize_restore_field("/tmp/x", "t", None);
    assert!(Uuid::parse_str(&m).is_ok(), "unsafe → new_v4: {m}");
    assert!(!drain_path_events().is_empty(), "unsafe は event surface");
}

#[test]
fn normalize_restore_cell_materializes_unsafe_preserves_safe_empty() {
    fresh_events();
    // unsafe → new_v4 を書き戻し + event。
    let unsafe_cell = RwLock::new("/tmp/x".to_string());
    normalize_restore_cell(&unsafe_cell, "t", None);
    assert!(
        Uuid::parse_str(&unsafe_cell.read().unwrap()).is_ok(),
        "unsafe → new_v4 書き戻し"
    );
    assert!(!drain_path_events().is_empty(), "event surface");
    // safe → 不変（parity）/ empty → 不変（"" / 未設定→生成契約）。
    let safe_cell = RwLock::new("iid-b058-fixed".to_string());
    normalize_restore_cell(&safe_cell, "t", None);
    assert_eq!(*safe_cell.read().unwrap(), "iid-b058-fixed");
    let empty_cell = RwLock::new(String::new());
    normalize_restore_cell(&empty_cell, "t", None);
    assert_eq!(*empty_cell.read().unwrap(), "");
    assert!(drain_path_events().is_empty(), "safe/empty は event なし");
}

#[test]
fn take_path_event_pops_one() {
    fresh_events();
    surface_path_event("e1"); // global (instance=None)
    surface_path_event("e2");
    assert_eq!(take_path_event(None).as_deref(), Some("e1"));
    assert_eq!(take_path_event(None).as_deref(), Some("e2"));
    assert_eq!(take_path_event(None), None);
}

// ── D3: per-instance routing（materialize event は当該 instance のみ・wall は global）──────
#[test]
fn take_path_event_routes_per_instance() {
    fresh_events();
    surface_path_event_for(Some("inst-A"), "materialize-A"); // A の materialize event
    surface_path_event_for(Some("inst-B"), "materialize-B"); // B の materialize event
    surface_path_event("wall-global"); // wall event（instance=None）

    // A は自分の event + global を取れる。B の event は取れない（false attribution 防止）。
    let a1 = take_path_event(Some("inst-A"));
    let a2 = take_path_event(Some("inst-A"));
    let mut a = [a1, a2].into_iter().flatten().collect::<Vec<_>>();
    a.sort();
    assert_eq!(
        a,
        vec!["materialize-A".to_string(), "wall-global".to_string()]
    );
    // B は自分の event のみ残っている（global は A が drain 済）。
    assert_eq!(
        take_path_event(Some("inst-B")).as_deref(),
        Some("materialize-B")
    );
    assert_eq!(take_path_event(Some("inst-B")), None);
    // 他 instance の tagged event は None caller には出ない。
    assert_eq!(take_path_event(None), None);
}

#[test]
fn normalize_cell_rewrites_only_unsafe() {
    fresh_events();
    let safe = RwLock::new("iid-b058-fixed".to_string());
    normalize_observation_cell(&safe, "t");
    assert_eq!(
        *safe.read().unwrap(),
        "iid-b058-fixed",
        "safe は書き換えない（parity 不変）"
    );
    let unsafe_cell = RwLock::new("/tmp/x".to_string());
    normalize_observation_cell(&unsafe_cell, "t");
    let v = unsafe_cell.read().unwrap().clone();
    assert!(
        Uuid::parse_str(&v).is_ok(),
        "unsafe は fresh new_v4 に書き換え: {v}"
    );
    assert!(!drain_path_events().is_empty(), "event surface");
}
