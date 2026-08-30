use std::collections::{BTreeMap, BTreeSet};

use serde::Serialize;

use crate::input::SelectionManifest;
use crate::metrics::EvaluationReport;

const FORMAL_PERFORMANCE_IDS: usize = 290;
const FORMAL_IDS_PER_FOLD: usize = 58;

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub(crate) struct FormalReportIdentity {
    pub(super) candidate_id: String,
    pub(super) candidate_config_sha256: String,
    pub(super) measurement_definition_sha256: String,
}

#[derive(Debug)]
pub(crate) struct FormalMembershipContract {
    pub(super) manifest_sha256: String,
    pub(super) fold_ids: [BTreeSet<String>; 5],
    pub(super) performance_id_count: usize,
}

#[derive(Clone, Debug)]
pub(crate) struct FormalScoredReport {
    pub(super) identity: FormalReportIdentity,
    pub(super) evaluation: EvaluationReport,
}

impl FormalMembershipContract {
    pub(crate) fn from_manifest(manifest: &SelectionManifest) -> Result<Self, String> {
        require_sha256(&manifest.sha256, "formal membership manifest")?;
        if manifest.entries.len() != FORMAL_PERFORMANCE_IDS {
            return Err(format!(
                "formal membership requires exactly {FORMAL_PERFORMANCE_IDS} manifest IDs"
            ));
        }
        let mut fold_ids: [BTreeSet<String>; 5] = std::array::from_fn(|_| BTreeSet::new());
        let mut all_ids = BTreeSet::new();
        for entry in &manifest.entries {
            let formal = entry
                .formal
                .as_ref()
                .ok_or("formal membership received an opened-diagnostic row")?;
            let ids = fold_ids
                .get_mut(usize::from(formal.fold))
                .ok_or("formal membership contains a fold outside 0..4")?;
            if entry.id.is_empty()
                || !all_ids.insert(entry.id.clone())
                || !ids.insert(entry.id.clone())
            {
                return Err("formal membership contains an empty or duplicate ID".to_string());
            }
        }
        if fold_ids.iter().any(|ids| ids.len() != FORMAL_IDS_PER_FOLD) {
            return Err(format!(
                "formal membership requires exactly {FORMAL_IDS_PER_FOLD} IDs per fold"
            ));
        }
        Ok(Self {
            manifest_sha256: manifest.sha256.clone(),
            fold_ids,
            performance_id_count: all_ids.len(),
        })
    }

    #[cfg(test)]
    pub(super) fn synthetic(fold_ids: [BTreeSet<String>; 5]) -> Self {
        let performance_id_count = fold_ids.iter().map(BTreeSet::len).sum();
        Self {
            manifest_sha256: "aa".repeat(32),
            fold_ids,
            performance_id_count,
        }
    }
}

impl FormalScoredReport {
    #[cfg(test)]
    pub(super) fn synthetic(identity: FormalReportIdentity, evaluation: EvaluationReport) -> Self {
        Self {
            identity,
            evaluation,
        }
    }
}

impl FormalReportIdentity {
    #[cfg(test)]
    pub(super) fn synthetic() -> Self {
        Self {
            candidate_id: "synthetic-candidate".to_string(),
            candidate_config_sha256: "bb".repeat(32),
            measurement_definition_sha256: "cc".repeat(32),
        }
    }
}

pub(super) fn validate_report_set(
    membership: &FormalMembershipContract,
    pooled: &FormalScoredReport,
    folds: &[(u8, FormalScoredReport)],
) -> Result<(), String> {
    require_sha256(&membership.manifest_sha256, "formal membership manifest")?;
    if pooled.identity.candidate_id.is_empty() {
        return Err("formal report identity has an empty candidate ID".to_string());
    }
    require_sha256(
        &pooled.identity.candidate_config_sha256,
        "formal candidate config",
    )?;
    require_sha256(
        &pooled.identity.measurement_definition_sha256,
        "formal measurement definition",
    )?;
    let mut expected_union = BTreeSet::new();
    for ids in &membership.fold_ids {
        for id in ids {
            if !expected_union.insert(id.clone()) {
                return Err("formal manifest fold memberships are not disjoint".to_string());
            }
        }
    }
    if expected_union.len() != membership.performance_id_count {
        return Err("formal manifest fold membership coverage is incomplete".to_string());
    }

    let mut folded_tracks = BTreeMap::new();
    for (fold, report) in folds {
        if report.identity != pooled.identity {
            return Err(
                "formal reports do not share one candidate/config/definition identity".to_string(),
            );
        }
        let tracks = track_records(&report.evaluation)?;
        let actual_ids = tracks.keys().cloned().collect::<BTreeSet<_>>();
        if actual_ids != membership.fold_ids[usize::from(*fold)] {
            return Err(format!(
                "formal fold {fold} track IDs do not exactly match manifest membership"
            ));
        }
        for (id, bytes) in tracks {
            if folded_tracks.insert(id, bytes).is_some() {
                return Err("formal fold report track IDs are not disjoint".to_string());
            }
        }
    }
    if folded_tracks.keys().cloned().collect::<BTreeSet<_>>() != expected_union {
        return Err("formal fold reports do not cover the complete manifest".to_string());
    }
    if track_records(&pooled.evaluation)? != folded_tracks {
        return Err("formal pooled report is not the exact union of fold tracks".to_string());
    }
    Ok(())
}

fn track_records(evaluation: &EvaluationReport) -> Result<BTreeMap<String, Vec<u8>>, String> {
    let mut records = BTreeMap::new();
    for track in &evaluation.tracks {
        if track.performance_id.is_empty() {
            return Err("formal report contains an empty performance ID".to_string());
        }
        let bytes = serde_json::to_vec(track)
            .map_err(|error| format!("cannot bind formal track result: {error}"))?;
        if records
            .insert(track.performance_id.clone(), bytes)
            .is_some()
        {
            return Err("formal report contains a duplicate performance ID".to_string());
        }
    }
    Ok(records)
}

fn require_sha256(value: &str, name: &str) -> Result<(), String> {
    if value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        Ok(())
    } else {
        Err(format!("{name} SHA-256 must be 64 lowercase hex digits"))
    }
}
