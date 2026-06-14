//! B-128 (G-115-370): restore された identity（project_uuid / instance_id / daw_session_id）に
//! 攻撃・破損値（絶対パス / `..` / 非UUID）が来ても **path traversal を起こさない**ための唯一の
//! 検証層。両殻（egui=出荷 VST3 / JUCE=AU）の restore 経路は最終的にすべて本 crate の path builder
//! と io_thread を通るため、ここ一点で両殻を守る（consumer-side choke / G-115-370）。
//!
//! 二層構成:
//! - [`guard_path_component`] — **全 path builder 入口の within-base wall**。path-unsafe な component
//!   （絶対パス / `..` / `/` `\` `\0`）を **deterministic な base 内 quarantine 名**（`_q_<hex>`）へ
//!   畳んで escape を構造的に阻止する（fail-closed・[`surface_path_event`] で通知）。**path-safe な
//!   component（valid UUID も、`"iid-b058-fixed"` のような無害な非UUID literal も）は無改変で通す**
//!   ため、既存の path-format テスト・parity の literal id は不変（zero churn）。
//! - [`materialize_observation_id`] — **観測 family（io_thread）入口**で identity を materialize する。
//!   path-unsafe な値だけ **fresh new_v4** に差し替えて clean な観測継続を与える（§7② / coordination
//!   family は wall の deterministic quarantine を使い、reserve/release/count を coherent に保つ）。
//!
//! ## 設計上の境界（parity 不変との両立）
//! parity / reservation / record_signal などの既存テストは **path-safe な非UUID literal**（`"iid-pre"`
//! 等）を identity に使い、それが path に出ることを assert する。よって「非UUID を一律 new_v4」化は
//! parity を壊す。本層は **path-safety のみを基準**にし、traversal-unsafe な値だけを変換する。これにより
//! 「絶対パス / `..`」（真の traversal）は確実に封じつつ、無害な非UUID は素通し＝既存契約を壊さない。
//!
//! invalid-identity / invalid-path は [`surface_path_event`] で log + 観測可能 sink に出す
//! （R-28: silent swap 禁止）。

use std::borrow::Cow;
use std::path::Path;
use std::sync::{Mutex, OnceLock, RwLock};

use uuid::Uuid;

/// quarantine 名の接頭辞。valid UUID（hex+dash のみ）には決して現れないため、cap count や
/// pairing から quarantine 枠を確実に識別・除外できる（C3）。
pub const QUARANTINE_PREFIX: &str = "_q_";

/// path component の最大長（B-128 G-115-371 D4）。UUID(36) / 既存 literal id は十分収まり、
/// FFI C ABI 識別子バッファ（64）とも整合。超過は unsafe 扱い→quarantine（DoS 級の overlength 拒否）。
pub const MAX_COMPONENT_LEN: usize = 64;

fn event_sink() -> &'static Mutex<Vec<String>> {
    static SINK: OnceLock<Mutex<Vec<String>>> = OnceLock::new();
    SINK.get_or_init(|| Mutex::new(Vec::new()))
}

/// invalid-identity / invalid-path event を surface する（log + 観測可能 sink）。
/// R-28: 信頼境界の異常は silent swap せず必ず可視化する。
pub fn surface_path_event(msg: impl Into<String>) {
    let msg = msg.into();
    log::warn!("[path-identity] {msg}");
    if let Ok(mut g) = event_sink().lock() {
        g.push(msg);
    }
}

/// surface 済み event を drain する（テスト検証 / GUI 表示用）。
pub fn drain_path_events() -> Vec<String> {
    event_sink()
        .lock()
        .map(|mut g| std::mem::take(&mut *g))
        .unwrap_or_default()
}

/// surface 済み event を 1 件だけ pop する（FFI C ABI getter / JUCE status 用・無ければ None）。
pub fn take_path_event() -> Option<String> {
    event_sink().lock().ok().and_then(|mut g| {
        if g.is_empty() {
            None
        } else {
            Some(g.remove(0))
        }
    })
}

