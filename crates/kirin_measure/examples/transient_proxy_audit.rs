//! Candidate-blind acoustic audit for the ATTACK DRUM MIDI proxy.
//!
//! Preparation reads either a pinned DRUM development selection plus only its
//! referenced train/validation MIDI, or a synthetic fixture. It never opens
//! audio, E-GMD test/fresh holdout material, 2MIX material, or candidate output.

use std::process;

#[path = "transient_proxy_audit/annotation.rs"]
mod annotation;
#[path = "transient_proxy_audit/contract.rs"]
mod contract;
#[path = "transient_proxy_audit/csv.rs"]
mod csv;
#[path = "transient_drum_midi/mod.rs"]
mod drum_midi;
#[path = "transient_proxy_audit/io.rs"]
mod io;
#[path = "transient_proxy_audit/manifest.rs"]
mod manifest;
#[path = "transient_proxy_audit/matching.rs"]
mod matching;
#[path = "transient_proxy_audit/midi.rs"]
mod midi;
#[path = "transient_proxy_audit/plan.rs"]
mod plan;
#[path = "transient_proxy_audit/score.rs"]
mod score;

use contract::{Cli, Command, PrepareSource};

fn main() {
    let cli = Cli::parse_env().unwrap_or_else(|error| fail(2, error));
    match cli.command {
        Command::Prepare(command) => {
            let audit_plan = match command.source {
                PrepareSource::Development {
                    selection,
                    midi_root,
                } => {
                    if !plan::formal_development_audit_enabled() {
                        fail(2, plan::FORMAL_DEVELOPMENT_BLOCKER.to_string());
                    }
                    let source =
                        manifest::read_development_selection(&selection, &command.source_sha256)
                            .unwrap_or_else(|error| fail(2, error));
                    plan::build_plan(&source, Some(&midi_root))
                        .unwrap_or_else(|error| fail(1, error))
                }
                PrepareSource::Synthetic { fixture } => {
                    let source = manifest::read_synthetic_fixture(&fixture, &command.source_sha256)
                        .unwrap_or_else(|error| fail(2, error));
                    plan::build_plan(&source, None).unwrap_or_else(|error| fail(1, error))
                }
            };
            let plan_bytes = plan::render_plan(&audit_plan).unwrap_or_else(|error| fail(1, error));
            let plan_sha256 = io::sha256_bytes(&plan_bytes);
            let annotator_a = annotation::render_template(&audit_plan, &plan_sha256, "A");
            let annotator_b = annotation::render_template(&audit_plan, &plan_sha256, "B");
            io::write_bundle_create_new(&[
                (&command.plan_output, &plan_bytes),
                (&command.annotator_a_output, annotator_a.as_bytes()),
                (&command.annotator_b_output, annotator_b.as_bytes()),
            ])
            .unwrap_or_else(|error| fail(1, error));
            println!(
                "ATTACK DRUM MIDI proxy audit prepared: items={} duration=600.000s templates=not_ready plan_sha256={} plan={}",
                audit_plan.items.len(),
                plan_sha256,
                command.plan_output.display(),
            );
        }
        Command::Score(command) => {
            let plan = plan::read_plan(&command.plan).unwrap_or_else(|error| fail(2, error));
            let annotator_a = annotation::read_annotation(
                &command.annotator_a,
                &plan.plan,
                &plan.raw_sha256,
                "A",
            )
            .unwrap_or_else(|error| fail(2, error));
            let annotator_b = annotation::read_annotation(
                &command.annotator_b,
                &plan.plan,
                &plan.raw_sha256,
                "B",
            )
            .unwrap_or_else(|error| fail(2, error));
            let result = score::score(&plan, &annotator_a, &annotator_b);
            let result_bytes = score::render_result(&result).unwrap_or_else(|error| fail(1, error));
            io::write_new_atomic(&command.result_output, &result_bytes)
                .unwrap_or_else(|error| fail(1, error));
            println!(
                "ATTACK DRUM MIDI proxy audit scored: status={} result_sha256={} result={}",
                result.status,
                io::sha256_bytes(&result_bytes),
                command.result_output.display(),
            );
        }
    }
}

fn fail(code: i32, message: String) -> ! {
    eprintln!("ATTACK DRUM MIDI proxy audit error: {message}");
    process::exit(code);
}
