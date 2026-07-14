//! POST candidate discovery.
//!
//! This module reads `$TMPDIR/kirin/{project_uuid}/{post_instance}/post.json` snapshots
//! written by the POST IO thread. It deliberately does not run the IO loop or write files.

use serde::Deserialize;
use std::collections::BTreeSet;
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
    pub daw_session_id: String,
    #[serde(default)]
    pub host_process_id: u32,
    #[serde(default)]
    pub watch_owner_id: String,
    #[serde(default)]
    pub pair_pre_name: String,
    #[serde(default)]
    pub paired_pre_instance_id: String,
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
    pub daw_session_id: Option<String>,
    pub host_process_id: Option<u32>,
    pub pair_pre_name: Option<String>,
    /// Exact PRE runtime selected by this POST. Empty on legacy/name-only snapshots.
    pub paired_pre_instance_id: Option<String>,
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
    scan_post_candidates_in_mode(project_dir, false)
}

fn scan_post_pair_claims_in(project_dir: &Path) -> Vec<PostCandidate> {
    scan_post_candidates_in_mode(project_dir, true)
}

fn scan_post_candidates_in_mode(project_dir: &Path, include_bypassed: bool) -> Vec<PostCandidate> {
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
        if !crate::watch_snapshot_lease::snapshot_owner_is_live(&path, &parsed.watch_owner_id) {
            continue;
        }
        if !include_bypassed && parsed.signal_state == "bypassed" {
            continue;
        }
        let pair_pre_name = if parsed.pair_pre_name.is_empty() {
            None
        } else {
            Some(parsed.pair_pre_name)
        };
        let paired_pre_instance_id = if parsed.paired_pre_instance_id.is_empty() {
            None
        } else {
            Some(parsed.paired_pre_instance_id)
        };
        let daw_session_id = if parsed.daw_session_id.is_empty() {
            None
        } else {
            Some(parsed.daw_session_id)
        };
        let host_process_id = if parsed.host_process_id == 0 {
            None
        } else {
            Some(parsed.host_process_id)
        };
        out.push(PostCandidate {
            instance_id: parsed.instance_id,
            project_uuid: project_uuid.clone(),
            daw_session_id,
            host_process_id,
            pair_pre_name,
            paired_pre_instance_id,
            pair_claimed_at: parsed.pair_claimed_at,
            path: post_file,
        });
    }
    out.sort_by(|a, b| a.instance_id.cmp(&b.instance_id));
    out
}

fn scan_live_post_pair_candidates_in(project_dir: &Path) -> Vec<PostCandidate> {
    scan_post_pair_claims_in(project_dir)
        .into_iter()
        .filter(|candidate| {
            crate::watch_snapshot_lease::snapshot_file_is_live_pair_choice(
                &candidate.path,
                Duration::from_secs(DISCOVERY_STALE_SECS),
            )
        })
        .collect()
}

