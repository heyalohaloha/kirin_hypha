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
    enumerate_active_pre_pair_candidates, enumerate_live_pre_pair_choices,
    filter_candidates_by_name, scan_pre_candidates_in, PreCandidate,
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

/// Legacy unscoped PRE candidates for older callers.
///
/// This intentionally lists all fresh PRE candidates to preserve the historical API. Shipping
/// dropdowns should use [`enumerate_active_pre_pair_candidates_for_post_project_in_session`] so the
/// visible choices match what Keep can actually arm.
pub fn enumerate_active_pre_pair_candidates_for_post_project(
    kirin_root: &Path,
    _post_project_hash: &str,
) -> Vec<PreCandidate> {
    enumerate_active_pre_pair_candidates(kirin_root)
}

fn candidate_project_dir(candidate: &PreCandidate) -> Option<PathBuf> {
    candidate
        .path
        .parent()
        .and_then(|p| p.parent())
        .map(Path::to_path_buf)
}

fn candidate_belongs_to_project(candidate: &PreCandidate, post_project_hash: &str) -> bool {
    !post_project_hash.is_empty()
        && candidate_project_dir(candidate)
            .as_deref()
            .and_then(Path::file_name)
            .and_then(|name| name.to_str())
            == Some(post_project_hash)
}

fn candidate_matches_daw_session(candidate: &PreCandidate, post_daw_session_id: &str) -> bool {
    !post_daw_session_id.is_empty()
        && candidate.daw_session_id.as_deref() == Some(post_daw_session_id)
}

fn host_process_is_compatible(
    candidate_host_process_id: Option<u32>,
    host_process_id: u32,
) -> bool {
    host_process_id == 0
        || candidate_host_process_id.is_none()
        || candidate_host_process_id == Some(host_process_id)
}

fn exact_daw_scope_is_allowed(
    kirin_root: &Path,
    post_project_hash: &str,
    post_daw_session_id: &str,
    host_process_id: u32,
) -> bool {
    !post_daw_session_id.is_empty()
        && !crate::post_candidates::host_scope_has_other_active_post_project_outside_daw(
            kirin_root,
            post_project_hash,
            post_daw_session_id,
            host_process_id,
        )
}

fn same_host_single_post_project_is_allowed(
    kirin_root: &Path,
    post_project_hash: &str,
    host_process_id: u32,
    candidate_host_process_id: Option<u32>,
) -> bool {
    host_process_id != 0
        && candidate_host_process_id == Some(host_process_id)
        && !crate::post_candidates::host_scope_has_other_active_post_project(
            kirin_root,
            post_project_hash,
            host_process_id,
        )
}

fn exact_daw_candidate_is_allowed(
    kirin_root: &Path,
    candidate: &PreCandidate,
    post_project_hash: &str,
    post_daw_session_id: &str,
    host_process_id: u32,
) -> bool {
    candidate_matches_daw_session(candidate, post_daw_session_id)
        && host_process_is_compatible(candidate.host_process_id, host_process_id)
        && (candidate_belongs_to_project(candidate, post_project_hash)
            || exact_daw_scope_is_allowed(
                kirin_root,
                post_project_hash,
                post_daw_session_id,
                host_process_id,
            ))
}

fn candidate_is_in_post_scope(
    kirin_root: &Path,
    candidate: &PreCandidate,
    post_project_hash: &str,
    post_daw_session_id: &str,
    host_process_id: u32,
) -> bool {
    if exact_daw_candidate_is_allowed(
        kirin_root,
        candidate,
        post_project_hash,
        post_daw_session_id,
        host_process_id,
    ) {
        return true;
    }

    if same_host_single_post_project_is_allowed(
        kirin_root,
        post_project_hash,
        host_process_id,
        candidate.host_process_id,
    ) {
        return true;
    }

    post_daw_session_id.is_empty()
        && candidate.daw_session_id.is_none()
        && candidate_belongs_to_project(candidate, post_project_hash)
}

