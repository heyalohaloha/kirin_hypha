//! AU and VST3 are separate binaries and may publish different project/session identities for
//! instances in the same DAW document. Pair selection already treats an explicit PRE instance in
//! the same host process as authoritative. This module applies that same evidence to POST-only
//! operations (All Keep, All Stop, ready counts, and pair-ownership display) without weakening
//! automatic PRE selection.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

use crate::pairing_scope::{
    enumerate_live_pre_pair_choices_for_post_project_in_session,
    resolve_published_pair_claim_for_arm, SelectedPre,
};
use crate::post_candidates::{
    enumerate_active_post_pair_candidates,
    enumerate_active_post_pair_candidates_for_broadcast_scope, enumerate_live_post_pair_candidates,
    enumerate_live_post_pair_candidates_for_broadcast_scope, PostCandidate,
};
use crate::pre_candidates::{enumerate_live_pre_pair_choices, PreCandidate};

fn claims_visible_pre(
    candidate: &PostCandidate,
    visible_pre: &[PreCandidate],
    all_live_pre: &[PreCandidate],
) -> bool {
    if let Some(instance_id) = candidate.paired_pre_instance_id.as_deref() {
        if visible_pre.iter().any(|pre| pre.instance_id == instance_id) {
            return true;
        }
        if all_live_pre
            .iter()
            .any(|pre| pre.instance_id == instance_id)
        {
            return false;
        }
    }

    // Match the arm resolver's delete/recreate contract: an absent exact runtime may reconnect by
    // its human selector, but only when that selector resolves to one visible PRE.
    let Some(name) = candidate.pair_pre_name.as_deref() else {
        return false;
    };
    visible_pre
        .iter()
        .filter(|pre| pre.name.as_deref() == Some(name))
        .take(2)
        .count()
        == 1
}

fn merge_operation_group(
    strict_scope: Vec<PostCandidate>,
    all_candidates: Vec<PostCandidate>,
    visible_pre: &[PreCandidate],
    all_live_pre: &[PreCandidate],
    host_process_id: u32,
) -> Vec<PostCandidate> {
    let strict_paths: BTreeSet<PathBuf> = strict_scope
        .iter()
        .map(|candidate| candidate.path.clone())
        .collect();
    let mut grouped: Vec<PostCandidate> = all_candidates
        .into_iter()
        .filter(|candidate| {
            strict_paths.contains(&candidate.path)
                || (host_process_id != 0
                    && candidate.host_process_id == Some(host_process_id)
                    && claims_visible_pre(candidate, visible_pre, all_live_pre))
        })
        .collect();
    grouped.sort_by(|a, b| a.instance_id.cmp(&b.instance_id));
    grouped.dedup_by(|a, b| a.path == b.path);
    grouped
}

fn strict_scope_with_current_project(
    mut broadcast_scope: Vec<PostCandidate>,
    all_candidates: &[PostCandidate],
    current_project_uuid: &str,
    host_process_id: u32,
) -> Vec<PostCandidate> {
    let host_matches = |candidate: &PostCandidate| {
        host_process_id == 0
            || candidate.host_process_id.is_none()
            || candidate.host_process_id == Some(host_process_id)
    };
    broadcast_scope.retain(&host_matches);
    broadcast_scope.extend(
        all_candidates
            .iter()
            .filter(|candidate| {
                candidate.project_uuid == current_project_uuid && host_matches(candidate)
            })
            .cloned(),
    );
    broadcast_scope
}

/// Non-bypassed POSTs participating in the current explicit-pair operation group.
///
/// The existing DAW/session scope remains the baseline. A POST from another AU/VST3 shelf is added
/// only when it is in this host process and claims a PRE that the current POST can explicitly see.
pub fn enumerate_active_post_pair_candidates_for_operation_group(
    kirin_root: &Path,
    current_project_uuid: &str,
    current_daw_session_id: &str,
    host_process_id: u32,
) -> Vec<PostCandidate> {
    let visible_pre = enumerate_live_pre_pair_choices_for_post_project_in_session(
        kirin_root,
        current_project_uuid,
        current_daw_session_id,
    );
    let all_live_pre = enumerate_live_pre_pair_choices(kirin_root);
    let all_candidates = enumerate_active_post_pair_candidates(kirin_root);
    let strict_scope = strict_scope_with_current_project(
        enumerate_active_post_pair_candidates_for_broadcast_scope(
            kirin_root,
            current_daw_session_id,
            host_process_id,
        ),
        &all_candidates,
        current_project_uuid,
        host_process_id,
    );
    merge_operation_group(
        strict_scope,
        all_candidates,
        &visible_pre,
        &all_live_pre,
        host_process_id,
    )
}

