//! Pairing target resolution.
//!
//! This module owns PRE discovery scope, target selection, and latch semantics. It deliberately
//! does not write record_signal files or enter/exit Record. It only answers which PRE a POST
//! should see, keep, or keep latched.

use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use crate::record_signal::{
    enumerate_active_pre_pair_candidates, filter_candidates_by_name, scan_pre_candidates_in,
    PreCandidate,
};

fn post_pair_names_in_project(kirin_root: &Path, post_project_hash: &str) -> HashSet<String> {
    if post_project_hash.is_empty() {
        return HashSet::new();
    }
    crate::io_thread_post::scan_post_candidates_in(&kirin_root.join(post_project_hash))
        .into_iter()
        .filter_map(|c| c.pair_pre_name)
        .filter(|name| !name.is_empty())
        .collect()
}

fn pre_names_in_project(project_dir: &Path) -> HashSet<String> {
    scan_pre_candidates_in(project_dir)
        .into_iter()
        .filter_map(|c| c.name)
        .filter(|name| !name.is_empty())
        .collect()
}

/// Return fresh PRE project directories that best match the visible POST project's pair names.
///
/// PRE and POST can run in separate binaries, so their project UUIDs are not a stable session
/// boundary. Existing POST `pair_pre_name` values give a stronger local signal. When the POST side
/// has no names yet, all fresh PRE dirs are returned. When names exist but no PRE dir overlaps, no
/// fallback is performed; falling back would risk pairing to a different session.
pub fn discover_pre_dirs_for_post_project(
    kirin_root: &Path,
    post_project_hash: &str,
) -> Vec<PathBuf> {
    let dirs = crate::pre_discovery::discover_active_pre_dirs(kirin_root);
    let post_names = post_pair_names_in_project(kirin_root, post_project_hash);
    if post_names.is_empty() {
        return dirs;
    }

    let scored: Vec<(PathBuf, usize)> = dirs
        .into_iter()
        .map(|dir| {
            let pre_names = pre_names_in_project(&dir);
            let score = pre_names.intersection(&post_names).count();
            (dir, score)
        })
        .collect();
    let best = scored.iter().map(|(_, score)| *score).max().unwrap_or(0);
    if best == 0 {
        return Vec::new();
    }
    scored
        .into_iter()
        .filter_map(|(dir, score)| (score == best).then_some(dir))
        .collect()
}

/// PRE candidates for a POST project dropdown.
///
/// Candidate menus show what can be selected next, so they intentionally list all fresh PRE
/// candidates. Existing POST overlap is only a tie-break for final target selection.
pub fn enumerate_active_pre_pair_candidates_for_post_project(
    kirin_root: &Path,
    _post_project_hash: &str,
) -> Vec<PreCandidate> {
    enumerate_active_pre_pair_candidates(kirin_root)
}

/// Display/Keep shared PRE selection result.
pub struct SelectedPre {
    pub instance_id: String,
    /// Selected PRE `pre.json` path (`{kirin_root}/{project_uuid}/{instance_id}/pre.json`).
    pub pre_json: PathBuf,
    /// `{project_uuid}` directory containing the selected PRE.
    pub project_dir: PathBuf,
}

/// Strict PRE selection shared by Display and Arm paths.
///
/// `require_active` separates activity gates while keeping identity rules identical:
/// - Display: active + fresh + exactly one matching name.
/// - Arm: non-bypassed + fresh + exactly one matching name.
fn select_target_pre_core(
    kirin_root: &Path,
    pair_pre_name: &str,
    require_active: bool,
) -> Option<SelectedPre> {
    let dirs = crate::pre_discovery::discover_active_pre_dirs(kirin_root);
    select_target_pre_core_from_dirs(&dirs, pair_pre_name, require_active)
}

fn collect_selected_pre_from_dirs(
    dirs: &[PathBuf],
    pair_pre_name: &str,
    require_active: bool,
) -> Vec<SelectedPre> {
    if pair_pre_name.is_empty() {
        return Vec::new();
    }
    let candidates: Vec<PreCandidate> = dirs
        .iter()
        .flat_map(|d| scan_pre_candidates_in(d))
        .collect();
    let named = filter_candidates_by_name(candidates, pair_pre_name);

    let mut valid: Vec<SelectedPre> = Vec::new();
    for c in named {
        let Ok(content) = fs::read_to_string(&c.path) else {
            continue;
        };
        let Ok(v) = serde_json::from_str::<serde_json::Value>(&content) else {
            continue;
        };
        if require_active && v.get("signal_state").and_then(|x| x.as_str()) != Some("active") {
            continue;
        }
        let Some(t_str) = v.get("t").and_then(|x| x.as_str()) else {
            continue;
        };
        if !t_age_within(t_str, crate::io_thread_post::NO_PRE_SECS) {
            continue;
        }
        let Some(project_dir) = c
            .path
            .parent()
            .and_then(|p| p.parent())
            .map(Path::to_path_buf)
        else {
            continue;
        };
        valid.push(SelectedPre {
            instance_id: c.instance_id,
            pre_json: c.path,
            project_dir,
        });
    }
    valid
}

fn select_target_pre_core_from_dirs(
    dirs: &[PathBuf],
    pair_pre_name: &str,
    require_active: bool,
) -> Option<SelectedPre> {
    let mut valid = collect_selected_pre_from_dirs(dirs, pair_pre_name, require_active);

    if valid.len() == 1 {
        valid.pop()
    } else {
        None
    }
}

