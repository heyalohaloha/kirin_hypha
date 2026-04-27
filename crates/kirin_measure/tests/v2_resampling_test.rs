//! guardian_101 v2 / G-100-02 適合テスト V2-01〜V2-06。
//!
//! 各主要 SR で `PluginDataWriter` が次の v2 仕様を満たすことを構造的に確認する:
//! 1. `source_format` フィールドが入力 SR の実値を保持する (Phase D の有効/無効に
//!    関わらず常にメタ情報として残す)
//! 2. `Frame.n_prime` / `Frame.sharpness` が `Some` で書き出される (silent skip 撤去)
//! 3. `psb_snapshots` が `Some(Vec)` で初期化され、`append_psb` 後にスナップショット
//!    が積まれる
//!
//! これらは Writer 層 (plugin_data.rs) の挙動を検証する単体テストである。
//! Measure Thread 内のリサンプリング経路 (resampler.rs / measure_thread.rs)
//! のスモークテストは src/resampler.rs 内の `mod tests` に配置される。

use kirin_measure::{PluginDataFile, PluginDataRole as Role, PluginDataWriter, WriterPaths};
use std::fs;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};

fn isolated_dir(label: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_v2_resampling_{label}_{pid}_{n}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn run_v2_assertion(sample_rate: u32, label: &str) {
    let base = isolated_dir(label);
    let paths = WriterPaths::build(
        &base,
        "project_hash_test",
        "MIX",
        Role::Pre,
        "2026-04-17T14:32:08Z",
    );
    // 統合テストからは PluginDataWriter.paths が private のため、final_path だけ
    // 別途取り回す。WriterPaths::build は決定論的なのでコピーで OK。
    let final_path = paths.final_path.clone();
    let mut w = PluginDataWriter::create(
        paths,
        "11111111-2222-4333-8444-555555555555".to_string(),
        "project_hash_test".to_string(),
        Role::Pre,
        "MIX".to_string(),
        sample_rate,
    )
    .expect("create writer");

    // 構造的前提: 初期化直後に source_format が実 SR を保持し、
    // psb_snapshots は Some(Vec::new()) で初期化されていること
    assert_eq!(
        w.data().source_format,
        sample_rate,
        "source_format must preserve real SR"
    );
    assert_eq!(
        w.data().psb_snapshots.as_ref().map(|v| v.len()),
        Some(0),
        "psb_snapshots must be Some(empty Vec) on init"
    );

    // Phase D 値の append_frame と append_psb
    w.append_frame(0, [1.5; 20], 1.5, -14.0, -1.0, 12.0);
    w.append_psb(0, [-10.0; 20], true);
    w.flush().expect("flush");

    // ファイルを読み戻して JSON 上での値を確認
    let bytes = fs::read(&final_path).expect("read final");
    let loaded: PluginDataFile = serde_json::from_slice(&bytes).expect("parse JSON");

    // V2 合格基準#3: source_format が実 SR を保持
    assert_eq!(
        loaded.source_format, sample_rate,
        "[V2 SR={sample_rate}] source_format must equal input SR"
    );

    // V2 合格基準: n_prime / sharpness / psb_snapshots が Some
    assert!(
        loaded.frames[0].n_prime.is_some(),
        "[V2 SR={sample_rate}] n_prime must be Some"
    );
    assert!(
        loaded.frames[0].sharpness.is_some(),
        "[V2 SR={sample_rate}] sharpness must be Some"
    );
    assert!(
        loaded.psb_snapshots.is_some(),
        "[V2 SR={sample_rate}] psb_snapshots must be Some"
    );
    assert_eq!(
        loaded.psb_snapshots.as_ref().unwrap().len(),
        1,
        "[V2 SR={sample_rate}] psb_snapshots must contain 1 entry after append_psb"
    );

    // LUFS-M / TP / Crest は通常通り記録される (R-12 計測継続)
    assert_eq!(loaded.frames[0].lufs_m, -14.0);
    assert_eq!(loaded.frames[0].true_peak, -1.0);
    assert_eq!(loaded.frames[0].crest, 12.0);

    // JSON 文字列上で `"source_format":<SR>` が含まれることを確認
    let json_str = std::str::from_utf8(&bytes).expect("utf-8");
    assert!(
        json_str.contains(&format!("\"source_format\":{sample_rate}")),
        "[V2 SR={sample_rate}] JSON must contain source_format literal"
    );
}

/// V2-01: 44.1 kHz
#[test]
fn v2_01_44100() {
    run_v2_assertion(44_100, "44100");
}

/// V2-02: 48.0 kHz
#[test]
fn v2_02_48000() {
    run_v2_assertion(48_000, "48000");
}

/// V2-03: 88.2 kHz
#[test]
fn v2_03_88200() {
    run_v2_assertion(88_200, "88200");
}

/// V2-04: 96.0 kHz
#[test]
fn v2_04_96000() {
    run_v2_assertion(96_000, "96000");
}

/// V2-05: 176.4 kHz
#[test]
fn v2_05_176400() {
    run_v2_assertion(176_400, "176400");
}

/// V2-06: 192.0 kHz
#[test]
fn v2_06_192000() {
    run_v2_assertion(192_000, "192000");
}