/// Live POST pair claims participating in the current explicit-pair operation group.
///
/// Unlike the active variant, bypassed POST claims remain visible so ownership cannot silently be
/// reassigned while a paired instance is bypassed.
pub fn enumerate_live_post_pair_candidates_for_operation_group(
    kirin_root: &Path,
    current_project_uuid: &str,
    current_daw_session_id: &str,
    host_process_id: u32,
) -> Vec<PostCandidate> {
    let visible_pre = enumerate_live_pre_pair_choices_for_post_project_in_session(
        kirin_root,
        current_project_uuid,
        current_daw_session_id,
    );
    let all_live_pre = enumerate_live_pre_pair_choices(kirin_root);
    let all_candidates = enumerate_live_post_pair_candidates(kirin_root);
    let strict_scope = strict_scope_with_current_project(
        enumerate_live_post_pair_candidates_for_broadcast_scope(
            kirin_root,
            current_daw_session_id,
            host_process_id,
        ),
        &all_candidates,
        current_project_uuid,
        host_process_id,
    );
    merge_operation_group(
        strict_scope,
        all_candidates,
        &visible_pre,
        &all_live_pre,
        host_process_id,
    )
}

fn resolve_claim(kirin_root: &Path, candidate: &PostCandidate) -> Option<SelectedPre> {
    resolve_published_pair_claim_for_arm(
        kirin_root,
        candidate.pair_pre_name.as_deref(),
        candidate.paired_pre_instance_id.as_deref(),
        &candidate.project_uuid,
        candidate.daw_session_id.as_deref().unwrap_or(""),
    )
}

/// Keep-ready POSTs in the current operation group.
pub fn enumerate_ready_post_pair_candidates_for_operation_group(
    kirin_root: &Path,
    current_project_uuid: &str,
    current_daw_session_id: &str,
    host_process_id: u32,
) -> Vec<PostCandidate> {
    enumerate_active_post_pair_candidates_for_operation_group(
        kirin_root,
        current_project_uuid,
        current_daw_session_id,
        host_process_id,
    )
    .into_iter()
    .filter_map(|mut candidate| {
        let selected = resolve_claim(kirin_root, &candidate)?;
        candidate.paired_pre_instance_id = Some(selected.instance_id);
        Some(candidate)
    })
    .collect()
}

/// Live, resolved pair owners in the current operation group, including bypassed POSTs.
pub fn enumerate_owned_post_pair_candidates_for_operation_group(
    kirin_root: &Path,
    current_project_uuid: &str,
    current_daw_session_id: &str,
    host_process_id: u32,
) -> Vec<PostCandidate> {
    enumerate_live_post_pair_candidates_for_operation_group(
        kirin_root,
        current_project_uuid,
        current_daw_session_id,
        host_process_id,
    )
    .into_iter()
    .filter_map(|mut candidate| {
        let selected = resolve_claim(kirin_root, &candidate)?;
        candidate.paired_pre_instance_id = Some(selected.instance_id);
        Some(candidate)
    })
    .collect()
}

/// Project shelves that must receive an All Keep file for this operation group.
pub fn active_post_project_uuids_for_operation_group(
    kirin_root: &Path,
    current_project_uuid: &str,
    current_daw_session_id: &str,
    host_process_id: u32,
) -> Vec<String> {
    enumerate_ready_post_pair_candidates_for_operation_group(
        kirin_root,
        current_project_uuid,
        current_daw_session_id,
        host_process_id,
    )
    .into_iter()
    .map(|candidate| candidate.project_uuid)
    .collect::<BTreeSet<_>>()
    .into_iter()
    .collect()
}