/// path component が安全か（traversal / 絶対パス / 区切り / 空 を排除）。
///
/// B-128 cap-bypass 封止: [`QUARANTINE_PREFIX`]（`_q_`）は wall 専用の **予約 marker**。これを含む値は
/// 「safe だが quarantine 名を詐称」できてしまい、`count_frames` の `_q_` 除外（C3）を悪用して 12-pair
/// cap を bypass できる（攻撃者 instance_id が `_q_evil` 等を持つと real 枠が cap 未計数になる）。よって
/// `_q_` を含む値は **unsafe 扱い**にして決定的に quarantine へ畳む（正規 UUID / parity literal は `_q_`
/// を含まないため影響なし）。これで「`_q_` を含む枠 = 必ず wall 由来の真の quarantine」が不変条件になる。
pub fn is_path_safe_component(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= MAX_COMPONENT_LEN // D4: overlength 拒否
        && s != "."
        && s != ".."
        && !s.contains('/')
        && !s.contains('\\')
        && !s.chars().any(|c| c.is_control()) // D4: null byte 含む全制御文字を拒否
        && !s.contains(QUARANTINE_PREFIX)
        && !Path::new(s).is_absolute()
}

/// quarantine 名か（`_q_` を含む）。cap count / pairing から quarantine 枠を除外する判定に使う。
/// valid UUID は hex+dash のみで `_q_` を含まないため誤判定しない。
pub fn is_quarantine_component(s: &str) -> bool {
    s.contains(QUARANTINE_PREFIX)
}

/// raw（path-unsafe）→ **deterministic** な base 内 quarantine 名（`_q_<sha256 先頭8byte hex>`）。
/// 同一 raw は常に同一名 = reserve / release / count が coherent（C3 決定性）。
fn quarantine_name(raw: &str) -> String {
    use sha2::{Digest, Sha256};
    let digest = Sha256::digest(raw.as_bytes());
    let mut hex = String::with_capacity(QUARANTINE_PREFIX.len() + 16);
    hex.push_str(QUARANTINE_PREFIX);
    for b in digest.iter().take(8) {
        hex.push_str(&format!("{b:02x}"));
    }
    hex
}

/// **全 path builder 入口の within-base wall**（DiD・fail-closed）。
/// - safe → 借用を無改変で返す（valid UUID / 安全な literal は不変＝既存テスト無改修）。
/// - unsafe（絶対 / `..` / 区切り）→ invalid-path event + deterministic quarantine 名（base 内）。
///
/// `ctx` は event に出す発火点ラベル（例 `"io_dir.project_hash"`）。
pub fn guard_path_component<'a>(component: &'a str, ctx: &str) -> Cow<'a, str> {
    if is_path_safe_component(component) {
        Cow::Borrowed(component)
    } else {
        surface_path_event(format!(
            "invalid-path: path-unsafe component quarantined at {ctx} (len={})",
            component.len()
        ));
        Cow::Owned(quarantine_name(component))
    }
}

/// **観測 family（io_thread）入口**の identity materialize（唯一の検証点・G-115-370）。
/// - empty（restore 前の正常初期状態）→ silent に new_v4（event なし）。
/// - path-safe な非空値（valid UUID / 無害な literal）→ 無改変（parity literal id 不変）。
/// - path-unsafe（絶対 / `..` / 区切り）→ **fresh new_v4** + invalid-identity event（§7② 観測継続）。
pub fn materialize_observation_id(raw: &str, ctx: &str) -> String {
    if raw.is_empty() {
        return Uuid::new_v4().to_string();
    }
    if is_path_safe_component(raw) {
        return raw.to_string();
    }
    let fresh = Uuid::new_v4().to_string();
    surface_path_event(format!(
        "invalid-identity: path-unsafe {ctx} substituted with fresh {fresh} (len={})",
        raw.len()
    ));
    fresh
}