/// POST runtimes visible for pair-status discovery.
///
/// A current owner lease is stronger evidence of plugin presence than snapshot mtime. Legacy
/// snapshots retain the bounded freshness rule because they cannot prove a live producer.
pub fn enumerate_live_post_pair_candidates(kirin_root: &Path) -> Vec<PostCandidate> {
    let project_entries = match fs::read_dir(kirin_root) {
        Ok(entries) => entries,
        Err(_) => return Vec::new(),
    };
    let mut candidates = Vec::new();
    for entry in project_entries.flatten() {
        let project_dir = entry.path();
        if !project_dir.is_dir() {
            continue;
        }
        candidates.extend(scan_live_post_pair_candidates_in(&project_dir));
    }
    candidates.sort_by(|a, b| a.instance_id.cmp(&b.instance_id));
    candidates
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

            if !crate::watch_snapshot_lease::snapshot_file_has_live_owner(&post_json) {
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
#[cfg(test)]
pub(crate) fn self_check_pair_claim(
    project_dir: &Path,
    self_instance_id: &str,
    self_pair_pre_name: &str,
    self_pair_claimed_at: f64,
) -> bool {
    self_check_pair_claim_exact(
        project_dir,
        self_instance_id,
        self_pair_pre_name,
        "",
        self_pair_claimed_at,
    )
}

/// Exact-instance variant of [`self_check_pair_claim`].
///
/// Current snapshots compare `paired_pre_instance_id`, so two PREs with the same display name do
/// not steal each other's POST. A legacy POST without the exact field falls back to the name to
/// preserve the previous mutual-exclusion contract during a rolling upgrade.
#[cfg(test)]
pub(crate) fn self_check_pair_claim_exact(
    project_dir: &Path,
    self_instance_id: &str,
    self_pair_pre_name: &str,
    self_pre_instance_id: &str,
    self_pair_claimed_at: f64,
) -> bool {
    if self_pair_pre_name.is_empty() && self_pre_instance_id.is_empty() {
        return false;
    }
    let candidates = scan_live_post_pair_candidates_in(project_dir);
    for cand in &candidates {
        if cand.instance_id == self_instance_id {
            continue;
        }
        let claims_same_pre = if self_pre_instance_id.is_empty() {
            !self_pair_pre_name.is_empty()
                && cand.pair_pre_name.as_deref() == Some(self_pair_pre_name)
        } else if let Some(candidate_pre_instance_id) = cand.paired_pre_instance_id.as_deref() {
            candidate_pre_instance_id == self_pre_instance_id
        } else {
            !self_pair_pre_name.is_empty()
                && cand.pair_pre_name.as_deref() == Some(self_pair_pre_name)
        };
        if !claims_same_pre {
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

/// Whether another active POST project is visible inside the same host process.
///
/// `host_process_id` is intentionally broader than a project UUID and is only safe as a guard, not
/// as proof of document identity. When a DAW can keep multiple documents open inside one process,
/// callers must fail closed before accepting a project-external PRE solely because it is globally
/// unique.
pub fn host_scope_has_other_active_post_project(
    kirin_root: &Path,
    current_project_uuid: &str,
    host_process_id: u32,
) -> bool {
    host_process_id != 0
        && enumerate_active_post_pair_candidates(kirin_root)
            .into_iter()
            .any(|c| {
                c.host_process_id == Some(host_process_id) && c.project_uuid != current_project_uuid
            })
}

/// Whether the same host has another active POST project that is not in the given DAW scope.
///
/// Exact DAW-session matches can bridge split AU/VST3 shelves only when every visible POST shelf in
/// this host agrees on that same DAW identity. A different or missing identity in another shelf
/// means the host is ambiguous, so project-external PREs must fail closed.
pub fn host_scope_has_other_active_post_project_outside_daw(
    kirin_root: &Path,
    current_project_uuid: &str,
    daw_session_id: &str,
    host_process_id: u32,
) -> bool {
    if host_process_id == 0 || daw_session_id.is_empty() {
        return false;
    }
    enumerate_active_post_pair_candidates(kirin_root)
        .into_iter()
        .any(|c| {
            c.host_process_id == Some(host_process_id)
                && c.project_uuid != current_project_uuid
                && c.daw_session_id.as_deref() != Some(daw_session_id)
        })
}

fn daw_session_matches(candidate: &PostCandidate, daw_session_id: &str) -> bool {
    !daw_session_id.is_empty()
        && candidate
            .daw_session_id
            .as_deref()
            .is_some_and(|candidate_daw| candidate_daw == daw_session_id)
}

pub fn current_host_process_id() -> u32 {
    std::process::id()
}

pub fn broadcast_scope_ids_match(
    local_daw_session_id: &str,
    local_host_process_id: u32,
    remote_daw_session_id: &str,
    remote_host_process_id: u32,
) -> bool {
    if !local_daw_session_id.is_empty() && !remote_daw_session_id.is_empty() {
        return local_daw_session_id == remote_daw_session_id;
    }
    if !local_daw_session_id.is_empty() || !remote_daw_session_id.is_empty() {
        return false;
    }

    local_host_process_id != 0
        && remote_host_process_id != 0
        && local_host_process_id == remote_host_process_id
}

fn host_process_matches(candidate: &PostCandidate, host_process_id: u32) -> bool {
    host_process_id != 0 && candidate.host_process_id == Some(host_process_id)
}

fn broadcast_scope_matches(
    candidate: &PostCandidate,
    daw_session_id: &str,
    host_process_id: u32,
) -> bool {
    if daw_session_matches(candidate, daw_session_id) {
        return true;
    }

    let candidate_has_daw = candidate
        .daw_session_id
        .as_deref()
        .is_some_and(|daw| !daw.is_empty());
    if !daw_session_id.is_empty() || candidate_has_daw {
        return false;
    }

    host_process_matches(candidate, host_process_id)
}

/// Active POST candidates in the same DAW session, spanning AU/VST3 project UUID shelves.
///
/// Missing `daw_session_id` is not treated as a DAW-session match. Callers that need tolerance for
/// hosts that restore instance-scoped DAW IDs should use the broadcast-scope helper, which keeps a
/// same-host single POST shelf together and fails closed when another shelf makes the host
/// ambiguous.
pub fn enumerate_active_post_pair_candidates_for_daw_session(
    kirin_root: &Path,
    daw_session_id: &str,
) -> Vec<PostCandidate> {
    enumerate_active_post_pair_candidates(kirin_root)
        .into_iter()
        .filter(|c| daw_session_matches(c, daw_session_id))
        .collect()
}

/// Active POST candidates in the same broadcast scope.
///
/// `daw_session_id` is preferred when the host gives a coherent document identity. Some DAW/plugin
/// wrapper combinations restore this value per instance instead. In that shape, all POSTs from the
/// currently visible document still share one POST project shelf, so same-host candidates from a
/// single POST shelf are kept together. If the same host exposes multiple POST shelves, fallback
/// closes and only exact DAW-session matches are returned.
pub fn enumerate_active_post_pair_candidates_for_broadcast_scope(
    kirin_root: &Path,
    daw_session_id: &str,
    host_process_id: u32,
) -> Vec<PostCandidate> {
    let candidates = enumerate_active_post_pair_candidates(kirin_root);
    if !daw_session_id.is_empty() {
        let same_host_single_project =
            same_host_single_project_candidates(&candidates, host_process_id);
        if !same_host_single_project.is_empty() {
            return same_host_single_project;
        }
    }

    candidates
        .into_iter()
        .filter(|c| broadcast_scope_matches(c, daw_session_id, host_process_id))
        .collect()
}

/// Live POST pair claims in the same DAW/broadcast scope.
///
/// This is deliberately independent of signal bypass. A bypassed POST cannot measure, but its
/// explicit PRE ownership and PAIR indicator must not silently move to another POST.
pub fn enumerate_live_post_pair_candidates_for_broadcast_scope(
    kirin_root: &Path,
    daw_session_id: &str,
    host_process_id: u32,
) -> Vec<PostCandidate> {
    let candidates = enumerate_live_post_pair_candidates(kirin_root);
    if !daw_session_id.is_empty() {
        let same_host_single_project =
            same_host_single_project_candidates(&candidates, host_process_id);
        if !same_host_single_project.is_empty() {
            return same_host_single_project;
        }
    }

    candidates
        .into_iter()
        .filter(|candidate| broadcast_scope_matches(candidate, daw_session_id, host_process_id))
        .collect()
}

fn same_host_single_project_candidates(
    candidates: &[PostCandidate],
    host_process_id: u32,
) -> Vec<PostCandidate> {
    if host_process_id == 0 {
        return Vec::new();
    }
    let same_host: Vec<PostCandidate> = candidates
        .iter()
        .filter(|c| c.host_process_id == Some(host_process_id))
        .cloned()
        .collect();
    let projects = same_host
        .iter()
        .map(|c| c.project_uuid.as_str())
        .collect::<BTreeSet<_>>();
    if projects.len() == 1 {
        same_host
    } else {
        Vec::new()
    }
}

/// Project UUID shelves containing active POSTs in the same DAW session.
pub fn active_post_project_uuids_for_daw_session(
    kirin_root: &Path,
    daw_session_id: &str,
) -> Vec<String> {
    enumerate_active_post_pair_candidates_for_daw_session(kirin_root, daw_session_id)
        .into_iter()
        .map(|c| c.project_uuid)
        .collect::<BTreeSet<_>>()
        .into_iter()
        .collect()
}

/// Project UUID shelves containing active POSTs in the same broadcast scope.
pub fn active_post_project_uuids_for_broadcast_scope(
    kirin_root: &Path,
    daw_session_id: &str,
    host_process_id: u32,
) -> Vec<String> {
    enumerate_active_post_pair_candidates_for_broadcast_scope(
        kirin_root,
        daw_session_id,
        host_process_id,
    )
    .into_iter()
    .map(|c| c.project_uuid)
    .collect::<BTreeSet<_>>()
    .into_iter()
    .collect()
}
