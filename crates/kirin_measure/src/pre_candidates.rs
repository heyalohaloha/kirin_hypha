//! PRE candidate scanning and legacy distance selection.
//!
//! This module owns `/tmp/kirin/{project_uuid}/{pre_instance_id}/pre.json` scanning. It does not
//! read or write `record_signal` files and should stay usable from both pairing display and keep
//! paths.

use serde::Deserialize;
use std::fs;
use std::path::{Path, PathBuf};

const RECORD_SIGNAL_RESERVED_DIR: &str = "record_signal";

/// `/tmp/kirin/{ph}/{instance_id}/pre.json` 1 件分のパース結果。
#[derive(Debug, Clone, PartialEq)]
pub struct PreCandidate {
    pub instance_id: String,
    pub lufs_m: Option<f64>,
    pub true_peak: Option<f64>,
    pub crest: Option<f64>,
    pub path: PathBuf,
    /// B-027 段階 2: PRE 表示用 Name (PRE GUI で入力 / chunk-persist)。
    /// 旧 schema (`name` field 不在の PRE 出力) では `None`。
    /// POST `pair_pre_name` filter で照合する識別子。
    pub name: Option<String>,
}

/// pre tmp JSON の抜粋（distance 計算 + signal_state filter に必要なフィールドのみ）。
///
/// B-024: `signal_state` を追加。
/// B-027 段階 1: `scan_pre_candidates_in` は `"bypassed"` のみ除外する (Bypass 防御)。
/// `"active"` / `"inactive"` / 旧 schema 不在 (=None) は pair 候補化される。
/// B-027 段階 2: `name` 追加。POST `pair_pre_name` filter で照合する識別子。
/// pre.json schema バージョン (`v`) は変更しない (旧 PRE 出力との互換維持)。
#[derive(Debug, Deserialize)]
struct PreTmpJson {
    instance_id: String,
    #[serde(default)]
    lufs_m: Option<f64>,
    #[serde(default)]
    true_peak: Option<f64>,
    #[serde(default)]
    crest: Option<f64>,
    #[serde(default)]
    signal_state: Option<String>,
    #[serde(default)]
    name: Option<String>,
}

/// `/tmp/kirin/{project_hash}/{instance_id}/pre.json` を全 instance_id 横断で走査。
///
/// `tmp_base` は通常 `PlatformPaths::current_kirin_tmp_root()`。走査失敗・パース失敗は
/// silently skip。返値は instance_id 辞書順（再現性確保）。
///
/// B-021 補足: 本関数は `tmp_base.join(project_hash)` 配下のみ scan する。
/// cdylib 隔離下で POST の `project_hash` が PRE と一致しない場合、結果は空に
/// なる。新コードでは `pre_discovery::discover_active_pre_dir` で project_dir
/// を動的検出してから [`scan_pre_candidates_in`] を直接呼ぶこと。
pub fn scan_pre_candidates(tmp_base: &Path, project_hash: &str) -> Vec<PreCandidate> {
    scan_pre_candidates_in(&tmp_base.join(project_hash))
}

/// 指定された `{project_uuid}/` dir 配下の `pre.json` を走査する。
///
/// `scan_pre_candidates` 内部実装かつ B-021 Phase 1A の Keep ハンドラ修正用 API。
/// `project_dir` は `pre_discovery::discover_active_pre_dir` の戻り値（または
/// 同等 path）を渡す。`record_signal/` 予約名 dir は除外する。
pub fn scan_pre_candidates_in(project_dir: &Path) -> Vec<PreCandidate> {
    let entries = match fs::read_dir(project_dir) {
        Ok(e) => e,
        Err(_) => return Vec::new(),
    };
    let mut out = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        // record_signal/ ディレクトリは除外
        if path.file_name().and_then(|n| n.to_str()) == Some(RECORD_SIGNAL_RESERVED_DIR) {
            continue;
        }
        let pre_file = path.join("pre.json");
        let Ok(bytes) = fs::read(&pre_file) else {
            continue;
        };
        // B-077: serde 失敗（破損 / 旧形式 pre.json）を無言 skip せずログに残す（沈黙をやめる）。
        // UI には出さない（R-28: 他 instance の pre.json 不整合は利用者操作と非紐づき / log のみ可視化）。
        let parsed: PreTmpJson = match serde_json::from_slice(&bytes) {
            Ok(p) => p,
            Err(e) => {
                log::warn!(
                    "[pairing] skip unparseable pre.json {}: {}",
                    pre_file.display(),
                    e
                );
                continue;
            }
        };
        // B-027: Bypassed の PRE のみ pair 候補から除外。
        // Active / Inactive / 旧 schema (signal_state 不在) は候補化する。
        // 読込側 filter (二重防御の片側)。書込側 guard は
        // io_thread_pre.rs::poll_record_signal の signal_state チェックで担保。
        if parsed.signal_state.as_deref() == Some("bypassed") {
            continue;
        }
        out.push(PreCandidate {
            instance_id: parsed.instance_id,
            lufs_m: parsed.lufs_m,
            true_peak: parsed.true_peak,
            crest: parsed.crest,
            path: pre_file,
            name: parsed.name,
        });
    }
    out.sort_by(|a, b| a.instance_id.cmp(&b.instance_id));
    out
}

