//! Pairing scope inference.
//!
//! This module owns the session-scope heuristic used when PRE and POST project UUIDs do not
//! naturally match across plugin binary boundaries. It deliberately does not write record_signal
//! files or decide Keep state. It only answers: "which fresh PRE project directories belong to the
//! POST project we are looking from?"

use std::collections::HashSet;
use std::path::{Path, PathBuf};

use crate::record_signal::{
    enumerate_active_pre_pair_candidates, scan_pre_candidates_in, PreCandidate,
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
