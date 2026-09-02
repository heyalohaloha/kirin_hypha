//! POST minus PRE delta resolution and freshness rules.

use super::{NO_PRE_SECS, STALE_SECS};
use crate::delta::{DeltaMode, DeltaResult, DeltaSnapshot};
use crate::record_signal::SIGNALS_SUBDIR;
use crate::{MeasureResult, SignalState};
use std::fs;
use std::path::{Path, PathBuf};

/// PRE ファイルをスキャンして Δ を算出する（後方互換ラッパー / 既存 integration test 用）。
///
/// W-280: pair filter なし版。本ラッパーは `pair_pre_name = None` 相当で呼ぶ。
/// 新規プロダクションコードは `compute_delta_with_state(..., pair_pre_name)` を直接呼ぶこと。
#[doc(hidden)]
pub fn compute_delta(project_dir: &Path, post: &MeasureResult) -> Result<DeltaResult, String> {
    compute_delta_with_state(project_dir, post, None).map(|(delta, _)| delta)
}

/// B-048 / G-115-245 Last Known Good: 新 `DeltaResult` と前回 `last_active` を
/// マージする pure 関数。`run_tick` 内で Mutex lock 取得後に呼ばれる。
///
/// セマンティクス:
/// - `new_delta.mode == Active` → 新 6 field 値で `DeltaSnapshot` 作成 →
///   `last_active = Some(snapshot)`。`prev_last_active` は破棄。
/// - `new_delta.mode == Stale | NoPre` → `prev_last_active` をそのまま保持。
///   新値を `last_active` に書き込まない (PRE pre.json 不在/古い区間で「直近の
///   Active 時の凍結値」を GUI に保持させ続けるため)。
/// - `new_delta.mode == Bypassed` → 明示 OFF なので凍結値を保持しない。
///
/// 純関数のため Mutex / fs 副作用なし → unit test 容易。
/// `run_tick` (Mutex / fs / spawn 副作用あり) は本関数を呼ぶだけのアダプタ。
///
/// 可視性: `pub` は `tests/io_thread_post_test.rs` (kirin_measure crate 外扱いの
/// integration test) から呼ぶための公開。外部 crate consumer (hypha_post 等) は
/// 本関数を直接使う想定無し (`DeltaResult::last_active` を読むのは GUI 側のみ)。
pub fn merge_last_active(
    prev_last_active: Option<DeltaSnapshot>,
    new_delta: DeltaResult,
) -> DeltaResult {
    match new_delta.mode {
        DeltaMode::Active => {
            let snapshot = DeltaSnapshot {
                lufs: new_delta.lufs,
                lufs_s: new_delta.lufs_s,
                psr: new_delta.psr,
                tp: new_delta.tp,
                n_prime_total: new_delta.n_prime_total,
                crest: new_delta.crest,
                sharpness: new_delta.sharpness,
            };
            DeltaResult {
                last_active: Some(snapshot),
                ..new_delta
            }
        }
        DeltaMode::Stale | DeltaMode::NoPre => DeltaResult {
            last_active: prev_last_active,
            ..new_delta
        },
        DeltaMode::Bypassed | DeltaMode::PreInactive => new_delta,
    }
}

fn snapshot_from_delta(d: &DeltaResult) -> Option<DeltaSnapshot> {
    if d.lufs.is_none()
        && d.lufs_s.is_none()
        && d.psr.is_none()
        && d.tp.is_none()
        && d.n_prime_total.is_none()
        && d.crest.is_none()
        && d.sharpness.is_none()
    {
        None
    } else {
        Some(DeltaSnapshot {
            lufs: d.lufs,
            lufs_s: d.lufs_s,
            psr: d.psr,
            tp: d.tp,
            n_prime_total: d.n_prime_total,
            crest: d.crest,
            sharpness: d.sharpness,
        })
    }
}

pub(super) fn resolve_delta_for_non_active_post(
    state: SignalState,
    pair_pre_name: &str,
    previous: &DeltaResult,
) -> DeltaResult {
    if state != SignalState::Inactive || pair_pre_name.trim().is_empty() {
        return DeltaResult::default();
    }

    DeltaResult {
        mode: DeltaMode::Stale,
        last_active: previous
            .last_active
            .clone()
            .or_else(|| snapshot_from_delta(previous)),
        ..DeltaResult::default()
    }
}

/// B-059 / G-115-245 置換: `run_tick` が delta_result に書く値を決める pure 関数。
///
/// - `mode == NoPre | Bypassed | PreInactive`（= 有効ペアなし / PRE OFF）→ **`new_delta` をそのまま**
///   （`DeltaResult::default()` 由来で `last_active = None` ＝ クリア）。表示=commit 一本化で
///   「選定 None なのに直近 Δ が凍結表示される」のを防ぐ（B-048 の NoPre 保持を廃止）。
/// - `mode == Active | Stale`（= 一意有効 PRE を選定）→ `merge_last_active`（Active 保存 /
///   Stale 5-10s は同一有効 pair の凍結値保持＝B-048 維持）。
///
/// 純関数（Mutex/fs 副作用なし）→ unit test 容易。
pub(crate) fn resolve_delta_for_store(
    new_delta: DeltaResult,
    prev_last_active: Option<DeltaSnapshot>,
) -> DeltaResult {
    if matches!(
        new_delta.mode,
        DeltaMode::NoPre | DeltaMode::Bypassed | DeltaMode::PreInactive
    ) {
        new_delta
    } else {
        merge_last_active(prev_last_active, new_delta)
    }
}