/// Project shelves that must receive an All Stop file, including bypassed live pair owners.
pub fn live_post_project_uuids_for_operation_group(
    kirin_root: &Path,
    current_project_uuid: &str,
    current_daw_session_id: &str,
    host_process_id: u32,
) -> Vec<String> {
    enumerate_owned_post_pair_candidates_for_operation_group(
        kirin_root,
        current_project_uuid,
        current_daw_session_id,
        host_process_id,
    )
    .into_iter()
    .map(|candidate| candidate.project_uuid)
    .collect::<BTreeSet<_>>()
    .into_iter()
    .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn post(
        instance_id: &str,
        project_uuid: &str,
        daw_session_id: &str,
        host_process_id: u32,
        pre_instance_id: Option<&str>,
        pre_name: Option<&str>,
    ) -> PostCandidate {
        PostCandidate {
            instance_id: instance_id.into(),
            project_uuid: project_uuid.into(),
            daw_session_id: Some(daw_session_id.into()),
            host_process_id: Some(host_process_id),
            pair_pre_name: pre_name.map(str::to_string),
            paired_pre_instance_id: pre_instance_id.map(str::to_string),
            pair_claimed_at: 1.0,
            path: PathBuf::from(project_uuid)
                .join(instance_id)
                .join("post.json"),
        }
    }

    fn pre(instance_id: &str, name: &str) -> PreCandidate {
        PreCandidate {
            instance_id: instance_id.into(),
            lufs_m: None,
            true_peak: None,
            crest: None,
            path: PathBuf::from(instance_id).join("pre.json"),
            name: Some(name.into()),
            host_process_id: None,
            daw_session_id: None,
        }
    }

    #[test]
    fn operation_group_bridges_au_vst_shelves_by_exact_visible_pre() {
        let host = 42;
        let au = post(
            "post-2mix",
            "project-au",
            "daw-au",
            host,
            Some("pre-2mix"),
            Some("2Mix"),
        );
        let drum = post(
            "post-drum",
            "project-vst",
            "daw-vst",
            host,
            Some("pre-drum"),
            Some("Drum"),
        );
        let music = post(
            "post-music",
            "project-vst",
            "daw-vst",
            host,
            Some("pre-music"),
            Some("Music"),
        );
        let grouped = merge_operation_group(
            vec![au.clone()],
            vec![au, drum, music],
            &[
                pre("pre-2mix", "2Mix"),
                pre("pre-drum", "Drum"),
                pre("pre-music", "Music"),
            ],
            &[
                pre("pre-2mix", "2Mix"),
                pre("pre-drum", "Drum"),
                pre("pre-music", "Music"),
            ],
            host,
        );
        assert_eq!(
            grouped
                .iter()
                .map(|candidate| candidate.instance_id.as_str())
                .collect::<Vec<_>>(),
            vec!["post-2mix", "post-drum", "post-music"]
        );
    }

    #[test]
    fn operation_group_rejects_other_host_and_nonvisible_exact_claim() {
        let host = 42;
        let au = post(
            "post-2mix",
            "project-au",
            "daw-au",
            host,
            Some("pre-2mix"),
            Some("2Mix"),
        );
        let other_host = post(
            "post-other-host",
            "project-vst",
            "daw-vst",
            77,
            Some("pre-drum"),
            Some("Drum"),
        );
        let hidden = post(
            "post-hidden",
            "project-vst",
            "daw-vst",
            host,
            Some("pre-hidden"),
            Some("Hidden"),
        );
        let strict = strict_scope_with_current_project(
            vec![au.clone(), other_host.clone()],
            &[au.clone(), other_host.clone(), hidden.clone()],
            "project-au",
            host,
        );
        let grouped = merge_operation_group(
            strict,
            vec![au, other_host, hidden],
            &[pre("pre-2mix", "2Mix"), pre("pre-drum", "Drum")],
            &[pre("pre-2mix", "2Mix"), pre("pre-drum", "Drum")],
            host,
        );
        assert_eq!(grouped.len(), 1);
        assert_eq!(grouped[0].instance_id, "post-2mix");
    }

    #[test]
    fn legacy_name_claim_requires_one_visible_pre() {
        let host = 42;
        let legacy = post(
            "post-legacy",
            "project-vst",
            "daw-vst",
            host,
            None,
            Some("Drum"),
        );
        assert_eq!(
            merge_operation_group(
                Vec::new(),
                vec![legacy.clone()],
                &[pre("pre-drum", "Drum")],
                &[pre("pre-drum", "Drum")],
                host
            )
            .len(),
            1
        );
        assert!(merge_operation_group(
            Vec::new(),
            vec![legacy],
            &[pre("pre-drum-a", "Drum"), pre("pre-drum-b", "Drum")],
            &[pre("pre-drum-a", "Drum"), pre("pre-drum-b", "Drum")],
            host,
        )
        .is_empty());
    }

    #[test]
    fn current_project_remains_in_strict_baseline_without_shell_ids() {
        let current = post("post-current", "project-current", "", 0, None, Some("Drum"));
        let other = post("post-other", "project-other", "", 0, None, Some("Music"));
        let strict = strict_scope_with_current_project(
            Vec::new(),
            &[current.clone(), other],
            "project-current",
            0,
        );
        assert_eq!(strict, vec![current]);
    }

    #[test]
    fn stale_exact_claim_reconnects_only_by_unique_visible_name() {
        let host = 42;
        let stale_exact = post(
            "post-drum",
            "project-vst",
            "daw-vst",
            host,
            Some("pre-deleted"),
            Some("Drum"),
        );
        assert_eq!(
            merge_operation_group(
                Vec::new(),
                vec![stale_exact.clone()],
                &[pre("pre-recreated", "Drum")],
                &[pre("pre-recreated", "Drum")],
                host,
            )
            .len(),
            1
        );
        assert!(merge_operation_group(
            Vec::new(),
            vec![stale_exact],
            &[
                pre("pre-recreated-a", "Drum"),
                pre("pre-recreated-b", "Drum")
            ],
            &[
                pre("pre-recreated-a", "Drum"),
                pre("pre-recreated-b", "Drum")
            ],
            host,
        )
        .is_empty());

        assert!(merge_operation_group(
            Vec::new(),
            vec![post(
                "post-drum",
                "project-vst",
                "daw-vst",
                host,
                Some("pre-exact-outside"),
                Some("Drum"),
            )],
            &[pre("pre-recreated", "Drum")],
            &[
                pre("pre-recreated", "Drum"),
                pre("pre-exact-outside", "Drum")
            ],
            host,
        )
        .is_empty());
    }
}
