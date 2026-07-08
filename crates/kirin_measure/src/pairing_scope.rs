//! Pairing target resolution.
//!
//! This module owns PRE discovery scope, target selection, and latch semantics. It deliberately
//! does not write record_signal files or enter/exit Record. It only answers which PRE a POST
//! should see, keep, or keep latched.

use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use crate::pre_candidates::{
    enumerate_active_pre_pair_candidates, filter_candidates_by_name, scan_pre_candidates_in,
    PreCandidate,
};
use crate::SignalState;

fn post_pair_names_in_project(kirin_root: &Path, post_project_hash: &str) -> HashSet<String> {
    if post_project_hash.is_empty() {
        return HashSet::new();
    }
    crate::post_candidates::scan_post_candidates_in(&kirin_root.join(post_project_hash))
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
#[derive(Clone, Debug)]
pub struct SelectedPre {
    pub instance_id: String,
    /// Selected PRE `pre.json` path (`{kirin_root}/{project_uuid}/{instance_id}/pre.json`).
    pub pre_json: PathBuf,
    /// `{project_uuid}` directory containing the selected PRE.
    pub project_dir: PathBuf,
}

#[derive(Clone, Debug)]
struct ScopedSelectedPre {
    selected: SelectedPre,
    host_process_id: Option<u32>,
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
) -> Vec<ScopedSelectedPre> {
    if pair_pre_name.is_empty() {
        return Vec::new();
    }
    let candidates: Vec<PreCandidate> = dirs
        .iter()
        .flat_map(|d| scan_pre_candidates_in(d))
        .collect();
    let named = filter_candidates_by_name(candidates, pair_pre_name);

    let mut valid: Vec<ScopedSelectedPre> = Vec::new();
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
        valid.push(ScopedSelectedPre {
            selected: SelectedPre {
                instance_id: c.instance_id,
                pre_json: c.path,
                project_dir,
            },
            host_process_id: c.host_process_id,
        });
    }
    valid
}

fn select_unique_pre(mut valid: Vec<ScopedSelectedPre>) -> Option<SelectedPre> {
    if valid.len() == 1 {
        Some(valid.pop().expect("len checked").selected)
    } else {
        None
    }
}

fn filter_by_host_process(
    valid: Vec<ScopedSelectedPre>,
    host_process_id: u32,
) -> Vec<ScopedSelectedPre> {
    if host_process_id == 0 {
        return Vec::new();
    }
    valid
        .into_iter()
        .filter(|c| c.host_process_id == Some(host_process_id))
        .collect()
}

fn selected_belongs_to_project(selected: &SelectedPre, post_project_hash: &str) -> bool {
    !post_project_hash.is_empty()
        && selected
            .project_dir
            .file_name()
            .and_then(|name| name.to_str())
            == Some(post_project_hash)
}

fn project_external_selection_is_guarded(
    kirin_root: &Path,
    selected: &SelectedPre,
    post_project_hash: &str,
    host_process_id: u32,
) -> bool {
    !selected_belongs_to_project(selected, post_project_hash)
        && crate::post_candidates::host_scope_has_other_active_post_project(
            kirin_root,
            post_project_hash,
            host_process_id,
        )
}

fn select_unique_pre_unless_guarded(
    valid: Vec<ScopedSelectedPre>,
    kirin_root: &Path,
    post_project_hash: &str,
    host_process_id: u32,
) -> Option<SelectedPre> {
    let selected = select_unique_pre(valid)?;
    if project_external_selection_is_guarded(
        kirin_root,
        &selected,
        post_project_hash,
        host_process_id,
    ) {
        None
    } else {
        Some(selected)
    }
}

fn select_target_pre_core_from_dirs(
    dirs: &[PathBuf],
    pair_pre_name: &str,
    require_active: bool,
) -> Option<SelectedPre> {
    select_unique_pre(collect_selected_pre_from_dirs(
        dirs,
        pair_pre_name,
        require_active,
    ))
}