/// `$TMPDIR/kirin/{project_hash}/` 配下の全 instance_id サブディレクトリを走査して
/// `pre.json` を集め、Δ を算出する (W-280: pair filter 対応)。
///
/// # pair_pre_name セマンティクス (W-280 / G-115-248)
/// - `Some(target)` (非空文字): 各 pre.json を read → `parsed["name"].as_str()` が
///   `target` に一致するものだけ候補に push する。pair 確立後の Δ 計算経路で
///   2 セット環境の交互 pick 症状を解消する根本対処。
/// - `None` または `Some("")` (filter なし):
///   - project_dir 配下 instance 数が **1 件**なら従来通り pass-through (ZSA: single-PRE
///     後方互換 / pair_pre_name 未設定時の挙動維持)。
///   - **2 件以上**は曖昧として `DeltaMode::NoPre` (R-7 / R-26 沈黙ゲート / 推測でない)。
///
/// # 戻り値
/// - 0 個 → `DeltaMode::NoPre`, `None`
/// - 複数 → 最新 `t` を選択（ISO 8601 は文字列比較で最新判定可 / A-2 上流 filter 済前提）
/// - 鮮度判定 → `Active` / `Stale` / `NoPre`
pub(super) fn compute_delta_with_state(
    project_dir: &Path,
    post: &MeasureResult,
    pair_pre_name: Option<&str>,
) -> Result<(DeltaResult, Option<SignalState>), String> {
    if !project_dir.exists() {
        return Ok((
            DeltaResult {
                mode: DeltaMode::NoPre,
                ..Default::default()
            },
            None,
        ));
    }

    // W-280: 空文字は filter 適用なし扱い (B-027 段階 1 pass-through セマンティクス継承)。
    let pair_target = pair_pre_name.filter(|s| !s.is_empty());

    // W-280: 1 回目の walk で「project_dir 配下の instance 候補 (pre.json 持ち)」を
    // 列挙し、その後 pair filter / pass-through 判定を行う。
    let mut candidates: Vec<PathBuf> = Vec::new();
    let project_entries = fs::read_dir(project_dir).map_err(|e| format!("read_dir: {e}"))?;
    for entry in project_entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        // record_signal/ 等の予約名は instance_id ではない
        if path.file_name().and_then(|n| n.to_str()) == Some(SIGNALS_SUBDIR) {
            continue;
        }
        let candidate = path.join("pre.json");
        if candidate.is_file() {
            candidates.push(candidate);
        }
    }

    // W-280: pair filter — Some(target) なら name 一致のみ / None なら 1 件 pass-through。
    let mut pre_files: Vec<PathBuf> = match pair_target {
        Some(target) => {
            let mut matched = Vec::new();
            for cand in candidates {
                let content = match fs::read_to_string(&cand) {
                    Ok(s) => s,
                    Err(_) => continue,
                };
                let parsed: serde_json::Value = match serde_json::from_str(&content) {
                    Ok(v) => v,
                    Err(_) => continue,
                };
                if parsed.get("name").and_then(|v| v.as_str()) == Some(target) {
                    matched.push(cand);
                }
            }
            matched
        }
        None => {
            // pair 未指定: 1 件のみ pass-through / 2 件以上は曖昧として 0 件 (NoPre 落ち)。
            if candidates.len() == 1 {
                candidates
            } else {
                Vec::new()
            }
        }
    };

    if pre_files.is_empty() {
        return Ok((
            DeltaResult {
                mode: DeltaMode::NoPre,
                ..Default::default()
            },
            None,
        ));
    }

    let best = select_best_pre(&mut pre_files)?;
    compute_delta_for_pre_file(&best, post)
}

