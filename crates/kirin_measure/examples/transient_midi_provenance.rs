//! B-551 ATTACK DRUM MIDI archive-member provenance verifier.
//!
//! This tool verifies the frozen B-550 development selection against one
//! pinned E-GMD MIDI-only archive. It never authorizes candidate scoring.

use std::process;

use sha2::{Digest, Sha256};

#[path = "transient_midi_provenance/archive.rs"]
mod archive;
#[path = "transient_midi_provenance/canonical.rs"]
mod canonical;
#[path = "transient_midi_provenance/contract.rs"]
mod contract;
#[allow(dead_code)] // The shared module also exports the development writer helper.
#[path = "transient_development_contract/csv.rs"]
mod csv;
#[path = "transient_drum_excerpt/mod.rs"]
mod drum_excerpt;
#[path = "transient_drum_midi/mod.rs"]
mod drum_midi;
#[path = "transient_midi_provenance/manifest.rs"]
mod manifest;
#[path = "transient_midi_provenance/receipt.rs"]
mod receipt;
#[path = "transient_midi_provenance/run.rs"]
mod run;

fn main() {
    let cli = contract::Cli::parse_env().unwrap_or_else(|error| fail(2, error));
    let completed = run::execute(&cli).unwrap_or_else(|error| fail(1, error));
    println!(
        "ATTACK DRUM MIDI provenance complete: members={} bytes={} raw_notes={} events={} result={} digest={}",
        completed.members,
        completed.member_bytes,
        completed.excerpt_raw_notes,
        completed.excerpt_events,
        cli.result.display(),
        completed.receipt_sha256,
    );
}

pub(crate) fn sha256_bytes(bytes: &[u8]) -> String {
    hex::encode(Sha256::digest(bytes))
}

fn fail(code: i32, message: String) -> ! {
    eprintln!("ATTACK DRUM MIDI provenance error: {message}");
    process::exit(code);
}