/// PRE candidates that the current POST can actually arm.
///
/// The dropdown must not show a PRE that `Keep` will reject. DAW identity is preferred when all
/// visible POST shelves in this host agree on it. If hosts restore DAW IDs per instance, a same-host
/// single POST shelf is kept together. When another POST shelf makes the host ambiguous, fallback
/// closes.
pub fn enumerate_active_pre_pair_candidates_for_post_project_in_session(
    kirin_root: &Path,
    post_project_hash: &str,
    post_daw_session_id: &str,
) -> Vec<PreCandidate> {
    let host_process_id = crate::post_candidates::current_host_process_id();
    enumerate_active_pre_pair_candidates(kirin_root)
        .into_iter()
        .filter(|candidate| {
            candidate_is_in_post_scope(
                kirin_root,
                candidate,
                post_project_hash,
                post_daw_session_id,
                host_process_id,
            )
        })
        .collect()
}

fn explicit_candidate_is_in_post_scope(
    kirin_root: &Path,
    candidate: &PreCandidate,
    post_project_hash: &str,
    post_daw_session_id: &str,
    host_process_id: u32,
) -> bool {
    // An explicit click is stronger than unreliable per-instance project/session IDs. The host
    // process still forms a hard runtime boundary, so another DAW process is never offered merely
    // because it reused a human name.
    if host_process_id != 0 && candidate.host_process_id == Some(host_process_id) {
        return true;
    }
    candidate_is_in_post_scope(
        kirin_root,
        candidate,
        post_project_hash,
        post_daw_session_id,
        host_process_id,
    )
}

/// Live PRE runtimes offered for an explicit click in the current POST.
///
/// Unlike the automatic/Keep resolver, this list does not discard a leased runtime solely because
/// its measurement timestamp is stale. Exact selection records the instance ID, while the Pair
/// status remains Waiting until fresh snapshots resume.
pub fn enumerate_live_pre_pair_choices_for_post_project_in_session(
    kirin_root: &Path,
    post_project_hash: &str,
    post_daw_session_id: &str,
) -> Vec<PreCandidate> {
    let host_process_id = crate::post_candidates::current_host_process_id();
    enumerate_live_pre_pair_choices(kirin_root)
        .into_iter()
        .filter(|candidate| {
            explicit_candidate_is_in_post_scope(
                kirin_root,
                candidate,
                post_project_hash,
                post_daw_session_id,
                host_process_id,
            )
        })
        .collect()
}

/// Display/Keep shared PRE selection result.
#[derive(Clone, Debug)]
pub struct SelectedPre {
    pub instance_id: String,
    /// Selected PRE `pre.json` path (`{kirin_root}/{project_uuid}/{instance_id}/pre.json`).
    pub pre_json: PathBuf,
    /// `{project_uuid}` directory containing the selected PRE.
    pub project_dir: PathBuf,
    /// DAW document/session identity from PRE `pre.json`, when available.
    pub daw_session_id: Option<String>,
    /// DAW host process ID from PRE `pre.json`, when available.
    pub host_process_id: Option<u32>,
}

/// Resolve one exact PRE chosen by the user. No name ambiguity or metric freshness gate is applied.
pub fn select_live_pre_pair_choice_by_instance_for_post_project_in_session(
    kirin_root: &Path,
    instance_id: &str,
    post_project_hash: &str,
    post_daw_session_id: &str,
) -> Option<(SelectedPre, String)> {
    let candidate = enumerate_live_pre_pair_choices_for_post_project_in_session(
        kirin_root,
        post_project_hash,
        post_daw_session_id,
    )
    .into_iter()
    .find(|candidate| candidate.instance_id == instance_id)?;
    let project_dir = candidate_project_dir(&candidate)?;
    let name = candidate.name.clone().unwrap_or_default();
    Some((
        SelectedPre {
            instance_id: candidate.instance_id,
            pre_json: candidate.path,
            project_dir,
            daw_session_id: candidate.daw_session_id,
            host_process_id: candidate.host_process_id,
        },
        name,
    ))
}

fn select_unique_live_pre_pair_choice_by_name_for_explicit_reconnect(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    post_daw_session_id: &str,
) -> Option<SelectedPre> {
    let mut matches = enumerate_live_pre_pair_choices_for_post_project_in_session(
        kirin_root,
        post_project_hash,
        post_daw_session_id,
    )
    .into_iter()
    .filter(|candidate| candidate.name.as_deref() == Some(pair_pre_name));
    let candidate = matches.next()?;
    if matches.next().is_some() {
        return None;
    }
    let project_dir = candidate_project_dir(&candidate)?;
    Some(SelectedPre {
        instance_id: candidate.instance_id,
        pre_json: candidate.path,
        project_dir,
        daw_session_id: candidate.daw_session_id,
        host_process_id: candidate.host_process_id,
    })
}

