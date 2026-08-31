use std::collections::BTreeSet;

use crate::contract::InputIdentities;
use crate::folds::FoldPlan;
use crate::ledger::OpenedLedger;
use crate::metadata::{MetadataStats, PreflightExclusion};
use crate::output_csv::{
    render_exclusion_shards, render_folds, render_manifest, render_reserve_shards, FOLDS_NAME,
    MANIFEST_NAME, RECEIPT_NAME,
};
use crate::output_receipt::{render_receipt, ArtifactPart};
use crate::selector::SelectionOutcome;

pub(crate) struct ArtifactFile {
    pub(crate) name: String,
    pub(crate) bytes: Vec<u8>,
}

pub(crate) struct Artifacts {
    pub(crate) files: Vec<ArtifactFile>,
}

pub(crate) fn render_artifacts(
    selection: &SelectionOutcome,
    folds: &FoldPlan,
    metadata: &MetadataStats,
    required_drummers: &BTreeSet<String>,
    ledger: &OpenedLedger,
    identities: &InputIdentities,
    exclusions: &[PreflightExclusion],
) -> Result<Artifacts, String> {
    let manifest = render_manifest(&selection.selected, folds)?;
    let folds_csv = render_folds(&selection.selected, folds)?;
    let reserve = render_reserve_shards(&selection.reserve);
    let exclusion_files = render_exclusion_shards(exclusions);
    let exclusion_parts = parts(&exclusion_files);
    let reserve_parts = parts(&reserve);
    let receipt = render_receipt(
        selection,
        folds,
        metadata,
        required_drummers,
        ledger,
        identities,
        &manifest,
        &folds_csv,
        exclusion_parts,
        reserve_parts,
    )?;
    let mut files = vec![
        ArtifactFile {
            name: MANIFEST_NAME.into(),
            bytes: manifest,
        },
        ArtifactFile {
            name: FOLDS_NAME.into(),
            bytes: folds_csv,
        },
    ];
    files.extend(
        exclusion_files
            .into_iter()
            .map(|(name, bytes, _)| ArtifactFile { name, bytes }),
    );
    files.extend(
        reserve
            .into_iter()
            .map(|(name, bytes, _)| ArtifactFile { name, bytes }),
    );
    files.push(ArtifactFile {
        name: RECEIPT_NAME.into(),
        bytes: receipt,
    });
    Ok(Artifacts { files })
}

fn parts(files: &[(String, Vec<u8>, usize)]) -> Vec<ArtifactPart> {
    files
        .iter()
        .map(|(name, bytes, rows)| ArtifactPart {
            path: name.clone(),
            sha256: crate::contract::sha256_bytes(bytes),
            rows: *rows,
        })
        .collect()
}
