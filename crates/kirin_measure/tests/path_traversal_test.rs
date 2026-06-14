//! B-128 (G-115-370): restore path traversal 封止の検収（C1〜C3 + §7）。
//!
//! 両殻（egui=出荷 VST3 / JUCE=AU）の restore 経路は最終的に本 crate の path builder と io_thread を
//! 通る。egui は `set_project_uuid`/`set_daw_session_id`（kirin_measure cell）+ `params.instance_id` を
//! 直接 builder/io_thread に渡し、JUCE は FFI `set_identity` 経由で同じ builder に到達する。よって本
//! crate の builder wall + materialize を検証すれば両殻の restore 値を end-to-end で守れる。

use std::path::{Path, PathBuf};

use kirin_measure::all_keep_signal;
use kirin_measure::all_stop_signal;
use kirin_measure::io_thread_pre::io_dir;
use kirin_measure::path_identity::{drain_path_events, is_path_safe_component, is_quarantine_component};
use kirin_measure::plugin_data::{Role, WriterPaths};
use kirin_measure::record_signal;
use kirin_measure::reservation::{count_frames, reserve_pairing_at, ReserveOutcome};
use kirin_measure::{materialize_observation_id, process_project_hash, set_project_uuid, MAX_ACTIVE_PER_PROJECT};

/// 3 攻撃ケース（§7）: (i)絶対パス (ii)../traversal (iii)非UUID。
const ABS: &str = "/tmp/x";
const TRAVERSAL: &str = "../../../../tmp/x";
const NON_UUID: &str = "not-a-uuid";
/// 真の traversal（絶対 / ..）のみ。non-UUID(safe) は traversal でないため別扱い。
const TRAVERSAL_ATTACKS: [&str; 2] = [ABS, TRAVERSAL];