fn select_target_pre_core_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    require_active: bool,
) -> Option<SelectedPre> {
    let all_dirs = crate::pre_discovery::discover_active_pre_dirs(kirin_root);
    let all_valid = collect_selected_pre_from_dirs(&all_dirs, pair_pre_name, require_active);
    if all_valid.is_empty() {
        return None;
    }

    let host_process_id = crate::post_candidates::current_host_process_id();
    if let Some(selected) = select_unique_pre(all_valid.clone()) {
        if !project_external_selection_is_guarded(
            kirin_root,
            &selected,
            post_project_hash,
            host_process_id,
        ) {
            return Some(selected);
        }
    }

    let same_host_valid = filter_by_host_process(all_valid, host_process_id);
    if !same_host_valid.is_empty() {
        if let Some(selected) = select_unique_pre(same_host_valid) {
            if !project_external_selection_is_guarded(
                kirin_root,
                &selected,
                post_project_hash,
                host_process_id,
            ) {
                return Some(selected);
            }
        }

        let scoped_dirs = discover_pre_dirs_for_post_project(kirin_root, post_project_hash);
        let scoped_same_host = filter_by_host_process(
            collect_selected_pre_from_dirs(&scoped_dirs, pair_pre_name, require_active),
            host_process_id,
        );
        return select_unique_pre_unless_guarded(
            scoped_same_host,
            kirin_root,
            post_project_hash,
            host_process_id,
        );
    }

    let scoped_dirs = discover_pre_dirs_for_post_project(kirin_root, post_project_hash);
    select_unique_pre_unless_guarded(
        collect_selected_pre_from_dirs(&scoped_dirs, pair_pre_name, require_active),
        kirin_root,
        post_project_hash,
        host_process_id,
    )
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
    /// Exact PRE signal state if it was present in `pre.json`.
    pub signal_state: Option<SignalState>,
    /// `signal_state == "active"`.
    pub active: bool,
    /// `t` age is within the PRE freshness TTL.
    pub fresh: bool,
}

/// Read exactly one latched PRE `pre.json`.
///
/// This does not scan shelves or re-evaluate ambiguity. After a latch is established, identity
/// stays bound to this file until the POST pair name is explicitly changed or cleared.
pub fn read_pre_at(pre_json: &Path) -> Option<LatchedPreState> {
    let content = fs::read_to_string(pre_json).ok()?;
    let v: serde_json::Value = serde_json::from_str(&content).ok()?;
    let name = v.get("name").and_then(|x| x.as_str()).map(str::to_string);
    let signal_state = v
        .get("signal_state")
        .and_then(|x| x.as_str())
        .map(|s| match s {
            "active" => SignalState::Active,
            "bypassed" => SignalState::Bypassed,
            _ => SignalState::Inactive,
        });
    let active = signal_state == Some(SignalState::Active);
    let fresh = v
        .get("t")
        .and_then(|x| x.as_str())
        .map(|t| t_age_within(t, crate::io_thread_post::NO_PRE_SECS))
        .unwrap_or(false);
    Some(LatchedPreState {
        name,
        signal_state,
        active,
        fresh,
    })
}

fn selected_from_latch(
    pair_pre_name: &str,
    latched: &Mutex<Option<LatchedPre>>,
) -> Option<SelectedPre> {
    if pair_pre_name.is_empty() {
        return None;
    }
    let g = latched.lock().ok()?;
    let l = g.as_ref()?;
    if l.name != pair_pre_name {
        return None;
    }
    Some(SelectedPre {
        instance_id: l.instance_id.clone(),
        pre_json: l.pre_json.clone(),
        project_dir: l.project_dir.clone(),
    })
}

/// Resolve Arm/Keep target, preferring an established matching latch.
///
/// Once the user has paired by name, the latched instance is stronger than transient `pre.json`
/// freshness/name reads. A stale or temporarily missing PRE may fail to acknowledge Record later,
/// but it must not make the pair look released or force a new candidate choice.
pub fn resolve_arm_target(
    kirin_root: &Path,
    pair_pre_name: &str,
    latched: &Mutex<Option<LatchedPre>>,
) -> Option<SelectedPre> {
    if let Some(sel) = selected_from_latch(pair_pre_name, latched) {
        return Some(sel);
    }
    select_target_pre_for_arm(kirin_root, pair_pre_name)
}

/// Resolve POST-project-scoped Arm/Keep target, preferring an established matching latch.
pub fn resolve_arm_target_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    latched: &Mutex<Option<LatchedPre>>,
) -> Option<SelectedPre> {
    if let Some(sel) = selected_from_latch(pair_pre_name, latched) {
        return Some(sel);
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
