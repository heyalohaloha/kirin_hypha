//! POST candidate discovery.
//!
//! This module reads `$TMPDIR/kirin/{project_uuid}/{post_instance}/post.json` snapshots
//! written by the POST IO thread. It deliberately does not run the IO loop or write files.

use serde::Deserialize;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use crate::pre_discovery::DISCOVERY_STALE_SECS;

const RECORD_SIGNAL_RESERVED_DIR: &str = "record_signal";

/// `post.json` の deserialize 用 wire format struct (B-027 段階 3-B α-7-1 / Step 4)。
///
/// Active 時 (full) と Bypassed・Inactive 時 (minimal) で書込 field 数が異なる。
/// 共通 field (instance_id / signal_state / t / pair_pre_name) は最低限の必須型。
/// schema metadata (`v` / `role`) は `#[serde(default)]` で旧 schema 互換確保。
/// Active のみの値 field は `Option<T>` + `#[serde(default)]` で minimal 形式 /
/// 旧 schema での不在を許容する。
#[allow(dead_code)]
#[derive(Debug, Clone, Deserialize)]
pub(crate) struct PostTmpJson {
    #[serde(default)]
    pub v: u32,
    #[serde(default)]
    pub role: String,
    pub instance_id: String,
    pub signal_state: String,
    #[serde(default)]
    pub pre_signal_state: Option<String>,
    pub t: String,
    #[serde(default)]
    pub pair_pre_name: String,
    #[serde(default)]
    pub pair_claimed_at: f64,
    #[serde(default)]
    pub lufs_m: Option<f64>,
    #[serde(default)]
    pub true_peak: Option<f64>,
    #[serde(default)]
    pub crest: Option<f64>,
    #[serde(default)]
    pub psr: Option<f64>,
    #[serde(default)]
    pub n_prime_total: Option<f64>,
    #[serde(default)]
    pub sharpness: Option<f64>,
    #[serde(default)]
    pub psb_summary: Option<PostPsbSummary>,
}

#[allow(dead_code)]
#[derive(Debug, Clone, Deserialize)]
pub(crate) struct PostPsbSummary {
    pub low: f64,
    pub mid: f64,
    pub high: f64,
}

/// `/tmp/kirin/{project_uuid}/{instance_id}/post.json` 1 件分のパース結果。
#[derive(Debug, Clone, PartialEq)]
pub struct PostCandidate {
    pub instance_id: String,
    pub project_uuid: String,
    pub pair_pre_name: Option<String>,
    /// W-281 / G-115-249: post.json から read した pair claim 時刻 (Unix epoch sec)。
    /// `self_check_pair_claim` の後着優先比較で使う。旧 schema は 0.0 fallback。
    pub pair_claimed_at: f64,
    pub path: PathBuf,
}

/// 指定された `{project_uuid}/` dir 配下の `post.json` を走査する。
///
/// `record_signal/` 予約 dir は除外。`post.json` deserialize 失敗・ファイル不在は
/// skip。`signal_state == "bypassed"` の POST は除外する。
pub fn scan_post_candidates_in(project_dir: &Path) -> Vec<PostCandidate> {
    let project_uuid = project_dir
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("")
        .to_string();
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
        if path.file_name().and_then(|n| n.to_str()) == Some(RECORD_SIGNAL_RESERVED_DIR) {
            continue;
        }
        let post_file = path.join("post.json");
        let Ok(bytes) = fs::read(&post_file) else {
            continue;
        };
        let parsed: PostTmpJson = match serde_json::from_slice(&bytes) {
            Ok(p) => p,
            Err(e) => {
                log::warn!(
                    "[pairing] skip unparseable post.json {}: {}",
                    post_file.display(),
                    e
                );
                continue;
            }
        };
        if parsed.signal_state == "bypassed" {
            continue;
        }
        let pair_pre_name = if parsed.pair_pre_name.is_empty() {
            None
        } else {
            Some(parsed.pair_pre_name)
        };
        out.push(PostCandidate {
            instance_id: parsed.instance_id,
            project_uuid: project_uuid.clone(),
            pair_pre_name,
            pair_claimed_at: parsed.pair_claimed_at,
            path: post_file,
        });
    }
    out.sort_by(|a, b| a.instance_id.cmp(&b.instance_id));
    out
}

/// `kirin_root` (= `$TMPDIR/kirin/`) 配下を scan して mtime fresh の全 active POST dir を返す。
pub(crate) fn discover_active_post_dirs(kirin_root: &Path) -> Vec<PathBuf> {
    let now = SystemTime::now();
    let stale_threshold = Duration::from_secs(DISCOVERY_STALE_SECS);

    let project_entries = match fs::read_dir(kirin_root) {
        Ok(e) => e,
        Err(_) => return Vec::new(),
    };

    let mut candidates: Vec<(PathBuf, SystemTime)> = Vec::new();

    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }

        let instance_entries = match fs::read_dir(&project_dir) {
            Ok(e) => e,
            Err(_) => continue,
        };

        let mut latest_in_project: Option<SystemTime> = None;
        for instance_entry in instance_entries.flatten() {
            let instance_dir = instance_entry.path();
            if !instance_dir.is_dir() {
                continue;
            }

            let post_json = instance_dir.join("post.json");
            let meta = match fs::metadata(&post_json) {
                Ok(m) => m,
                Err(_) => continue,
            };
            if !meta.is_file() {
                continue;
            }

            let mtime = match meta.modified() {
                Ok(t) => t,
                Err(_) => continue,
            };

            if let Ok(age) = now.duration_since(mtime) {
                if age > stale_threshold {
                    continue;
                }
            }

            latest_in_project = Some(match latest_in_project {
                Some(prev) if prev > mtime => prev,
                _ => mtime,
            });
        }

        if let Some(t) = latest_in_project {
            candidates.push((project_dir, t));
        }
    }

    candidates.sort_by(|a, b| a.0.file_name().cmp(&b.0.file_name()));
    candidates.into_iter().map(|(p, _)| p).collect()
}

/// 自 instance が同 project_dir 配下の他 POST と pair_pre_name を共有し、
/// 自身より新しい claim が存在するなら release 必要 (`true`) を返す。
pub fn self_check_pair_claim(
    project_dir: &Path,
    self_instance_id: &str,
    self_pair_pre_name: &str,
    self_pair_claimed_at: f64,
) -> bool {
    if self_pair_pre_name.is_empty() {
        return false;
    }
    let candidates = scan_post_candidates_in(project_dir);
    for cand in &candidates {
        if cand.instance_id == self_instance_id {
            continue;
        }
        if cand.pair_pre_name.as_deref() != Some(self_pair_pre_name) {
            continue;
        }
        if cand.pair_claimed_at > self_pair_claimed_at {
            return true;
        }
        if cand.pair_claimed_at == self_pair_claimed_at
            && cand.instance_id.as_str() < self_instance_id
        {
            return true;
        }
    }
    false
}

pub fn enumerate_active_post_pair_candidates(kirin_root: &Path) -> Vec<PostCandidate> {
    discover_active_post_dirs(kirin_root)
        .into_iter()
        .flat_map(|d| scan_post_candidates_in(&d))
        .collect()
}
