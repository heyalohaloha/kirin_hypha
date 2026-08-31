use std::collections::{BTreeMap, BTreeSet};

use serde::Serialize;

use super::*;

#[derive(Serialize)]
pub(super) struct MidiBindingAudit {
    status: &'static str,
    policy: &'static str,
    tolerance_micros: u64,
    b551_raw_sha_mismatch_performance_ids: Vec<String>,
    audio_bounds_failure_performance_ids: Vec<String>,
    annotation_flag_mismatch_performance_ids: Vec<String>,
}

impl MidiBindingAudit {
    pub(super) fn passes(&self) -> bool {
        !self.raw_sha_failed() && !self.bounds_failed()
    }

    fn raw_sha_failed(&self) -> bool {
        !self.b551_raw_sha_mismatch_performance_ids.is_empty()
    }

    fn bounds_failed(&self) -> bool {
        !self.audio_bounds_failure_performance_ids.is_empty()
            || !self.annotation_flag_mismatch_performance_ids.is_empty()
    }
}

pub(super) fn audit_midi_binding(members: &[&MemberEvidence]) -> MidiBindingAudit {
    let mut sha_mismatch = Vec::new();
    let mut bounds_failure = Vec::new();
    let mut flag_mismatch = Vec::new();
    for member in members {
        if member.bundled_midi.member_sha256 != member.bundled_midi.b551_member_sha256 {
            sha_mismatch.push(member.performance_id.clone());
        }
        let midi = &member.bundled_midi;
        let actual_limit = u128::from(member.decode.actual_samples) * 1_000_000
            + u128::from(MIDI_RANGE_TOLERANCE_MICROS) * 44_100;
        let computed = midi.first_note_micros <= midi.last_note_micros
            && u128::from(midi.last_note_micros) * 44_100 <= actual_limit;
        if !computed {
            bounds_failure.push(member.performance_id.clone());
        }
        if midi.annotation_bounds_pass != computed {
            flag_mismatch.push(member.performance_id.clone());
        }
    }
    let passed = sha_mismatch.is_empty() && bounds_failure.is_empty() && flag_mismatch.is_empty();
    MidiBindingAudit {
        status: if passed { "pass" } else { "fail" },
        policy: "bundled MIDI raw SHA must equal B-551; shared-parser first/last note-on must be ordered and the last note-on must not exceed decoded audio end by more than 2000 microseconds",
        tolerance_micros: MIDI_RANGE_TOLERANCE_MICROS,
        b551_raw_sha_mismatch_performance_ids: sha_mismatch,
        audio_bounds_failure_performance_ids: bounds_failure,
        annotation_flag_mismatch_performance_ids: flag_mismatch,
    }
}

#[derive(Serialize)]
pub(super) struct DuplicateAudit {
    policy: &'static str,
    raw_wav_members: DuplicateClass,
    source_canonical_pcm: DuplicateClass,
    core_canonical_pcm: DuplicateClass,
    guard_canonical_pcm_observation: DuplicateClass,
    rejection_group_count: usize,
    cross_split_rejection_group_count: usize,
}

impl DuplicateAudit {
    pub(super) fn passes(&self) -> bool {
        self.rejection_group_count == 0
    }
}

#[derive(Serialize)]
struct DuplicateClass {
    status: &'static str,
    duplicate_group_count: usize,
    groups: Vec<DuplicateGroup>,
}

#[derive(Serialize)]
struct DuplicateGroup {
    evidence_sha256: String,
    performance_ids: Vec<String>,
    splits: Vec<DevelopmentSplit>,
}

pub(super) fn audit_duplicates(members: &[&MemberEvidence]) -> DuplicateAudit {
    let raw_wav_members = duplicate_class(groups(members, |row| row.member_sha256.clone()));
    let source_canonical_pcm = duplicate_class(groups(members, |row| {
        row.source_pcm.canonical_sha256.clone()
    }));
    let core_canonical_pcm =
        duplicate_class(groups(members, |row| row.core_pcm.canonical_sha256.clone()));
    let guard_canonical_pcm_observation = duplicate_class(groups(members, |row| {
        row.guard_pcm.canonical_sha256.clone()
    }));
    let rejection_group_count = raw_wav_members.duplicate_group_count
        + source_canonical_pcm.duplicate_group_count
        + core_canonical_pcm.duplicate_group_count;
    let cross_split_rejection_group_count =
        [&raw_wav_members, &source_canonical_pcm, &core_canonical_pcm]
            .into_iter()
            .flat_map(|class| &class.groups)
            .filter(|group| group.splits.len() > 1)
            .count();
    DuplicateAudit {
        policy: "raw WAV, source canonical PCM, or core-relative canonical PCM duplicate groups fail the component; guard is separately domain-hashed and observed only",
        raw_wav_members,
        source_canonical_pcm,
        core_canonical_pcm,
        guard_canonical_pcm_observation,
        rejection_group_count,
        cross_split_rejection_group_count,
    }
}