/// B-027 段階 2: POST 側 `pair_pre_name` で候補を filter する。
///
/// `pair_pre_name` が空文字の場合は filter を適用せず候補をそのまま返す
/// (後方互換 = 従来 `pick_closest_pre` 単独経路)。
/// 非空の場合は `candidate.name == Some(pair_pre_name)` のみ通過させる。
///
/// 同一 Name の PRE が複数残った場合は呼出側 `pick_closest_pre` で距離最小を選ぶ。
/// 旧 schema (`name = None`) の PRE は非空 filter 時に必ず除外される
/// (新 POST + 旧 PRE 混在環境では filter 不一致 → 0 件)。
pub fn filter_candidates_by_name(
    candidates: Vec<PreCandidate>,
    pair_pre_name: &str,
) -> Vec<PreCandidate> {
    if pair_pre_name.is_empty() {
        return candidates;
    }
    candidates
        .into_iter()
        .filter(|c| c.name.as_deref() == Some(pair_pre_name))
        .collect()
}

/// B-027 段階 3-A 修正 (G-115-49 / α-2 撤回): 全 active PRE dir を flatten 列挙する
/// (POST GUI ComboBox dropdown 描画用)。
///
/// `kirin_root` 配下を [`crate::pre_discovery::discover_active_pre_dirs`] で fresh
/// 全件 Vec として取得し、各 dir の `pre.json` を [`scan_pre_candidates_in`] で
/// 候補化して flatten した `Vec<PreCandidate>` を返す。
///
/// # α-2 撤回根拠 (G-115-49)
/// 旧 `enumerate_same_project_pair_candidates` は `file_name() == project_hash`
/// で同 project_uuid 配下のみに絞っていたが、PRE.vst3 / POST.vst3 が cdylib 隔離
/// (`kirin_measure::project_uuid_cell()` の `static OnceLock` が cdylib 単位で
/// 別実体) のため、両 cdylib の `params.project_uuid` は構造的に乖離する。
/// 結果として filter は常に 0 件しか返さず、ComboBox は永遠に "No candidates" を
/// 表示する状態だった (実機検証 #5-A-3 / 2026-05-04 NG)。
///
/// 本関数は project_hash 引数を取らず、全 fresh dir を flatten する。POST 利用者は
/// Name 識別 (B-027 段階 2 の `pair_pre_name` filter) で目的 PRE を選ぶ運用となる。
///
/// `trigger_keep` の Keep 経路 (B-027 段階 2 fix) も同じ flatten 経路で一貫する。
///
/// # 戻り順
/// `discover_active_pre_dirs` の mtime 降順 dir 順 → 各 dir 内
/// `scan_pre_candidates_in` の instance_id 辞書順。
///
/// # cdylib 越境通信
/// 含まない (filesystem enumerate のみ)。`project_uuid_cell()` / `peek_project_uuid`
/// / `set_project_uuid` のいずれも本関数からは触れない。
pub fn enumerate_active_pre_pair_candidates(kirin_root: &Path) -> Vec<PreCandidate> {
    crate::pre_discovery::discover_active_pre_dirs(kirin_root)
        .into_iter()
        .flat_map(|d| scan_pre_candidates_in(&d))
        .collect()
}

/// POST 側計測値。距離計算用。
#[derive(Debug, Clone, Copy)]
pub struct PostMetrics {
    pub lufs_m: Option<f64>,
    pub true_peak: Option<f64>,
    pub crest: Option<f64>,
}

/// PRE 候補から最近 PRE を選ぶ（距離ベース）。
///
/// **B-059: production 非経路。** 表示=commit 一本化（厳格）により距離 auto-pick は
/// 廃止された（曖昧は沈黙＝選定しない）。本関数は単体テスト保持のため温存する。
/// production 選定は [`crate::pairing_scope::select_target_pre`] を使うこと。
///
/// - 0 件 → None
/// - 1 件 → その 1 つを返す（計測値無くても自動確定。G-50-35）
/// - 複数 + POST/PRE 双方に全 3 メトリック有 → distance 最小を返す
/// - 複数 + いずれかメトリック不在 → 最初の候補を返す（ソート済みで安定）
#[doc(hidden)]
pub fn pick_closest_pre(candidates: &[PreCandidate], post: PostMetrics) -> Option<&PreCandidate> {
    match candidates.len() {
        0 => None,
        1 => candidates.first(),
        _ => {
            let (Some(pl), Some(pt), Some(pc)) = (post.lufs_m, post.true_peak, post.crest) else {
                return candidates.first();
            };
            let mut best: Option<(&PreCandidate, f64)> = None;
            for cand in candidates {
                let (Some(cl), Some(ct), Some(cc)) = (cand.lufs_m, cand.true_peak, cand.crest)
                else {
                    continue;
                };
                let dist = (pl - cl).abs() + (pt - ct).abs() + (pc - cc).abs();
                match &best {
                    Some((_, best_d)) if *best_d <= dist => {} // 同値は先頭優先
                    _ => best = Some((cand, dist)),
                }
            }
            best.map(|(c, _)| c).or_else(|| candidates.first())
        }
    }
}

#[cfg(test)]
mod tests;