/// Resolve a POST's published pair claim for All Keep/readiness checks.
///
/// Current POSTs publish an exact PRE instance. That identity is attempted first and remains valid
/// while its runtime lease exists, even when Watch measurements are temporarily stale. A missing
/// exact runtime falls back to the human name so delete/recreate can reconnect immediately. Legacy
/// name-only POSTs keep the strict fresh+unique Arm resolver.
pub fn resolve_published_pair_claim_for_arm(
    kirin_root: &Path,
    pair_pre_name: Option<&str>,
    paired_pre_instance_id: Option<&str>,
    post_project_hash: &str,
    post_daw_session_id: &str,
) -> Option<SelectedPre> {
    if let Some(instance_id) = paired_pre_instance_id.filter(|id| !id.is_empty()) {
        if let Some((selected, _)) =
            select_live_pre_pair_choice_by_instance_for_post_project_in_session(
                kirin_root,
                instance_id,
                post_project_hash,
                post_daw_session_id,
            )
        {
            return Some(selected);
        }

        // Do not reinterpret an exact ID that is still owned outside this POST's explicit scope.
        // Only a genuinely absent runtime may use the delete/recreate name contract.
        if enumerate_live_pre_pair_choices(kirin_root)
            .iter()
            .any(|candidate| candidate.instance_id == instance_id)
        {
            return None;
        }

        // The missing exact ID proves that this is a previously explicit claim, not a new
        // automatic name match. Preserve that intent across delete/recreate only when the same
        // host process exposes exactly one live PRE with the published name.
        if let Some(name) = pair_pre_name.filter(|name| !name.is_empty()) {
            return select_unique_live_pre_pair_choice_by_name_for_explicit_reconnect(
                kirin_root,
                name,
                post_project_hash,
                post_daw_session_id,
            );
        }
        return None;
    }

    let name = pair_pre_name.filter(|name| !name.is_empty())?;
    select_target_pre_for_arm_for_post_project_in_session(
        kirin_root,
        name,
        post_project_hash,
        post_daw_session_id,
    )
}

pub fn latch_selected_pre(name: String, selected: SelectedPre) -> LatchedPre {
    LatchedPre {
        name,
        instance_id: selected.instance_id,
        project_dir: selected.project_dir,
        pre_json: selected.pre_json,
        daw_session_id: selected.daw_session_id,
        host_process_id: selected.host_process_id,
        readiness: LatchedPreReadiness::Confirmed,
    }
}

#[derive(Clone, Debug)]
struct ScopedSelectedPre {
    selected: SelectedPre,
    host_process_id: Option<u32>,
    daw_session_id: Option<String>,
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
                daw_session_id: c.daw_session_id.clone(),
                host_process_id: c.host_process_id,
            },
            host_process_id: c.host_process_id,
            daw_session_id: c.daw_session_id,
        });
    }
    valid
}

fn select_unique_scoped_pre(mut valid: Vec<ScopedSelectedPre>) -> Option<ScopedSelectedPre> {
    if valid.len() == 1 {
        Some(valid.pop().expect("len checked"))
    } else {
        None
    }
}

