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
use std::sync::{Mutex, OnceLock, RwLock};

use uuid::Uuid;

/// quarantine 名の接頭辞。valid UUID（hex+dash のみ）には決して現れないため、cap count や
/// pairing から quarantine 枠を確実に識別・除外できる（C3）。
pub const QUARANTINE_PREFIX: &str = "_q_";

/// path component の最大長（B-128 G-115-371 D4）。UUID(36) / 既存 literal id は十分収まり、
/// FFI C ABI 識別子バッファ（64）とも整合。超過は unsafe 扱い→quarantine（DoS 級の overlength 拒否）。
pub const MAX_COMPONENT_LEN: usize = 64;

/// surface された path-identity anomaly（B-128 G-115-373 / D3）。
/// `instance` が `Some` = 当該 instance の **materialize event**（per-instance routing / set_identity・
/// normalize_restore_cell で instance_id 既知）。`None` = instance context のない **wall event**
/// （engine 深部の builder / global・honest under-specify＝特定 instance を偽らない）。
/// 本番は PRE/POST が別 cdylib ＝ sink は role 毎に別（role 分離は構造的）。
struct PathEvent {
    instance: Option<String>,
    msg: String,
}

fn event_sink() -> &'static Mutex<Vec<PathEvent>> {
    static SINK: OnceLock<Mutex<Vec<PathEvent>>> = OnceLock::new();
    SINK.get_or_init(|| Mutex::new(Vec::new()))
}

/// instance context **なし**の anomaly（wall event 等）を **global** に surface する。
/// global event は最初に drain した editor（同 role の任意 instance）が surface する
/// （honest under-specify: 特定 instance を偽らない・R-28 silent swap 禁止）。
pub fn surface_path_event(msg: impl Into<String>) {
    surface_path_event_for(None, msg);
}

/// instance context **つき**の anomaly（materialize event）を surface する。
/// `instance=Some(id)` で当該 instance の editor のみが drain する（per-instance routing / D3）。
pub fn surface_path_event_for(instance: Option<&str>, msg: impl Into<String>) {
    let msg = msg.into();
    match instance {
        Some(id) => log::warn!("[path-identity instance={id}] {msg}"),
        None => log::warn!("[path-identity] {msg}"),
    }
    if let Ok(mut g) = event_sink().lock() {
        g.push(PathEvent {
            instance: instance.map(str::to_string),
            msg,
        });
    }
}

/// surface 済み event を全て drain する（テスト検証用・instance 問わず msg のみ）。
pub fn drain_path_events() -> Vec<String> {
    event_sink()
        .lock()
        .map(|mut g| std::mem::take(&mut *g).into_iter().map(|e| e.msg).collect())
        .unwrap_or_default()
}

/// surface 済み event を 1 件 pop する（殻 UI 用 / D3 per-instance routing）。
/// `my_instance=Some(id)`: 当該 instance の materialize event（`instance==Some(id)`）か wall event
/// （`instance==None`）の最初の 1 件。`my_instance=None`: wall event（global）のみ。
/// **他 instance の tagged event は返さない**（false instance attribution 防止）。
pub fn take_path_event(my_instance: Option<&str>) -> Option<String> {
    let mut g = event_sink().lock().ok()?;
    let pos = g.iter().position(|e| match (e.instance.as_deref(), my_instance) {
        (None, _) => true,                    // global wall event は誰でも surface
        (Some(ev), Some(mine)) => ev == mine, // 自 instance の materialize event
        (Some(_), None) => false,             // 他 instance 専用 → None caller には返さない
    })?;
    Some(g.remove(pos).msg)
}

/// canonical path-component char policy（**単一定義** / B-133 G-115-383）: ASCII 英数字 + `-`。
/// UUID v4（hex+hyphen）/ parity literal（`iid-b058-fixed` 等）は全て充足。この allowlist は旧 denylist を
/// **subsume** する: `/` `\` / 制御文字（null 含む）/ `.`（→ `.`/`..` 自動拒否）/ 絶対パス（`/`・`:`・`\`）/
/// `_`（→ QUARANTINE_PREFIX `_q_`）。hardening 本体 = 旧 denylist が許していた `"` `<` `>` `&` `;` `|` `$`
/// 空白 `.` `_` 非ASCII を一貫拒否し、**文字方針を本関数 1 点に集約**する（散在 denylist の正規化 /
/// 個別 char patch でない）。within-base 不変は HOLDS（traversal でない・DiD）。
fn is_path_safe_char(c: char) -> bool {
    c.is_ascii_alphanumeric() || c == '-'
}