/// **restore 受領点（FFI set_identity）用の単一 materialize**（B-128 G-115-371）。
/// FFI `self.identity` を本関数で materialize すれば keep / record / 永続化 / io_thread が全て
/// materialize 済 self.identity を読み、family 間分裂（raw 第二源）と uncounted-Record-bypass が
/// 1 点で構造的に消える（unsafe→counted 正規 reservation の new_v4）。
/// - **empty は空のまま返す**（「未設定→enable で生成」契約を保つ・materialize_observation_id と異なる）。
/// - path-safe な非空値（valid UUID / 無害な literal）→ 無改変（parity literal-id テスト不変）。
/// - path-unsafe → fresh new_v4 + invalid-identity event（D2/D3）。
pub fn materialize_restore_field(raw: &str, ctx: &str) -> String {
    if raw.is_empty() {
        return String::new();
    }
    if is_path_safe_component(raw) {
        return raw.to_string();
    }
    let fresh = Uuid::new_v4().to_string();
    surface_path_event(format!(
        "invalid-identity: path-unsafe {ctx} substituted with fresh {fresh} (len={})",
        raw.len()
    ));
    fresh
}

/// **restore 受領点（egui `initialize`）用の同期 cell materialize**（B-128 G-115-371 / D2）。
/// FFI の `set_identity` materialize と対称。`materialize_restore_field` を使う（empty は空のまま＝
/// 「未設定→生成」契約保持）。egui は params(`Arc<RwLock<String>>`)を editor keep と io_thread が共有
/// するため、**io_thread spawn / GUI keep より前**に本関数で params を畳めば、async な io_thread
/// normalize が走る前の窓で uncounted-quarantine Record に入る経路を閉じる（両殻 D2 統一）。
pub fn normalize_restore_cell(cell: &RwLock<String>, ctx: &str) {
    let current = match cell.read() {
        Ok(g) => g.clone(),
        Err(_) => return,
    };
    let materialized = materialize_restore_field(&current, ctx);
    if materialized != current {
        if let Ok(mut g) = cell.write() {
            *g = materialized;
        }
    }
}