/// 選定済みの PRE `pre.json` だけを読んで Δ を算出する。
///
/// Pairing/Arm 側が決めた `LatchedPre::pre_json` を再スキャンで失わないための境界。
/// `compute_delta_with_state` は棚から候補を選ぶ互換入口として残し、Record/ラッチ表示は
/// 本関数へ直接入る。
pub(super) fn compute_delta_for_pre_file(
    pre_json: &Path,
    post: &MeasureResult,
) -> Result<(DeltaResult, Option<SignalState>), String> {
    let content = fs::read_to_string(pre_json).map_err(|e| format!("read PRE file: {e}"))?;
    let parsed: serde_json::Value =
        serde_json::from_str(&content).map_err(|e| format!("parse PRE JSON: {e}"))?;

    let pre_signal_state = parsed["signal_state"].as_str().map(|s| match s {
        "active" => SignalState::Active,
        "bypassed" => SignalState::Bypassed,
        _ => SignalState::Inactive,
    });

    if pre_signal_state != Some(SignalState::Active) {
        return Ok((
            DeltaResult {
                lufs: None,
                lufs_s: None,
                psr: None,
                tp: None,
                n_prime_total: None,
                crest: None,
                sharpness: None,
                mode: match pre_signal_state {
                    Some(SignalState::Bypassed) => DeltaMode::Bypassed,
                    Some(SignalState::Inactive) => DeltaMode::PreInactive,
                    _ => DeltaMode::NoPre,
                },
                last_active: None, // B-048 §4-2: run_tick で merge する責務分業
            },
            pre_signal_state,
        ));
    }

    let mode = freshness_mode(&parsed)?;
    if mode == DeltaMode::NoPre {
        return Ok((
            DeltaResult {
                mode: DeltaMode::NoPre,
                ..Default::default()
            },
            pre_signal_state,
        ));
    }

    let pre_lufs = parsed["lufs_m"].as_f64();
    let pre_lufs_s = parsed["lufs_s"].as_f64();
    let pre_psr = parsed["psr"].as_f64();
    let pre_tp = parsed["true_peak"].as_f64();
    // (io_thread_pre.rs:856-862 / opt_f64 ではなく conditional concat)、欠落時の
    // `serde_json::Value::Null` も `as_f64() == None` で素直に Δ=None になる。
    let pre_n_prime_total = parsed["n_prime_total"].as_f64();
    let pre_crest = parsed["crest"].as_f64();
    let pre_sharpness = parsed["sharpness"].as_f64();

    let delta_lufs = post.lufs_m.zip(pre_lufs).map(|(p, r)| p - r);
    let delta_lufs_s = post.lufs_s.zip(pre_lufs_s).map(|(p, r)| p - r);
    let delta_psr = post.psr.zip(pre_psr).map(|(p, r)| p - r);
    let delta_tp = post.true_peak.zip(pre_tp).map(|(p, r)| p - r);
    let delta_n = post
        .n_prime_total
        .zip(pre_n_prime_total)
        .map(|(p, r)| p - r);
    let delta_crest = post.crest.zip(pre_crest).map(|(p, r)| p - r);
    let delta_sharpness = post.sharpness.zip(pre_sharpness).map(|(p, r)| p - r);

    Ok((
        DeltaResult {
            lufs: delta_lufs,
            lufs_s: delta_lufs_s,
            psr: delta_psr,
            tp: delta_tp,
            n_prime_total: delta_n,
            crest: delta_crest,
            sharpness: delta_sharpness,
            mode,
            last_active: None, // B-048 §4-2: run_tick で merge する責務分業
        },
        pre_signal_state,
    ))
}

/// pre.json リストから最新 `t` フィールドを持つファイルを返す。
fn select_best_pre(files: &mut Vec<PathBuf>) -> Result<PathBuf, String> {
    if files.len() == 1 {
        return Ok(files.remove(0));
    }

    let mut best_path: Option<PathBuf> = None;
    let mut best_t = String::new();

    for path in files.iter() {
        let content = match fs::read_to_string(path) {
            Ok(c) => c,
            Err(_) => continue,
        };
        let parsed: serde_json::Value = match serde_json::from_str(&content) {
            Ok(v) => v,
            Err(_) => continue,
        };
        let t = parsed["t"].as_str().unwrap_or("").to_string();
        if t > best_t {
            best_t = t;
            best_path = Some(path.clone());
        }
    }

    best_path.ok_or_else(|| "no valid PRE file found".to_string())
}

fn freshness_mode(parsed: &serde_json::Value) -> Result<DeltaMode, String> {
    let t_str = parsed["t"]
        .as_str()
        .ok_or_else(|| "PRE JSON missing 't' field".to_string())?;

    let pre_time = chrono::DateTime::parse_from_rfc3339(t_str)
        .map_err(|e| format!("parse PRE timestamp: {e}"))?;

    let now = chrono::Utc::now();
    let age_secs = (now - pre_time.with_timezone(&chrono::Utc)).num_seconds();

    let mode = if age_secs >= NO_PRE_SECS {
        DeltaMode::NoPre
    } else if age_secs >= STALE_SECS {
        DeltaMode::Stale
    } else {
        DeltaMode::Active
    };

    // B-047 / G-115-247: NoPre / Stale 観測ログ (UI 異常診断用)。
    // instance_id は PRE 側 serialize_pre_json が書き込む "instance_id" field を引く。
    // freshness_mode は io_thread_post tick (100ms) ごとに呼ばれるため、
    // Stale/NoPre 継続中はログが洪水になる。Phase 1 観察フェーズ後は edge-triggered
    // (前回 mode との比較) または log level 引き下げを別 commit で検討する。
    if mode != DeltaMode::Active {
        let iid = parsed
            .get("instance_id")
            .and_then(|v| v.as_str())
            .unwrap_or("?");
        log::warn!(
            "[freshness] PRE pre.json {:?}: age_secs={} instance_id={}",
            mode,
            age_secs,
            iid
        );
    }

    Ok(mode)
}
