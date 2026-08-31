//! Reproducible DRUM development-set preparation for ATTACK Phase 2-R.
//!
//! This tool reads only E-GMD train/validation metadata and MIDI. It never
//! opens audio, test MIDI, fresh holdout data, 2MIX data, or candidate scores.

use std::process;

#[path = "transient_development_contract/contract.rs"]
mod contract;
#[path = "transient_development_contract/csv.rs"]
mod csv;
#[path = "transient_drum_excerpt/mod.rs"]
mod drum_excerpt;
#[path = "transient_drum_midi/mod.rs"]
mod drum_midi;
#[path = "transient_development_contract/folds.rs"]
mod folds;
#[path = "transient_development_contract/ledger.rs"]
mod ledger;
#[path = "transient_development_contract/metadata.rs"]
mod metadata;
#[path = "transient_development_contract/midi.rs"]
mod midi;
#[path = "transient_development_contract/output.rs"]
mod output;
#[path = "transient_development_contract/output_csv.rs"]
mod output_csv;
#[path = "transient_development_contract/output_receipt.rs"]
mod output_receipt;
#[path = "transient_development_contract/output_stats.rs"]
mod output_stats;
#[path = "transient_development_contract/policy.rs"]
mod policy;
#[path = "transient_development_contract/publish.rs"]
mod publish;
#[path = "transient_development_contract/selector.rs"]
mod selector;

use contract::{verify_input_identity, Cli};
use folds::assign_grouped_folds;
use ledger::OpenedLedger;
use metadata::{enrich_performances, read_official_metadata};
use output::render_artifacts;
use policy::candidate_evaluation_gate;
use publish::publish_artifacts_create_new;
use selector::select_development;

fn main() {
    let cli = Cli::parse_env().unwrap_or_else(|error| fail(2, error));
    let identities = verify_input_identity(&cli).unwrap_or_else(|error| fail(2, error));
    let ledger = OpenedLedger::embedded().unwrap_or_else(|error| fail(2, error));
    let metadata =
        read_official_metadata(&cli.metadata, &ledger).unwrap_or_else(|error| fail(2, error));
    let enriched = enrich_performances(metadata.performances, &cli.midi_root)
        .unwrap_or_else(|error| fail(2, error));
    let selection = select_development(&enriched.performances, &metadata.available_drummers)
        .unwrap_or_else(|error| fail(1, error));
    candidate_evaluation_gate(&selection.assessment).expect_err(
        "unattached provenance, audio, fold, and acoustic gates must forbid evaluation",
    );
    let folds = assign_grouped_folds(&selection.selected).unwrap_or_else(|error| fail(1, error));
    let artifacts = render_artifacts(
        &selection,
        &folds,
        &metadata.stats,
        &metadata.available_drummers,
        &ledger,
        &identities,
        &enriched.exclusions,
    )
    .unwrap_or_else(|error| fail(1, error));
    let paths = publish_artifacts_create_new(&cli.output_dir, &artifacts)
        .unwrap_or_else(|error| fail(1, error));

    println!(
        "ATTACK DRUM development prepared: selection_status={} fold_status={} fold_qualified={} fold_deficits={} selected={} reserve={} duration={:.3}s beat={} fill={} kick_only={} hat_only={} folds=5 winner_allowed=false output={}",
        selection.assessment.status_code(),
        folds.qualification.status,
        folds.qualification.qualified,
        folds.qualification.deficits.len(),
        selection.selected.len(),
        selection.reserve.len(),
        selection.assessment.unique_duration_secs,
        selection.assessment.beat_ids,
        selection.assessment.fill_ids,
        selection.assessment.kick_only_events,
        selection.assessment.hat_only_events,
        paths.receipt.display(),
    );
}

fn fail(code: i32, message: String) -> ! {
    eprintln!("ATTACK DRUM development error: {message}");
    process::exit(code);
}
