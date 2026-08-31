//! B-552 ATTACK DRUM audio archive-member and PCM provenance verifier.
//!
//! This tool authenticates the official E-GMD audio archive, but only
//! decompresses and semantically decodes the frozen B-550 train/validation
//! selection. It does not authorize candidate scoring.

use std::process;

use sha2::{Digest, Sha256};

#[path = "transient_audio_provenance/archive.rs"]
mod archive;
#[path = "transient_audio_provenance/contract.rs"]
mod contract;
#[allow(dead_code)] // The shared module also exports the development writer helper.
#[path = "transient_development_contract/csv.rs"]
mod csv;
#[allow(dead_code)] // Reuse the exact B-550 verifier without its MIDI CLI/output surface.
#[path = "transient_midi_provenance/contract.rs"]
mod development_contract;
#[path = "transient_drum_excerpt/mod.rs"]
mod drum_excerpt;
#[path = "transient_drum_midi/mod.rs"]
mod drum_midi;
#[path = "transient_midi_provenance/manifest.rs"]
mod manifest;
#[path = "transient_audio_provenance/pcm.rs"]
mod pcm;
#[path = "transient_audio_provenance/receipt.rs"]
mod receipt;
#[path = "transient_audio_provenance/run.rs"]
mod run;
#[path = "transient_audio_provenance/wav.rs"]
mod wav;

fn main() {
    let cli = contract::Cli::parse_env().unwrap_or_else(|error| fail(2, error));
    let completed = run::execute(&cli).unwrap_or_else(|error| fail(1, error));
    println!(
        "ATTACK DRUM audio provenance complete: members={} bytes={} samples={} result={} digest={}",
        completed.members,
        completed.member_bytes,
        completed.source_samples,
        cli.result.display(),
        completed.receipt_sha256,
    );
}

pub(crate) fn sha256_bytes(bytes: &[u8]) -> String {
    hex::encode(Sha256::digest(bytes))
}

fn fail(code: i32, message: String) -> ! {
    eprintln!("ATTACK DRUM audio provenance error: {message}");
    process::exit(code);
}