fn groups(
    members: &[&MemberEvidence],
    digest: impl Fn(&MemberEvidence) -> String,
) -> Vec<DuplicateGroup> {
    let mut by_hash = BTreeMap::<String, Vec<String>>::new();
    for member in members {
        by_hash
            .entry(digest(member))
            .or_default()
            .push(member.performance_id.clone());
    }
    by_hash
        .into_iter()
        .filter_map(|(evidence_sha256, mut performance_ids)| {
            if performance_ids.len() < 2 {
                return None;
            }
            performance_ids.sort();
            let ids = performance_ids.iter().collect::<BTreeSet<_>>();
            let splits = members
                .iter()
                .filter(|member| ids.contains(&member.performance_id))
                .map(|member| member.split)
                .collect::<BTreeSet<_>>()
                .into_iter()
                .collect();
            Some(DuplicateGroup {
                evidence_sha256,
                performance_ids,
                splits,
            })
        })
        .collect()
}

fn duplicate_class(groups: Vec<DuplicateGroup>) -> DuplicateClass {
    DuplicateClass {
        status: if groups.is_empty() { "pass" } else { "fail" },
        duplicate_group_count: groups.len(),
        groups,
    }
}

#[derive(Serialize)]
pub(super) struct SilenceAudit {
    status: &'static str,
    policy: &'static str,
    anomalous_member_count: usize,
    source_all_zero_performance_ids: Vec<String>,
    core_all_zero_performance_ids: Vec<String>,
    source_constant_performance_ids: Vec<String>,
    core_constant_performance_ids: Vec<String>,
}

impl SilenceAudit {
    pub(super) fn passes(&self) -> bool {
        self.anomalous_member_count == 0
    }
}

pub(super) fn audit_silence(members: &[&MemberEvidence]) -> SilenceAudit {
    let matching = |region: fn(&MemberEvidence) -> &PcmRegionEvidence,
                    predicate: fn(&PcmRegionEvidence) -> bool| {
        members
            .iter()
            .filter(|member| predicate(region(member)))
            .map(|member| member.performance_id.clone())
            .collect::<Vec<_>>()
    };
    let all_zero = |region: &PcmRegionEvidence| region.statistics.zero_samples == region.samples;
    let constant = |region: &PcmRegionEvidence| {
        region.statistics.minimum_pcm24 == region.statistics.maximum_pcm24
    };
    let source_all_zero_performance_ids = matching(|row| &row.source_pcm, all_zero);
    let core_all_zero_performance_ids = matching(|row| &row.core_pcm, all_zero);
    let source_constant_performance_ids = matching(|row| &row.source_pcm, constant);
    let core_constant_performance_ids = matching(|row| &row.core_pcm, constant);
    let anomalous = source_constant_performance_ids
        .iter()
        .chain(&core_constant_performance_ids)
        .collect::<BTreeSet<_>>();
    SilenceAudit {
        status: if anomalous.is_empty() { "pass" } else { "fail" },
        policy: "all-zero and nonzero-constant source/core are reported for all fixed IDs; any constant member fails this component without exclusion or reserve replacement",
        anomalous_member_count: anomalous.len(),
        source_all_zero_performance_ids,
        core_all_zero_performance_ids,
        source_constant_performance_ids,
        core_constant_performance_ids,
    }
}

pub(super) fn downstream_blockers(
    midi: &MidiBindingAudit,
    duplicates: &DuplicateAudit,
    silence: &SilenceAudit,
) -> Vec<&'static str> {
    let mut blockers = vec![
        "formal_authorization_not_pinned_in_source_commit",
        "fold_balance_qualification_verifier_not_implemented",
        "blind_proxy_audit_verifier_not_implemented",
        "candidate_plan_ordered_configs_stages_controls_verifier_not_implemented",
        "not_ready_context_guard_unimplemented",
        "candidate_set_completion_receipt_not_implemented",
        "lodo_loso_diagnostic_results_not_ready",
        "audio_component_does_not_construct_formal_authorization",
    ];
    if midi.raw_sha_failed() {
        blockers.push("bundled_midi_b551_raw_sha_binding_failed");
    }
    if midi.bounds_failed() {
        blockers.push("bundled_midi_audio_annotation_bounds_audit_failed");
    }
    if !duplicates.passes() {
        blockers.push("audio_duplicate_audit_failed");
    }
    if !silence.passes() {
        blockers.push("audio_silence_or_constant_audit_failed");
    }
    blockers
}
