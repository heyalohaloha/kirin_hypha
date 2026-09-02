use std::collections::{BTreeMap, BTreeSet};

use serde::{Deserialize, Serialize};

use crate::contract::sha256_bytes;
use crate::csv::parse_csv_line;

const LEDGER_BYTES: &[u8] = include_bytes!("fixtures/opened_set_ledger_v1.json");
const B546_MANIFEST_BYTES: &[u8] =
    include_bytes!("../transient_candidate_eval/fixtures/transient_egmd_selection_v1.csv");
const B548_MANIFEST_BYTES: &[u8] =
    include_bytes!("fixtures/transient_egmd_opened_b548_semantic_v1.csv");
const ISOLATION_INCIDENT_BYTES: &[u8] =
    include_bytes!("fixtures/test_isolation_incident_20260830.json");
const LEDGER_SCHEMA: &str = "kirin-hypha-attack-opened-set-ledger-v1";
const LEDGER_SHA256: &str = "e9935efba336a40b44ba46bcaa234117927c05e36bb5ca8f80f257b8ba58b3ca";
const B546_SHA256: &str = "adf8753f31ab655089a31434e66c6dbf8084a6589b5d0d32ca8358f92b5b66a3";
const B548_SHA256: &str = "151e876109722459b7d836525e5cf6e0d2e7fe1c41bc87b23c4ca6faadd6c8c3";
const ISOLATION_INCIDENT_SHA256: &str =
    "27c10a5c6de848029deb18c394181be76ed373d67269773f519708cf53494257";
const B546_PATH: &str =
    "crates/kirin_measure/examples/transient_candidate_eval/fixtures/transient_egmd_selection_v1.csv";