fn select_unique_pre(valid: Vec<ScopedSelectedPre>) -> Option<SelectedPre> {
    select_unique_scoped_pre(valid).map(|s| s.selected)
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

fn filter_by_daw_session(
    valid: Vec<ScopedSelectedPre>,
    post_daw_session_id: &str,
) -> Vec<ScopedSelectedPre> {
    if post_daw_session_id.is_empty() {
        return Vec::new();
    }
    valid
        .into_iter()
        .filter(|c| c.daw_session_id.as_deref() == Some(post_daw_session_id))
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

fn selected_matches_daw_session(selected: &SelectedPre, post_daw_session_id: &str) -> bool {
    !post_daw_session_id.is_empty()
        && selected.daw_session_id.as_deref() == Some(post_daw_session_id)
}

fn exact_daw_selection_is_allowed(
    kirin_root: &Path,
    selected: &SelectedPre,
    post_project_hash: &str,
    post_daw_session_id: &str,
    host_process_id: u32,
) -> bool {
    selected_matches_daw_session(selected, post_daw_session_id)
        && host_process_is_compatible(selected.host_process_id, host_process_id)
        && (selected_belongs_to_project(selected, post_project_hash)
            || exact_daw_scope_is_allowed(
                kirin_root,
                post_project_hash,
                post_daw_session_id,
                host_process_id,
            ))
}

fn same_host_selection_is_allowed(
    kirin_root: &Path,
    scoped: &ScopedSelectedPre,
    post_project_hash: &str,
    host_process_id: u32,
) -> bool {
    same_host_single_post_project_is_allowed(
        kirin_root,
        post_project_hash,
        host_process_id,
        scoped.host_process_id,
    )
}

fn project_external_selection_is_guarded(
    kirin_root: &Path,
    scoped: &ScopedSelectedPre,
    post_project_hash: &str,
    post_daw_session_id: &str,
    host_process_id: u32,
) -> bool {
    if exact_daw_selection_is_allowed(
        kirin_root,
        &scoped.selected,
        post_project_hash,
        post_daw_session_id,
        host_process_id,
    ) {
        return false;
    }
    if same_host_selection_is_allowed(kirin_root, scoped, post_project_hash, host_process_id) {
        return false;
    }

    if !post_daw_session_id.is_empty() || scoped.daw_session_id.is_some() {
        return true;
    }

    !selected_belongs_to_project(&scoped.selected, post_project_hash)
}

fn selected_from_latch_is_in_post_scope(
    kirin_root: &Path,
    selected: &SelectedPre,
    post_project_hash: &str,
    post_daw_session_id: &str,
) -> bool {
    let host_process_id = crate::post_candidates::current_host_process_id();
    // An exact runtime latch was either established by a strict automatic resolver or by an
    // explicit user click. Once it exists, per-instance project/session IDs must not invalidate
    // that identity. The host process remains the hard boundary.
    if host_process_id != 0 && selected.host_process_id == Some(host_process_id) {
        return true;
    }
    if exact_daw_selection_is_allowed(
        kirin_root,
        selected,
        post_project_hash,
        post_daw_session_id,
        host_process_id,
    ) {
        return true;
    }
    if same_host_single_post_project_is_allowed(
        kirin_root,
        post_project_hash,
        host_process_id,
        selected.host_process_id,
    ) {
        return true;
    }

    post_daw_session_id.is_empty()
        && selected.daw_session_id.is_none()
        && selected_belongs_to_project(selected, post_project_hash)
}

fn select_unique_pre_unless_guarded(
    valid: Vec<ScopedSelectedPre>,
    kirin_root: &Path,
    post_project_hash: &str,
    post_daw_session_id: &str,
    host_process_id: u32,
) -> Option<SelectedPre> {
    let scoped = select_unique_scoped_pre(valid)?;
    if project_external_selection_is_guarded(
        kirin_root,
        &scoped,
        post_project_hash,
        post_daw_session_id,
        host_process_id,
    ) {
        None
    } else {
        Some(scoped.selected)
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
    post_daw_session_id: &str,
    require_active: bool,
) -> Option<SelectedPre> {
    let all_dirs = crate::pre_discovery::discover_active_pre_dirs(kirin_root);
    let all_valid = collect_selected_pre_from_dirs(&all_dirs, pair_pre_name, require_active);
    if all_valid.is_empty() {
        return None;
    }

    let host_process_id = crate::post_candidates::current_host_process_id();
    let same_daw_valid: Vec<_> = filter_by_daw_session(all_valid.clone(), post_daw_session_id)
        .into_iter()
        .filter(|scoped| {
            exact_daw_selection_is_allowed(
                kirin_root,
                &scoped.selected,
                post_project_hash,
                post_daw_session_id,
                host_process_id,
            )
        })
        .collect();
    if !same_daw_valid.is_empty() {
        return select_unique_pre(same_daw_valid);
    }

    if let Some(scoped) = select_unique_scoped_pre(all_valid.clone()) {
        if !project_external_selection_is_guarded(
            kirin_root,
            &scoped,
            post_project_hash,
            post_daw_session_id,
            host_process_id,
        ) {
            return Some(scoped.selected);
        }
    }

    let same_host_valid = filter_by_host_process(all_valid, host_process_id);
    if !same_host_valid.is_empty() {
        if let Some(scoped) = select_unique_scoped_pre(same_host_valid) {
            if !project_external_selection_is_guarded(
                kirin_root,
                &scoped,
                post_project_hash,
                post_daw_session_id,
                host_process_id,
            ) {
                return Some(scoped.selected);
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
            post_daw_session_id,
            host_process_id,
        );
    }

    let scoped_dirs = discover_pre_dirs_for_post_project(kirin_root, post_project_hash);
    select_unique_pre_unless_guarded(
        collect_selected_pre_from_dirs(&scoped_dirs, pair_pre_name, require_active),
        kirin_root,
        post_project_hash,
        post_daw_session_id,
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
    select_target_pre_core_for_post_project(kirin_root, pair_pre_name, post_project_hash, "", true)
}

/// POST-project-scoped display target selection with DAW document identity.
pub fn select_target_pre_for_post_project_in_session(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    post_daw_session_id: &str,
) -> Option<SelectedPre> {
    select_target_pre_core_for_post_project(
        kirin_root,
        pair_pre_name,
        post_project_hash,
        post_daw_session_id,
        true,
    )
}

/// POST-project-scoped Arm/Keep target selection.
pub fn select_target_pre_for_arm_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
) -> Option<SelectedPre> {
    select_target_pre_core_for_post_project(kirin_root, pair_pre_name, post_project_hash, "", false)
}

/// POST-project-scoped Arm/Keep target selection with DAW document identity.
pub fn select_target_pre_for_arm_for_post_project_in_session(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    post_daw_session_id: &str,
) -> Option<SelectedPre> {
    select_target_pre_core_for_post_project(
        kirin_root,
        pair_pre_name,
        post_project_hash,
        post_daw_session_id,
        false,
    )
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LatchedPreReadiness {
    /// The exact PRE runtime was observed with a live owner lease in this DAW process.
    Confirmed,
    /// DAW state restored an exact locator before that PRE runtime republished its snapshot.
    RestoredWaiting,
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
    /// DAW document/session identity at latch time, when available.
    pub daw_session_id: Option<String>,
    /// DAW host process ID at latch time, when available.
    pub host_process_id: Option<u32>,
    /// Runtime proof state for exact DAW-state restoration.
    pub readiness: LatchedPreReadiness,
}

/// Direct read state for a latched PRE.
pub struct LatchedPreState {
    /// Exact PRE instance ID read from the snapshot.
    pub instance_id: Option<String>,
    /// PRE `name` field.
    pub name: Option<String>,
    /// Exact PRE signal state if it was present in `pre.json`.
    pub signal_state: Option<SignalState>,
    /// `signal_state == "active"`.
    pub active: bool,
    /// `t` age is within the PRE freshness TTL.
    pub fresh: bool,
    /// DAW document/session identity from PRE `pre.json`, when available.
    pub daw_session_id: Option<String>,
    /// DAW process that owns the PRE runtime snapshot, when present.
    pub host_process_id: Option<u32>,
}

/// Read exactly one latched PRE `pre.json`.
///
/// This does not scan shelves or re-evaluate ambiguity. After a latch is established, identity
/// stays bound to this file until the POST pair name changes or a confirmed current owner
/// explicitly releases its lease. A restored locator is not confirmed by previous-process residue.
pub fn read_pre_at(pre_json: &Path) -> Option<LatchedPreState> {
    let content = fs::read_to_string(pre_json).ok()?;
    let v: serde_json::Value = serde_json::from_str(&content).ok()?;
    let instance_id = v
        .get("instance_id")
        .and_then(|x| x.as_str())
        .map(str::to_string);
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
    let daw_session_id = v
        .get("daw_session_id")
        .and_then(|x| x.as_str())
        .filter(|s| !s.is_empty())
        .map(str::to_string);
    let host_process_id = v
        .get("host_process_id")
        .and_then(|x| x.as_u64())
        .and_then(|value| u32::try_from(value).ok())
        .filter(|value| *value != 0);
    Some(LatchedPreState {
        instance_id,
        name,
        signal_state,
        active,
        fresh,
        daw_session_id,
        host_process_id,
    })
}

fn snapshot_matches_exact_latch_runtime(latched: &LatchedPre) -> bool {
    let snapshot_matches = read_pre_at(&latched.pre_json).is_some_and(|state| {
        state.instance_id.as_deref() == Some(latched.instance_id.as_str())
            && latched
                .host_process_id
                .is_none_or(|host| state.host_process_id == Some(host))
    });
    snapshot_matches
        && crate::watch_snapshot_lease::snapshot_file_has_current_live_owner(&latched.pre_json)
}

/// Promote a saved fixed locator only after the new DAW process proves that the exact PRE runtime
/// owns that path. A stale JSON/lease left by the previous process can never satisfy this step.
pub fn confirm_restored_latch_runtime(latched: &Mutex<Option<LatchedPre>>) -> bool {
    let candidate = latched
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .clone();
    let Some(candidate) = candidate else {
        return false;
    };
    if candidate.readiness == LatchedPreReadiness::Confirmed {
        return true;
    }
    if !snapshot_matches_exact_latch_runtime(&candidate) {
        return false;
    }

    let mut binding = latched
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner());
    let Some(current) = binding.as_mut() else {
        return false;
    };
    if current.instance_id != candidate.instance_id || current.pre_json != candidate.pre_json {
        return false;
    }
    current.readiness = LatchedPreReadiness::Confirmed;
    true
}

enum LatchedTarget {
    None,
    RestoredWaiting,
    Selected(SelectedPre),
}

fn selected_from_latch(pair_pre_name: &str, latched: &Mutex<Option<LatchedPre>>) -> LatchedTarget {
    let Some(l) = latched.lock().ok().and_then(|binding| binding.clone()) else {
        return LatchedTarget::None;
    };
    if l.name != pair_pre_name {
        return LatchedTarget::None;
    }
    if l.readiness == LatchedPreReadiness::RestoredWaiting
        && !confirm_restored_latch_runtime(latched)
    {
        return LatchedTarget::RestoredWaiting;
    }
    if crate::watch_snapshot_lease::snapshot_file_has_released_current_owner(&l.pre_json) {
        return LatchedTarget::None;
    }
    LatchedTarget::Selected(SelectedPre {
        instance_id: l.instance_id.clone(),
        pre_json: l.pre_json.clone(),
        project_dir: l.project_dir.clone(),
        daw_session_id: l.daw_session_id.clone(),
        host_process_id: l.host_process_id,
    })
}

fn selected_from_latch_for_post_project(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    post_daw_session_id: &str,
    latched: &Mutex<Option<LatchedPre>>,
) -> LatchedTarget {
    match selected_from_latch(pair_pre_name, latched) {
        LatchedTarget::Selected(sel)
            if selected_from_latch_is_in_post_scope(
                kirin_root,
                &sel,
                post_project_hash,
                post_daw_session_id,
            ) =>
        {
            LatchedTarget::Selected(sel)
        }
        LatchedTarget::RestoredWaiting => LatchedTarget::RestoredWaiting,
        _ => LatchedTarget::None,
    }
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
    match selected_from_latch(pair_pre_name, latched) {
        LatchedTarget::Selected(sel) => return Some(sel),
        LatchedTarget::RestoredWaiting => return None,
        LatchedTarget::None => {}
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
    match selected_from_latch_for_post_project(
        kirin_root,
        pair_pre_name,
        post_project_hash,
        "",
        latched,
    ) {
        LatchedTarget::Selected(sel) => return Some(sel),
        LatchedTarget::RestoredWaiting => return None,
        LatchedTarget::None => {}
    }
    select_target_pre_core_for_post_project(kirin_root, pair_pre_name, post_project_hash, "", false)
}

/// Resolve POST-project-scoped Arm/Keep target with DAW document identity.
pub fn resolve_arm_target_for_post_project_in_session(
    kirin_root: &Path,
    pair_pre_name: &str,
    post_project_hash: &str,
    post_daw_session_id: &str,
    latched: &Mutex<Option<LatchedPre>>,
) -> Option<SelectedPre> {
    match selected_from_latch_for_post_project(
        kirin_root,
        pair_pre_name,
        post_project_hash,
        post_daw_session_id,
        latched,
    ) {
        LatchedTarget::Selected(sel) => return Some(sel),
        LatchedTarget::RestoredWaiting => return None,
        LatchedTarget::None => {}
    }
    select_target_pre_core_for_post_project(
        kirin_root,
        pair_pre_name,
        post_project_hash,
        post_daw_session_id,
        false,
    )
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