fn isolated_base() -> PathBuf {
    use std::sync::atomic::{AtomicU64, Ordering};
    static C: AtomicU64 = AtomicU64::new(0);
    let n = C.fetch_add(1, Ordering::Relaxed);
    let d = std::env::temp_dir().join(format!("kirin_b128_{}_{}", std::process::id(), n));
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

/// 構築 path が base を **構造的に逸脱しない**ことを検証する（lexical な `..`/絶対も含めて）。
fn assert_within_base(path: &Path, base: &Path) {
    assert!(path.starts_with(base), "path が base を逸脱: path={path:?} base={base:?}");
    // 念のため component に `..` / RootDir（先頭以外）が混入しないこと。
    for c in path.strip_prefix(base).unwrap().components() {
        assert!(
            matches!(c, std::path::Component::Normal(_)),
            "base 配下に非 Normal component: {c:?} in {path:?}"
        );
    }
}

// ── §7 + C2: builder 直接注入（materialize bypass）で全 builder が base 内に留まる ──────
#[test]
fn c2_all_builders_quarantine_traversal_within_base() {
    let base = isolated_base();
    for atk in TRAVERSAL_ATTACKS {
        let _ = drain_path_events();

        // io_dir（Watch pre.json builder）— base = $TMPDIR/kirin。
        let kirin_root = std::env::temp_dir().join("kirin");
        let p = io_dir(atk, atk);
        assert_within_base(&p, &kirin_root);

        // WriterPaths::build（Record builder）。
        let wp = WriterPaths::build(&base, atk, atk, Role::Pre, "2026-06-14T00:00:00Z");
        assert_within_base(&wp.final_path, &base);

        // record_signal / all_keep / all_stop signal_path（全 coordination family）。
        assert_within_base(&record_signal::signal_path(&base, atk, atk), &base);
        assert_within_base(&all_keep_signal::signal_path(&base, atk, atk), &base);
        assert_within_base(&all_stop_signal::stop_signal_path(&base, atk, atk), &base);

        // 発火 = invalid-path event surface（silent drop 禁止）。
        assert!(
            !drain_path_events().is_empty(),
            "{atk}: builder wall は invalid-path event を surface する"
        );
    }
}

// ── §7(iii): 非UUID(path-safe) は traversal でない → base 内素通し（観測継続）─────────
#[test]
fn s7_non_uuid_safe_value_stays_within_base() {
    let base = isolated_base();
    assert!(is_path_safe_component(NON_UUID), "not-a-uuid は path-safe（traversal でない）");
    // builder は素通し（base/not-a-uuid/...）= within base・観測は当該値で継続。
    let wp = WriterPaths::build(&base, NON_UUID, NON_UUID, Role::Pre, "2026-06-14T00:00:00Z");
    assert_within_base(&wp.final_path, &base);
    assert!(
        wp.final_path.to_string_lossy().contains(NON_UUID),
        "safe な非UUID は素通し（parity literal id と同じ扱い）"
    );
    // 観測 family materialize も safe 値は不変（parity 不変の肝）。
    assert_eq!(materialize_observation_id(NON_UUID, "t"), NON_UUID);
}

// ── §7 egui 経路 end-to-end: set_project_uuid（egui restore 入口）→ builder が base 内 ──────
#[test]
fn s7_egui_set_project_uuid_path_is_within_base() {
    // egui 殻の restore: params.project_uuid（#[persist]）→ set_project_uuid(cell) → io_thread が
    // process_project_hash() を project_hash として builder へ。攻撃値を set してもその値で構築される
    // path は wall で base 内に留まる。within-base は cell の値に依らず常に成立（並行テスト安全）。
    let kirin_root = std::env::temp_dir().join("kirin");
    set_project_uuid(ABS.to_string());
    let ph = process_project_hash(); // = io_thread に渡る project_hash（egui 経路）
    let p = io_dir(&ph, "iid");
    assert_within_base(&p, &kirin_root);
    // io_thread 観測 family は materialize で絶対パスを fresh new_v4 に差し替え（§7② 観測継続）。
    let m = materialize_observation_id(ABS, "egui.project_uuid");
    assert!(uuid::Uuid::parse_str(&m).is_ok(), "観測 family は new_v4 で継続: {m}");
}

// ── C1: valid UUID は全 builder で同一 canonical（family 間分岐ゼロ）──────────────────
#[test]
fn c1_valid_uuid_is_identical_canonical_across_families() {
    let base = isolated_base();
    let uuid = "a1b2c3d4-1111-4222-8333-444455556666"; // valid canonical UUID
    let _ = drain_path_events();

    // io_thread path（観測）/ record_signal / all_keep / reservation のいずれも uuid を無改変で含む。
    let wp = WriterPaths::build(&base, uuid, uuid, Role::Pre, "2026-06-14T00:00:00Z");
    assert!(wp.final_path.to_string_lossy().contains(uuid));
    assert!(record_signal::signal_path(&base, uuid, uuid).to_string_lossy().contains(uuid));
    assert!(all_keep_signal::signal_path(&base, uuid, uuid).to_string_lossy().contains(uuid));
    // reservation: valid UUID pairing は quarantine されず正規 cap に数える。
    reserve_pairing_at(&base, uuid, uuid, uuid, chrono::Utc::now()).unwrap();
    assert_eq!(count_frames(&base, uuid), 1, "valid UUID 枠は正規 cap に数える");
    // 観測 family materialize も valid UUID を無改変（= path / pairing 同一値）。
    assert_eq!(materialize_observation_id(uuid, "t"), uuid);
    assert!(drain_path_events().is_empty(), "valid UUID は event を出さない（分岐ゼロ）");
}

// ── C3: traversal 注入が正規 12-pair cap を消費/汚染しない（bypass/DoS 不可）+ 決定性 ──────
#[test]
fn c3_quarantine_does_not_pollute_cap_and_is_deterministic() {
    let base = isolated_base();
    let ph = "c0ffee00-1111-4222-8333-444455556666"; // 正規 project（valid UUID）
    let now = chrono::Utc::now();

    // 正規 12 pair で満杯。
    for i in 0..MAX_ACTIVE_PER_PROJECT {
        let id = format!("aaaaaaaa-0000-4000-8000-{i:012x}");
        reserve_pairing_at(&base, ph, &id, &id, now).unwrap();
    }
    assert_eq!(count_frames(&base, ph), MAX_ACTIVE_PER_PROJECT, "正規 12 で満杯");

    // 攻撃者が traversal 値で多数 reserve → quarantine 枠は cap に数えない（DoS 不可）。
    for atk in TRAVERSAL_ATTACKS {
        let r = reserve_pairing_at(&base, ph, atk, atk, now).unwrap();
        // 決定性: 同じ攻撃値の 2 度目は AlreadyReserved（同一 quarantine 名 → coherent）。
        let r2 = reserve_pairing_at(&base, ph, atk, atk, now).unwrap();
        assert!(
            matches!(r, ReserveOutcome::Created) && matches!(r2, ReserveOutcome::AlreadyReserved),
            "{atk}: quarantine 枠は deterministic（reserve→AlreadyReserved coherent）"
        );
    }
    // cap は 12 のまま（quarantine 枠は `_q_` を含み count_frames が除外）。bypass も DoS も不可。
    assert_eq!(
        count_frames(&base, ph),
        MAX_ACTIVE_PER_PROJECT,
        "quarantine 枠は正規 cap を消費/汚染しない（cap=12 維持）"
    );

    // quarantine 枠ファイルは base 内・`_q_` 命名で物理存在（traversal していない）。
    let res_dir = base.join(ph).join("record_reservation");
    let mut quarantined = 0usize;
    for e in std::fs::read_dir(&res_dir).unwrap().flatten() {
        let name = e.file_name().to_string_lossy().to_string();
        if is_quarantine_component(&name) {
            quarantined += 1;
            assert_within_base(&e.path(), &base);
        }
    }
    assert!(quarantined >= TRAVERSAL_ATTACKS.len(), "traversal 値は quarantine 枠として base 内に隔離");
}

// ── C3 cap-bypass 封止: 予約 marker `_q_` を含む値で cap を bypass できない ───────────────
#[test]
fn c3_reserved_marker_literal_cannot_bypass_cap() {
    // 攻撃: instance_id が path-safe な `_q_*` literal を持つと、guard が無改変で通し real 枠を作るが
    // count_frames の `_q_` 除外で未計数 → 12-pair cap を bypass できてしまう（review 指摘の high）。
    // 封止後: `_q_` を含む値は unsafe 扱い → 決定的 quarantine（攻撃値そのものは枠名にならない）。
    let base = isolated_base();
    let ph = "dddddddd-1111-4222-8333-444455556666";
    let now = chrono::Utc::now();
    for i in 0..MAX_ACTIVE_PER_PROJECT {
        let id = format!("bbbbbbbb-0000-4000-8000-{i:012x}");
        reserve_pairing_at(&base, ph, &id, &id, now).unwrap();
    }
    assert_eq!(count_frames(&base, ph), MAX_ACTIVE_PER_PROJECT, "正規 12 で満杯");
    // marker を詐称する 50 個の値で reserve しても正規 cap は不変（全て quarantine 化）。
    for i in 0..50 {
        let atk = format!("_q_attacker_{i}");
        assert!(
            !is_path_safe_component(&atk),
            "予約 marker を含む値は unsafe 扱い（詐称封止）"
        );
        reserve_pairing_at(&base, ph, &atk, &atk, now).unwrap();
    }
    assert_eq!(
        count_frames(&base, ph),
        MAX_ACTIVE_PER_PROJECT,
        "marker 詐称値でも正規 12-pair cap を bypass できない"
    );
    // 攻撃値そのものが枠名に出ていない（決定的 quarantine 名で隔離）。
    let res_dir = base.join(ph).join("record_reservation");
    for e in std::fs::read_dir(&res_dir).unwrap().flatten() {
        let name = e.file_name().to_string_lossy().to_string();
        assert!(!name.contains("attacker"), "攻撃 literal は枠名に現れない: {name}");
    }
}