/// path component が安全か（B-133: allowlist 1 点正規化）。
///
/// B-128 cap-bypass 封止: [`QUARANTINE_PREFIX`]（`_q_`）は wall 専用の **予約 marker**。`_` が allowlist
/// 外のため `_q_` を含む値は allowlist で既に unsafe だが、cap-bypass 不変（`count_frames` の C3 `_q_`
/// 除外）が allowlist の `_` 除外に **暗黙依存しない**よう explicit guard を残す（DiD / 正規 UUID・parity
/// literal は `_q_` を含まないため影響なし）。「`_q_` を含む枠 = 必ず wall 由来の真の quarantine」が不変。
pub fn is_path_safe_component(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= MAX_COMPONENT_LEN // D4: overlength 拒否
        && s.chars().all(is_path_safe_char) // B-133: 文字方針を allowlist 1 点に正規化（'/' '\' 制御 '.'/'..' 絶対 を subsume）
        && !s.contains(QUARANTINE_PREFIX) // B-128 C3 不変を明示保持（'_' 除外で subsume 済だが DiD）
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
///
/// `instance_tag` で anomaly を per-instance routing する（D3 / 当該 instance の UI へ）。
pub fn materialize_restore_field(raw: &str, ctx: &str, instance_tag: Option<&str>) -> String {
    if raw.is_empty() {
        return String::new();
    }
    if is_path_safe_component(raw) {
        return raw.to_string();
    }
    let fresh = Uuid::new_v4().to_string();
    surface_path_event_for(
        instance_tag,
        format!(
            "invalid-identity: path-unsafe {ctx} substituted with fresh {fresh} (len={})",
            raw.len()
        ),
    );
    fresh
}

/// **restore 受領点（egui `initialize`）用の同期 cell materialize**（B-128 G-115-371 / D2）。
/// FFI の `set_identity` materialize と対称。`materialize_restore_field` を使う（empty は空のまま＝
/// 「未設定→生成」契約保持）。egui は params(`Arc<RwLock<String>>`)を editor keep と io_thread が共有
/// するため、**io_thread spawn / GUI keep より前**に本関数で params を畳めば、async な io_thread
/// normalize が走る前の窓で uncounted-quarantine Record に入る経路を閉じる（両殻 D2 統一）。
pub fn normalize_restore_cell(cell: &RwLock<String>, ctx: &str, instance_tag: Option<&str>) {
    let current = match cell.read() {
        Ok(g) => g.clone(),
        Err(_) => return,
    };
    let materialized = materialize_restore_field(&current, ctx, instance_tag);
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
    use std::path::Path; // B-133: absolute 検査を production から外したため test 局所 import に移動

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

    // ── B-133 (G-115-383): allowlist char-policy hardening ──────────────────
    #[test]
    fn b133_newly_rejected_chars_are_quarantined() {
        // 旧 denylist が許していた shell / HTML / display メタ文字・'.'・'_'・空白・非ASCII は
        // allowlist（[A-Za-z0-9-] 1 点）で一貫拒否され、guard で base 内・正準長の quarantine へ畳まれる。
        // いずれも旧 denylist（'/' '\\' 制御 '.' '..' '_q_' 絶対のみ拒否）では safe 扱いだった = hardening 本体。
        for s in [
            "a\"b", "a<b", "a>b", "a&b", "a;b", "a|b", "a$b", "a b", "a.b", "a_b",
            "a`b", "a'b", "a(b", "a)b", "a#b", "a%b", "a=b", "日本語Snare",
        ] {
            assert!(!is_path_safe_component(s), "{s:?} は B-133 allowlist で unsafe であるべき");
            let g = guard_path_component(s, "b133");
            assert!(g.starts_with(QUARANTINE_PREFIX), "{s:?} → quarantine: {g}");
            assert_eq!(g.len(), QUARANTINE_PREFIX.len() + 16, "quarantine は正準長: {g}");
            assert!(
                g[QUARANTINE_PREFIX.len()..].bytes().all(|b| b.is_ascii_hexdigit()),
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
            assert_eq!(materialize_observation_id(&uuid, "b133"), uuid, "正規 UUID は materialize 不変: {uuid}");
            if n == 0 {
                eprintln!("[b133 false-rejection guard] sample fresh uuid={uuid} → is_path_safe=true / guard=Borrowed / materialize 不変");
            }
            n += 1;
        }
        // parity literal id（hex+hyphen でない英数+hyphen literal）も素通し（既存 id テスト不変）。
        for s in ["iid-b058-fixed", "pre", "ph-A", "not-a-uuid", "a1b2c3d4-0000-4000-8000-000000000000"] {
            assert!(is_path_safe_component(s), "parity literal は safe: {s}");
            assert_eq!(guard_path_component(s, "b133"), Cow::Borrowed(s), "parity literal 素通し: {s}");
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
        assert_eq!(materialize_restore_field("", "t", None), "");
        assert!(drain_path_events().is_empty(), "empty は silent");
        // safe literal は不変（parity）。
        assert_eq!(materialize_restore_field("iid-b058-fixed", "t", None), "iid-b058-fixed");
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
        assert!(Uuid::parse_str(&unsafe_cell.read().unwrap()).is_ok(), "unsafe → new_v4 書き戻し");
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
        assert_eq!(a, vec!["materialize-A".to_string(), "wall-global".to_string()]);
        // B は自分の event のみ残っている（global は A が drain 済）。
        assert_eq!(take_path_event(Some("inst-B")).as_deref(), Some("materialize-B"));
        assert_eq!(take_path_event(Some("inst-B")), None);
        // 他 instance の tagged event は None caller には出ない。
        assert_eq!(take_path_event(None), None);
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