const B548_PATH: &str = "crates/kirin_measure/examples/transient_development_contract/fixtures/transient_egmd_opened_b548_semantic_v1.csv";
const SOURCE_HEADER: &str = "drummer,session,id,style,bpm,beat_type,time_signature,duration,split,midi_filename,audio_filename,kit_name";

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct LedgerSource {
    pub(crate) source_id: String,
    pub(crate) manifest_path: String,
    pub(crate) manifest_sha256: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct LedgerEntry {
    pub(crate) performance_id: String,
    pub(crate) sources: Vec<String>,
    pub(crate) source_split: SourceSplit,
    pub(crate) r#use: OpenedUse,
    pub(crate) fresh_holdout_excluded: bool,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, Ord, PartialEq, PartialOrd, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum SourceSplit {
    Validation,
    Test,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum OpenedUse {
    DevelopmentRequired,
    DiagnosticOnly,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct LedgerDocument {
    schema: String,
    sources: Vec<LedgerSource>,
    entries: Vec<LedgerEntry>,
}

#[derive(Clone, Debug)]
pub(crate) struct OpenedLedger {
    pub(crate) sha256: String,
    pub(crate) isolation_incident_sha256: String,
    pub(crate) fresh_holdout_isolation_breached: bool,
    pub(crate) sources: Vec<LedgerSource>,
    pub(crate) entries: Vec<LedgerEntry>,
    ids: BTreeSet<String>,
}

impl OpenedLedger {
    pub(crate) fn embedded() -> Result<Self, String> {
        if sha256_bytes(LEDGER_BYTES) != LEDGER_SHA256
            || sha256_bytes(B546_MANIFEST_BYTES) != B546_SHA256
            || sha256_bytes(B548_MANIFEST_BYTES) != B548_SHA256
            || sha256_bytes(ISOLATION_INCIDENT_BYTES) != ISOLATION_INCIDENT_SHA256
        {
            return Err("opened-set embedded artifact SHA-256 mismatch".to_string());
        }
        let document: LedgerDocument = serde_json::from_slice(LEDGER_BYTES)
            .map_err(|error| format!("invalid embedded opened-set ledger: {error}"))?;
        if document.schema != LEDGER_SCHEMA {
            return Err("unexpected opened-set ledger schema".to_string());
        }
        let sources = document
            .sources
            .iter()
            .map(|source| (source.source_id.as_str(), source.manifest_sha256.as_str()))
            .collect::<BTreeMap<_, _>>();
        if sources.len() != 2
            || sources.get("b546_opened_18") != Some(&B546_SHA256)
            || sources.get("b548_opened_12") != Some(&B548_SHA256)
            || document.sources.iter().any(|source| {
                !matches!(
                    (source.source_id.as_str(), source.manifest_path.as_str()),
                    ("b546_opened_18", B546_PATH) | ("b548_opened_12", B548_PATH)
                )
            })
        {
            return Err("opened-set source identity mismatch".to_string());
        }
        let source_entries = BTreeMap::from([
            (
                "b546_opened_18",
                parse_source_manifest(B546_MANIFEST_BYTES)?,
            ),
            (
                "b548_opened_12",
                parse_source_manifest(B548_MANIFEST_BYTES)?,
            ),
        ]);
        let expected_ids = source_entries
            .values()
            .flat_map(|entries| entries.keys().cloned())
            .collect::<BTreeSet<_>>();
        let mut ids = BTreeSet::new();
        for entry in &document.entries {
            if entry.performance_id.is_empty()
                || !ids.insert(entry.performance_id.clone())
                || entry.sources.is_empty()
                || entry
                    .sources
                    .iter()
                    .any(|source| !sources.contains_key(source.as_str()))
            {
                return Err("invalid opened-set ledger entry".to_string());
            }
            let unique_sources = entry.sources.iter().collect::<BTreeSet<_>>();
            if unique_sources.len() != entry.sources.len() {
                return Err("duplicate source in opened-set ledger entry".to_string());
            }
            let disposition_is_valid = matches!(
                (entry.source_split, entry.r#use),
                (SourceSplit::Validation, OpenedUse::DevelopmentRequired)
                    | (SourceSplit::Test, OpenedUse::DiagnosticOnly)
            );
            if !disposition_is_valid || !entry.fresh_holdout_excluded {
                return Err("invalid opened-set split/use disposition".to_string());
            }
            let expected_sources = source_entries
                .iter()
                .filter(|(_, entries)| entries.contains_key(&entry.performance_id))
                .map(|(source, _)| *source)
                .collect::<BTreeSet<_>>();
            let actual_sources = entry
                .sources
                .iter()
                .map(String::as_str)
                .collect::<BTreeSet<_>>();
            let expected_split = source_entries
                .values()
                .filter_map(|entries| entries.get(&entry.performance_id).copied())
                .collect::<BTreeSet<_>>();
            if actual_sources != expected_sources
                || expected_split.len() != 1
                || !expected_split.contains(&entry.source_split)
            {
                return Err("opened-set ledger differs from its source manifests".to_string());
            }
        }
        if ids != expected_ids || ids.len() != 24 {
            return Err(format!(
                "opened-set ledger must contain exactly 24 unique IDs, got {}",
                ids.len()
            ));
        }
        let forced = document
            .entries
            .iter()
            .filter(|entry| entry.r#use == OpenedUse::DevelopmentRequired)
            .count();
        if forced != 6 {
            return Err(format!(
                "opened validation ledger must force exactly 6 IDs, got {forced}"
            ));
        }
        Ok(Self {
            sha256: sha256_bytes(LEDGER_BYTES),
            isolation_incident_sha256: sha256_bytes(ISOLATION_INCIDENT_BYTES),
            fresh_holdout_isolation_breached: true,
            sources: document.sources,
            entries: document.entries,
            ids,
        })
    }

    pub(crate) fn opened_use(&self, performance_id: &str) -> Option<OpenedUse> {
        self.entries
            .iter()
            .find(|entry| entry.performance_id == performance_id)
            .map(|entry| entry.r#use)
    }

    pub(crate) fn len(&self) -> usize {
        self.ids.len()
    }
}

fn parse_source_manifest(bytes: &[u8]) -> Result<BTreeMap<String, SourceSplit>, String> {
    let text = std::str::from_utf8(bytes)
        .map_err(|error| format!("opened source manifest UTF-8: {error}"))?;
    let mut lines = text.lines();
    if lines.next().map(|line| line.trim_end_matches('\r')) != Some(SOURCE_HEADER) {
        return Err("unexpected opened source manifest header".to_string());
    }
    let mut entries = BTreeMap::new();
    for (index, raw_line) in lines.enumerate() {
        let fields = parse_csv_line(raw_line.trim_end_matches('\r'))
            .map_err(|error| format!("opened source row {}: {error}", index + 2))?;
        if fields.len() != 12 || fields[2].is_empty() {
            return Err(format!("invalid opened source row {}", index + 2));
        }
        let split = match fields[8].as_str() {
            "validation" => SourceSplit::Validation,
            "test" => SourceSplit::Test,
            value => return Err(format!("invalid opened source split: {value}")),
        };
        if entries.insert(fields[2].clone(), split).is_some() {
            return Err(format!("duplicate opened source ID: {}", fields[2]));
        }
    }
    if entries.is_empty() {
        return Err("opened source manifest is empty".to_string());
    }
    Ok(entries)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn embedded_ledger_is_exact_union_of_opened_sets() {
        let ledger = OpenedLedger::embedded().unwrap();
        assert_eq!(ledger.sha256, LEDGER_SHA256);
        assert_eq!(ledger.isolation_incident_sha256, ISOLATION_INCIDENT_SHA256);
        assert!(ledger.fresh_holdout_isolation_breached);
        assert_eq!(ledger.len(), 24);
        assert_eq!(ledger.sources.len(), 2);
        assert!(ledger.opened_use("drummer1/eval_session/1").is_some());
        assert!(ledger.opened_use("drummer7/session3/5").is_some());
        assert!(ledger.opened_use("drummer9/session1/12").is_some());
        let overlap = ledger
            .entries
            .iter()
            .filter(|entry| entry.sources.len() == 2)
            .count();
        assert_eq!(overlap, 6);
        assert_eq!(
            ledger.opened_use("drummer1/session1/16"),
            Some(OpenedUse::DevelopmentRequired)
        );
        assert_eq!(
            ledger.opened_use("drummer1/session1/189"),
            Some(OpenedUse::DiagnosticOnly)
        );
    }
}