/// io_thread が共有する `Arc<RwLock<String>>` 識別子セルを観測 family として materialize する。
/// path-unsafe なら fresh new_v4 を書き戻す（spawn 入口で 1 回）。safe なら無改変（書込なし）。
pub fn normalize_observation_cell(cell: &RwLock<String>, ctx: &str) {
    let current = match cell.read() {
        Ok(g) => g.clone(),
        Err(_) => return,
    };
    let materialized = materialize_observation_id(&current, ctx);
    if materialized != current {
        if let Ok(mut g) = cell.write() {
            *g = materialized;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fresh_events() {
        let _ = drain_path_events();
    }

    // ── wall: path-safety 判定 ───────────────────────────────────────────────
    #[test]
    fn safe_components_pass_through_unchanged() {
        // valid UUID / 無害な非UUID literal（parity の "iid-b058-fixed" 相当）は不変。
        for s in ["a1b2c3d4-0000-4000-8000-000000000000", "iid-b058-fixed", "pre", "ph-A", "not-a-uuid"] {
            assert!(is_path_safe_component(s), "{s} は safe");
            assert_eq!(guard_path_component(s, "t"), Cow::Borrowed(s), "{s} は wall を素通し");
        }
    }

    #[test]
    fn unsafe_components_are_quarantined_within_base() {
        // 末尾 2 件: 予約 marker `_q_` を含む値（cap-bypass 詐称）も unsafe 扱い→決定的 quarantine。
        for s in ["/tmp/x", "../../../../tmp/x", "..", "a/b", "a\\b", ".", "", "_q_evil", "x_q_y"] {
            assert!(!is_path_safe_component(s), "{s:?} は unsafe");
            let g = guard_path_component(s, "t");
            assert!(g.starts_with(QUARANTINE_PREFIX), "{s:?} → quarantine: {g}");
            // quarantine 名は単一 path component で traversal しない（区切り/`..`/絶対なし）。
            assert!(
                !g.contains('/') && !g.contains('\\') && &*g != ".." && !Path::new(&*g).is_absolute(),
                "quarantine 名は base 内 single component: {g}"
            );
            // 詐称防止: quarantine 名は `_q_` + 16 hex の正準形（攻撃値そのものを名前にしない）。
            assert_eq!(g.len(), QUARANTINE_PREFIX.len() + 16, "quarantine 名は正準長: {g}");
            assert!(g[QUARANTINE_PREFIX.len()..].bytes().all(|b| b.is_ascii_hexdigit()));
        }
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
        assert_eq!(materialize_observation_id("iid-b058-fixed", "t"), "iid-b058-fixed");
        assert!(drain_path_events().is_empty(), "safe は event なし");
        // unsafe → fresh new_v4 + event。
        let m = materialize_observation_id("../../../../tmp/x", "t");
        assert!(Uuid::parse_str(&m).is_ok(), "unsafe → valid new uuid (§7② 観測継続)");
        assert!(!is_quarantine_component(&m), "観測 family は quarantine でなく clean new uuid");
        assert!(!drain_path_events().is_empty(), "unsafe は invalid-identity event surface");
    }

    // ── D4: 長さ上限 + 制御文字 ───────────────────────────────────────────────
    #[test]
    fn d4_overlength_and_control_chars_rejected() {
        // valid UUID（36 / printable）は通過。
        assert!(is_path_safe_component("a1b2c3d4-0000-4000-8000-000000000000"));
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
        assert_eq!(materialize_restore_field("", "t"), "");
        assert!(drain_path_events().is_empty(), "empty は silent");
        // safe literal は不変（parity）。
        assert_eq!(materialize_restore_field("iid-b058-fixed", "t"), "iid-b058-fixed");
        assert!(drain_path_events().is_empty());
        // unsafe → fresh new_v4 + event。
        let m = materialize_restore_field("/tmp/x", "t");
        assert!(Uuid::parse_str(&m).is_ok(), "unsafe → new_v4: {m}");
        assert!(!drain_path_events().is_empty(), "unsafe は event surface");
    }

    #[test]
    fn normalize_restore_cell_materializes_unsafe_preserves_safe_empty() {
        fresh_events();
        // unsafe → new_v4 を書き戻し + event。
        let unsafe_cell = RwLock::new("/tmp/x".to_string());
        normalize_restore_cell(&unsafe_cell, "t");
        assert!(Uuid::parse_str(&unsafe_cell.read().unwrap()).is_ok(), "unsafe → new_v4 書き戻し");
        assert!(!drain_path_events().is_empty(), "event surface");
        // safe → 不変（parity）/ empty → 不変（"" / 未設定→生成契約）。
        let safe_cell = RwLock::new("iid-b058-fixed".to_string());
        normalize_restore_cell(&safe_cell, "t");
        assert_eq!(*safe_cell.read().unwrap(), "iid-b058-fixed");
        let empty_cell = RwLock::new(String::new());
        normalize_restore_cell(&empty_cell, "t");
        assert_eq!(*empty_cell.read().unwrap(), "");
        assert!(drain_path_events().is_empty(), "safe/empty は event なし");
    }

    #[test]
    fn take_path_event_pops_one() {
        fresh_events();
        surface_path_event("e1");
        surface_path_event("e2");
        assert_eq!(take_path_event().as_deref(), Some("e1"));
        assert_eq!(take_path_event().as_deref(), Some("e2"));
        assert_eq!(take_path_event(), None);
    }

    #[test]
    fn normalize_cell_rewrites_only_unsafe() {
        fresh_events();
        let safe = RwLock::new("iid-b058-fixed".to_string());
        normalize_observation_cell(&safe, "t");
        assert_eq!(*safe.read().unwrap(), "iid-b058-fixed", "safe は書き換えない（parity 不変）");
        let unsafe_cell = RwLock::new("/tmp/x".to_string());
        normalize_observation_cell(&unsafe_cell, "t");
        let v = unsafe_cell.read().unwrap().clone();
        assert!(Uuid::parse_str(&v).is_ok(), "unsafe は fresh new_v4 に書き換え: {v}");
        assert!(!drain_path_events().is_empty(), "event surface");
    }
}