fn select_target_pre_core_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    require_active: bool,
) -> Option<SelectedPre> {
    let all_dirs = crate::pre_discovery::discover_active_pre_dirs(kirin_root);
    let mut all_valid = collect_selected_pre_from_dirs(&all_dirs, pair_pre_name, require_active);
    match all_valid.len() {
        0 => None,
        1 => all_valid.pop(),
        _ => {
            let scoped_dirs = discover_pre_dirs_for_post_project(kirin_root, post_project_hash);
            select_target_pre_core_from_dirs(&scoped_dirs, pair_pre_name, require_active)
        }
    }
}

/// Strict PRE selection for display Delta (active + fresh + unique).
pub fn select_target_pre(kirin_root: &Path, pair_pre_name: &str) -> Option<SelectedPre> {
    select_target_pre_core(kirin_root, pair_pre_name, true)
}

/// Strict PRE selection for Arm/Keep (non-bypassed + fresh + unique).
pub fn select_target_pre_for_arm(kirin_root: &Path, pair_pre_name: &str) -> Option<SelectedPre> {
    select_target_pre_core(kirin_root, pair_pre_name, false)
}

/// POST-project-scoped display target selection.
pub fn select_target_pre_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
) -> Option<SelectedPre> {
    select_target_pre_core_for_post_project(kirin_root, pair_pre_name, post_project_hash, true)
}

/// POST-project-scoped Arm/Keep target selection.
pub fn select_target_pre_for_arm_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
) -> Option<SelectedPre> {
    select_target_pre_core_for_post_project(kirin_root, pair_pre_name, post_project_hash, false)
}

/// Established PRE<->POST latch shared by display ticks and Keep/Arm.
#[derive(Clone, Debug)]
pub struct LatchedPre {
    /// Pair name at latch time.
    pub name: String,
    /// Latched PRE instance id.
    pub instance_id: String,
    /// Project directory containing the latched PRE.
    pub project_dir: PathBuf,
    /// Exact latched PRE `pre.json`.
    pub pre_json: PathBuf,
}

/// Direct read state for a latched PRE.
pub struct LatchedPreState {
    /// PRE `name` field.
    pub name: Option<String>,
    /// `signal_state == "active"`.
    pub active: bool,
    /// `t` age is within the PRE freshness TTL.
    pub fresh: bool,
}

/// Read exactly one latched PRE `pre.json`.
///
/// This does not scan shelves or re-evaluate ambiguity. After a latch is established, identity
/// stays bound to this file until it is renamed, stale, or gone.
pub fn read_pre_at(pre_json: &Path) -> Option<LatchedPreState> {
    let content = fs::read_to_string(pre_json).ok()?;
    let v: serde_json::Value = serde_json::from_str(&content).ok()?;
    let name = v.get("name").and_then(|x| x.as_str()).map(str::to_string);
    let active = v.get("signal_state").and_then(|x| x.as_str()) == Some("active");
    let fresh = v
        .get("t")
        .and_then(|x| x.as_str())
        .map(|t| t_age_within(t, crate::io_thread_post::NO_PRE_SECS))
        .unwrap_or(false);
    Some(LatchedPreState {
        name,
        active,
        fresh,
    })
}

/// Resolve Arm/Keep target, preferring a still-fresh matching latch.
pub fn resolve_arm_target(
    kirin_root: &Path,
    pair_pre_name: &str,
    latched: &Mutex<Option<LatchedPre>>,
) -> Option<SelectedPre> {
    if let Ok(g) = latched.lock() {
        if let Some(l) = g.as_ref() {
            if l.name == pair_pre_name {
                if let Some(st) = read_pre_at(&l.pre_json) {
                    if st.fresh && st.name.as_deref() == Some(pair_pre_name) {
                        return Some(SelectedPre {
                            instance_id: l.instance_id.clone(),
                            pre_json: l.pre_json.clone(),
                            project_dir: l.project_dir.clone(),
                        });
                    }
                }
            }
        }
    }
    select_target_pre_for_arm(kirin_root, pair_pre_name)
}

/// Resolve POST-project-scoped Arm/Keep target, preferring a still-fresh matching latch.
pub fn resolve_arm_target_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    latched: &Mutex<Option<LatchedPre>>,
) -> Option<SelectedPre> {
    let all_dirs = crate::pre_discovery::discover_active_pre_dirs(kirin_root);
    if let Ok(g) = latched.lock() {
        if let Some(l) = g.as_ref() {
            if l.name == pair_pre_name && all_dirs.iter().any(|d| d == &l.project_dir) {
                if let Some(st) = read_pre_at(&l.pre_json) {
                    if st.fresh && st.name.as_deref() == Some(pair_pre_name) {
                        return Some(SelectedPre {
                            instance_id: l.instance_id.clone(),
                            pre_json: l.pre_json.clone(),
                            project_dir: l.project_dir.clone(),
                        });
                    }
                }
            }
        }
    }
    select_target_pre_core_for_post_project(kirin_root, pair_pre_name, post_project_hash, false)
}

/// Whether RFC3339 `t` is within `max_secs` from now.
fn t_age_within(t_str: &str, max_secs: i64) -> bool {
    match chrono::DateTime::parse_from_rfc3339(t_str) {
        Ok(t) => (chrono::Utc::now() - t.with_timezone(&chrono::Utc)).num_seconds() < max_secs,
        Err(_) => false,
    }
}

#[cfg(test)]
mod tests;
