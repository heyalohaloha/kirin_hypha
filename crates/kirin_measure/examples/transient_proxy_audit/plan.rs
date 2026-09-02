use std::collections::{BTreeMap, HashSet};
use std::fs;
use std::path::{Component, Path};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

use super::contract::{is_sha256, PROFILE, PURPOSE, SELECTION_SEED, TOOL_VERSION};
use super::io::sha256_bytes;
use super::manifest::{SourceArtifact, SourceKind, SourceTrack};
use super::midi::read_proxy_onsets;

pub(crate) const PLAN_SCHEMA: &str = "kirin-hypha-attack-midi-proxy-audit-plan-v1";
pub(crate) const TARGET_DURATION_MICROS: u64 = 600_000_000;
pub(crate) const FORMAL_DEVELOPMENT_BLOCKER: &str =
    "formal development audit is disabled until official audio identity, exact excerpt bytes, and a quota-stratified audit selection are pinned";

pub(crate) fn formal_development_audit_enabled() -> bool {
    false
}
const MAX_EXCERPT_MICROS: u64 = 60_000_000;
const SELECTION_ALGORITHM: &str =
    "sha256-length-prefixed-unique-performance-one-render-exact-600s-v1";
const DEFINITION: &str = concat!(
    "profile=DRUM\n",
    "purpose=midi-proxy-blind-acoustic-development\n",
    "candidate_output=forbidden\n",
    "audio_read=forbidden\n",
    "source=train_or_validation_development_selection_or_synthetic_fixture\n",
    "seed=ATTACK-MIDI-PROXY-AUDIT-20260830\n",
    "duration_micros=600000000\n",
    "max_excerpt_micros=60000000\n",
    "excerpt_interval=half_open_start_inclusive_end_exclusive\n",
    "midi_compound_span_micros=30000_inclusive_nonchaining_mean\n",
    "human_raw_tap_compound_span_micros=30000_inclusive_nonchaining_mean\n",
    "matching_tolerance_micros=25000_inclusive\n",
    "matching=max_cardinality_min_total_absolute_error_early_reference_early_prediction\n",
    "inter_annotator_f1_min=0.90\n",
    "midi_proxy_precision_recall_min_each_annotator=0.95\n",
    "incomplete_annotation=not_ready\n",
);

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct AuditPlan {
    pub(crate) schema: String,
    pub(crate) tool_version: String,
    pub(crate) definition_sha256: String,
    pub(crate) profile: String,
    pub(crate) purpose: String,
    pub(crate) selection_seed: String,
    pub(crate) selection_algorithm: String,
    pub(crate) source_kind: SourceKind,
    pub(crate) source_id: String,
    pub(crate) source_sha256: String,
    pub(crate) target_duration_micros: u64,
    pub(crate) selected_duration_micros: u64,
    pub(crate) candidate_output_observed: bool,
    pub(crate) audio_opened_by_tool: bool,
    pub(crate) test_or_fresh_holdout_permitted: bool,
    pub(crate) two_mix_permitted: bool,
    pub(crate) coordinator_only_contains_midi_proxy: bool,
    pub(crate) items: Vec<AuditItem>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct AuditItem {
    pub(crate) item_id: String,
    pub(crate) selection_rank: u32,
    pub(crate) fold: String,
    pub(crate) drummer: String,
    pub(crate) performance_id: String,
    pub(crate) source_split: String,
    pub(crate) kit_name: String,
    pub(crate) audio_relative_path: String,
    pub(crate) midi_sha256: String,
    pub(crate) source_duration_micros: u64,
    pub(crate) segment_start_micros: u64,
    pub(crate) segment_duration_micros: u64,
    pub(crate) midi_proxy_onsets_micros: Vec<u64>,
}

#[derive(Debug)]
pub(crate) struct PlanArtifact {
    pub(crate) plan: AuditPlan,
    pub(crate) raw_sha256: String,
}

pub(crate) fn definition_sha256() -> String {
    sha256_bytes(DEFINITION.as_bytes())
}

pub(crate) fn build_plan(
    source: &SourceArtifact,
    midi_root: Option<&Path>,
) -> Result<AuditPlan, String> {
    if source.kind == SourceKind::DevelopmentSelection {
        return Err(FORMAL_DEVELOPMENT_BLOCKER.to_string());
    }
    match (source.kind, midi_root) {
        (SourceKind::DevelopmentSelection, None) => {
            return Err("development selection requires an explicit MIDI root".to_string())
        }
        (SourceKind::SyntheticFixture, Some(_)) => {
            return Err("synthetic fixture must not have a MIDI root".to_string())
        }
        _ => {}
    }

    let representatives = representatives(source)?;
    let mut remaining = TARGET_DURATION_MICROS;
    let mut items = Vec::new();
    for track in representatives {
        if remaining == 0 {
            break;
        }
        let duration = track.duration_micros.min(MAX_EXCERPT_MICROS).min(remaining);
        let start = deterministic_start(source, &track, duration);
        let all_onsets = match &track.proxy_onsets_micros {
            Some(onsets) => onsets.clone(),
            None => read_proxy_onsets(
                midi_root.expect("development MIDI root checked above"),
                &track.midi_relative_path,
                &track.midi_sha256,
            )?,
        };
        let end = start
            .checked_add(duration)
            .ok_or("audit segment overflow")?;
        let proxy = all_onsets
            .into_iter()
            .filter(|&onset| onset >= start && onset < end)
            .map(|onset| onset - start)
            .collect::<Vec<_>>();
        if proxy.is_empty() {
            return Err(format!(
                "deterministic audit segment has no MIDI proxy events: {}",
                track.performance_id
            ));
        }
        items.push(AuditItem {
            item_id: format!("audit-{:03}", items.len() + 1),
            selection_rank: track.selection_rank,
            fold: track.fold,
            drummer: track.drummer,
            performance_id: track.performance_id,
            source_split: track.split,
            kit_name: track.kit_name,
            audio_relative_path: track.audio_relative_path,
            midi_sha256: track.midi_sha256,
            source_duration_micros: track.duration_micros,
            segment_start_micros: start,
            segment_duration_micros: duration,
            midi_proxy_onsets_micros: proxy,
        });
        remaining -= duration;
    }
    if remaining != 0 {
        return Err(format!(
            "unique development duration is short of 600 seconds by {:.6}s",
            remaining as f64 / 1_000_000.0
        ));
    }
    let plan = AuditPlan {
        schema: PLAN_SCHEMA.to_string(),
        tool_version: TOOL_VERSION.to_string(),
        definition_sha256: definition_sha256(),
        profile: PROFILE.to_string(),
        purpose: PURPOSE.to_string(),
        selection_seed: SELECTION_SEED.to_string(),
        selection_algorithm: SELECTION_ALGORITHM.to_string(),
        source_kind: source.kind,
        source_id: source.id.clone(),
        source_sha256: source.raw_sha256.clone(),
        target_duration_micros: TARGET_DURATION_MICROS,
        selected_duration_micros: TARGET_DURATION_MICROS,
        candidate_output_observed: false,
        audio_opened_by_tool: false,
        test_or_fresh_holdout_permitted: false,
        two_mix_permitted: false,
        coordinator_only_contains_midi_proxy: true,
        items,
    };
    validate_plan(&plan)?;
    Ok(plan)
}

pub(crate) fn render_plan(plan: &AuditPlan) -> Result<Vec<u8>, String> {
    validate_plan(plan)?;
    let mut bytes = serde_json::to_vec_pretty(plan)
        .map_err(|error| format!("cannot serialize audit plan: {error}"))?;
    bytes.push(b'\n');
    Ok(bytes)
}

pub(crate) fn read_plan(path: &Path) -> Result<PlanArtifact, String> {
    let bytes = fs::read(path)
        .map_err(|error| format!("cannot read audit plan {}: {error}", path.display()))?;
    let plan: AuditPlan = serde_json::from_slice(&bytes)
        .map_err(|error| format!("invalid audit plan JSON: {error}"))?;
    validate_plan(&plan)?;
    if plan.source_kind == SourceKind::DevelopmentSelection {
        return Err(FORMAL_DEVELOPMENT_BLOCKER.to_string());
    }
    Ok(PlanArtifact {
        plan,
        raw_sha256: sha256_bytes(&bytes),
    })
}

fn representatives(source: &SourceArtifact) -> Result<Vec<SourceTrack>, String> {
    let mut grouped = BTreeMap::<String, Vec<SourceTrack>>::new();
    for track in &source.tracks {
        grouped
            .entry(track.performance_id.clone())
            .or_default()
            .push(track.clone());
    }
    let mut representatives = Vec::with_capacity(grouped.len());
    for (_, mut renders) in grouped {
        renders.sort_by_key(|track| rank_key("render", source, track));
        representatives.push(renders.remove(0));
    }
    representatives.sort_by_key(|track| rank_key("performance", source, track));
    if representatives.is_empty() {
        return Err("audit source has no unique performances".to_string());
    }
    Ok(representatives)
}

fn deterministic_start(source: &SourceArtifact, track: &SourceTrack, duration: u64) -> u64 {
    let maximum = track.duration_micros - duration;
    if maximum == 0 {
        return 0;
    }
    let key = rank_key("segment-start", source, track);
    u64::from_be_bytes(key[..8].try_into().unwrap()) % (maximum + 1)
}

fn rank_key(domain: &str, source: &SourceArtifact, track: &SourceTrack) -> [u8; 32] {
    let mut hasher = Sha256::new();
    for value in [
        TOOL_VERSION,
        SELECTION_SEED,
        domain,
        &source.raw_sha256,
        &track.performance_id,
        &track.kit_name,
        &track.audio_relative_path,
    ] {
        hasher.update((value.len() as u64).to_be_bytes());
        hasher.update(value.as_bytes());
    }
    hasher.finalize().into()
}

fn validate_plan(plan: &AuditPlan) -> Result<(), String> {
    if plan.schema != PLAN_SCHEMA
        || plan.tool_version != TOOL_VERSION
        || plan.definition_sha256 != definition_sha256()
        || plan.profile != PROFILE
        || plan.purpose != PURPOSE
        || plan.selection_seed != SELECTION_SEED
        || plan.selection_algorithm != SELECTION_ALGORITHM
        || plan.source_id.is_empty()
        || !is_sha256(&plan.source_sha256)
        || plan.target_duration_micros != TARGET_DURATION_MICROS
        || plan.selected_duration_micros != TARGET_DURATION_MICROS
        || plan.candidate_output_observed
        || plan.audio_opened_by_tool
        || plan.test_or_fresh_holdout_permitted
        || plan.two_mix_permitted
        || !plan.coordinator_only_contains_midi_proxy
    {
        return Err("audit plan contract mismatch".to_string());
    }
    let mut performances = HashSet::new();
    let mut selection_ranks = HashSet::new();
    let mut total = 0_u64;
    for (index, item) in plan.items.iter().enumerate() {
        let split_matches_source = match plan.source_kind {
            SourceKind::DevelopmentSelection => {
                matches!(item.source_split.as_str(), "train" | "validation")
            }
            SourceKind::SyntheticFixture => item.source_split == "synthetic",
        };
        let segment_end = item
            .segment_start_micros
            .checked_add(item.segment_duration_micros);
        if item.item_id != format!("audit-{:03}", index + 1)
            || item.selection_rank == 0
            || !selection_ranks.insert(item.selection_rank)
            || !matches!(item.fold.as_str(), "0" | "1" | "2" | "3" | "4")
            || item.performance_id.is_empty()
            || item.drummer.is_empty()
            || item.kit_name.is_empty()
            || !performances.insert(&item.performance_id)
            || !split_matches_source
            || !is_sha256(&item.midi_sha256)
            || item.source_duration_micros == 0
            || item.segment_duration_micros == 0
            || item.segment_duration_micros > MAX_EXCERPT_MICROS
            || item.midi_proxy_onsets_micros.is_empty()
            || item
                .midi_proxy_onsets_micros
                .iter()
                .any(|&onset| onset >= item.segment_duration_micros)
            || item
                .midi_proxy_onsets_micros
                .windows(2)
                .any(|pair| pair[0] >= pair[1])
            || !is_relative(&item.audio_relative_path)
            || !matches!(segment_end, Some(end) if end <= item.source_duration_micros)
        {
            return Err(format!("invalid audit plan item at index {index}"));
        }
        total = total
            .checked_add(item.segment_duration_micros)
            .ok_or("audit plan duration overflow")?;
    }
    if plan.items.is_empty() || total != TARGET_DURATION_MICROS {
        return Err("audit plan does not contain exactly 600 seconds".to_string());
    }
    Ok(())
}

fn is_relative(value: &str) -> bool {
    let path = Path::new(value);
    !path.is_absolute()
        && path
            .components()
            .all(|component| matches!(component, Component::Normal(_)))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture_source() -> SourceArtifact {
        let path = Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("examples/transient_proxy_audit/fixtures/synthetic_proxy_audit_v1.json");
        let bytes = fs::read(&path).unwrap();
        super::super::manifest::read_synthetic_fixture(&path, &sha256_bytes(&bytes)).unwrap()
    }

    #[test]
    fn fixed_seed_selection_is_exactly_ten_minutes_and_deterministic() {
        let source = fixture_source();
        let first = render_plan(&build_plan(&source, None).unwrap()).unwrap();
        let second = render_plan(&build_plan(&source, None).unwrap()).unwrap();
        assert_eq!(first, second);
        let plan: AuditPlan = serde_json::from_slice(&first).unwrap();
        assert_eq!(plan.selected_duration_micros, 600_000_000);
        assert_eq!(plan.items.len(), 12);
        assert_eq!(
            plan.items
                .iter()
                .map(|item| &item.performance_id)
                .collect::<HashSet<_>>()
                .len(),
            12
        );
        assert!(!plan.candidate_output_observed);
    }

    #[test]
    fn short_source_and_mutated_plan_fail_closed() {
        let mut source = fixture_source();
        source.tracks.truncate(2);
        assert!(build_plan(&source, None).unwrap_err().contains("short"));

        let mut plan = build_plan(&fixture_source(), None).unwrap();
        plan.test_or_fresh_holdout_permitted = true;
        assert!(render_plan(&plan).is_err());
    }

    #[test]
    fn formal_development_plan_is_disabled_before_midi_or_audio_access() {
        let mut source = fixture_source();
        source.kind = SourceKind::DevelopmentSelection;
        assert_eq!(
            build_plan(&source, None).unwrap_err(),
            FORMAL_DEVELOPMENT_BLOCKER
        );
    }
}
