use crate::archive::{verify_pinned_archive, ArchiveVerification, VerifiedMember};
use crate::canonical::digest_contract;
use crate::contract::{read_manifest_bytes, verify_development_receipt, Cli, ParentEvidence};
use crate::drum_excerpt::EXCERPT_SAMPLE_RATE;
use crate::drum_midi::{excerpt_drum_midi, parse_drum_midi, ParsedDrumMidi};
use crate::manifest::{parse_pinned_manifest, ManifestRow, VerifiedManifest};
use crate::receipt::{
    publish_receipt_create_new, render_receipt, ArchiveReceiptInput, CanonicalEvidence,
    DevelopmentSplit, EventCounts, ExcerptEvidence, MemberCompression, MemberEvidence,
    ReceiptInput,
};
use crate::sha256_bytes;

const ARCHIVE_MEMBER_PREFIX: &str = "e-gmd-v1.0.0/";
const SOURCE_DURATION_TOLERANCE_MICROS: u64 = 2_000;

#[derive(Clone, Debug)]
pub(crate) struct Completed {
    pub(crate) members: usize,
    pub(crate) member_bytes: u64,
    pub(crate) excerpt_raw_notes: usize,
    pub(crate) excerpt_events: usize,
    pub(crate) receipt_sha256: String,
}

pub(crate) fn execute(cli: &Cli) -> Result<Completed, String> {
    let parent = verify_development_receipt(&cli.development_receipt)?;
    let manifest_bytes = read_manifest_bytes(&cli.manifest)?;
    let manifest = parse_pinned_manifest(&manifest_bytes)?;
    if parent.manifest_sha256 != sha256_bytes(&manifest_bytes) {
        return Err("manifest bytes disagree with the development receipt".to_string());
    }
    let requested = manifest
        .rows
        .iter()
        .map(|row| row.midi_relative_name.clone())
        .collect::<Vec<_>>();
    let archive = verify_pinned_archive(&cli.midi_archive, &requested)?;
    let members = verify_members(&manifest, &archive)?;
    verify_source_aggregate(&members, &parent)?;
    let receipt = render_receipt(ReceiptInput {
        parent: &parent,
        archive: archive_receipt_input(&archive),
        members: &members,
    })?;
    let receipt_sha256 = publish_receipt_create_new(&cli.result, &receipt)?;
    Ok(Completed {
        members: members.len(),
        member_bytes: archive.selected_uncompressed_bytes,
        excerpt_raw_notes: manifest.totals.excerpt_raw_notes,
        excerpt_events: manifest.totals.excerpt_compound_events,
        receipt_sha256,
    })
}

fn verify_members(
    manifest: &VerifiedManifest,
    archive: &ArchiveVerification,
) -> Result<Vec<MemberEvidence>, String> {
    if archive.selected_members.len() != manifest.rows.len() {
        return Err("archive member coverage differs from the manifest".to_string());
    }
    manifest
        .rows
        .iter()
        .zip(&archive.selected_members)
        .map(|(row, member)| verify_member(row, member))
        .collect()
}

fn verify_member(row: &ManifestRow, member: &VerifiedMember) -> Result<MemberEvidence, String> {
    if member.relative_name != row.midi_relative_name
        || member.member_name != format!("{ARCHIVE_MEMBER_PREFIX}{}", row.midi_relative_name)
    {
        return Err(format!(
            "archive member order or identity mismatch at rank {}",
            row.selection_rank
        ));
    }
    let member_sha256 = sha256_bytes(&member.bytes);
    if member_sha256 != row.midi_sha256 {
        return Err(format!(
            "archive MIDI SHA-256 differs from manifest at rank {}",
            row.selection_rank
        ));
    }
    let source = parse_drum_midi(&member.bytes)
        .map_err(|error| format!("MIDI rank {}: {error}", row.selection_rank))?;
    verify_source_time_range(row, &source)?;
    let excerpt = excerpt_drum_midi(
        &source,
        row.excerpt_start_sample_44100,
        row.excerpt_end_sample_44100,
        EXCERPT_SAMPLE_RATE,
    )
    .map_err(|error| format!("MIDI excerpt rank {}: {error}", row.selection_rank))?;
    let source_counts = event_counts(&source);
    let excerpt_counts = event_counts(&excerpt);
    let manifest_counts = EventCounts {
        raw_notes: row.excerpt_raw_notes,
        compound_events: row.excerpt_compound_events,
        kick_only_events: row.excerpt_kick_only_events,
        hat_only_events: row.excerpt_hat_only_events,
    };
    if excerpt_counts != manifest_counts {
        return Err(format!(
            "recomputed excerpt counts differ from manifest at rank {}",
            row.selection_rank
        ));
    }
    let digests = digest_contract(
        &source,
        &excerpt,
        row.excerpt_start_sample_44100,
        row.excerpt_end_sample_44100,
    )?;
    Ok(MemberEvidence {
        selection_rank: row.selection_rank,
        selection_key: row.selection_key.clone(),
        fold: row.fold,
        performance_id: row.performance_id.clone(),
        split: match row.split.as_str() {
            "train" => DevelopmentSplit::Train,
            "validation" => DevelopmentSplit::Validation,
            _ => return Err("manifest split escaped its parser contract".to_string()),
        },
        manifest_midi_filename: row.midi_relative_name.clone(),
        archive_member_name: member.member_name.clone(),
        manifest_midi_sha256: row.midi_sha256.clone(),
        member_sha256,
        uncompressed_bytes: member.uncompressed_size,
        compressed_bytes: member.compressed_size,
        crc32: member.crc32,
        compression: match member.compression {
            "stored" => MemberCompression::Stored,
            "deflated" => MemberCompression::Deflated,
            _ => return Err("archive returned an uncontracted compression method".to_string()),
        },
        source: CanonicalEvidence {
            counts: source_counts,
            notes_sha256: digests.source_notes_sha256,
            events_sha256: digests.source_events_sha256,
        },
        excerpt: ExcerptEvidence {
            start_sample_44100: row.excerpt_start_sample_44100,
            end_sample_44100: row.excerpt_end_sample_44100,
            manifest_counts,
            observed: CanonicalEvidence {
                counts: excerpt_counts,
                notes_sha256: digests.excerpt_notes_sha256,
                events_sha256: digests.excerpt_events_sha256,
            },
        },
    })
}

fn verify_source_time_range(row: &ManifestRow, source: &ParsedDrumMidi) -> Result<(), String> {
    let allowed_end = u128::from(row.source_duration_samples_44100)
        .checked_mul(1_000_000)
        .and_then(|value| {
            value.checked_add(
                u128::from(SOURCE_DURATION_TOLERANCE_MICROS)
                    .checked_mul(u128::from(EXCERPT_SAMPLE_RATE))?,
            )
        })
        .ok_or("source duration range overflow")?;
    if source
        .notes
        .iter()
        .any(|note| u128::from(note.time_micros) * u128::from(EXCERPT_SAMPLE_RATE) > allowed_end)
    {
        return Err(format!(
            "source MIDI note exceeds declared duration tolerance at rank {}",
            row.selection_rank
        ));
    }
    Ok(())
}

fn event_counts(parsed: &ParsedDrumMidi) -> EventCounts {
    EventCounts {
        raw_notes: parsed.raw_notes,
        compound_events: parsed.events.len(),
        kick_only_events: parsed.events.iter().filter(|event| event.kick_only).count(),
        hat_only_events: parsed.events.iter().filter(|event| event.hat_only).count(),
    }
}

fn verify_source_aggregate(
    members: &[MemberEvidence],
    parent: &ParentEvidence,
) -> Result<(), String> {
    let aggregate = members
        .iter()
        .try_fold(EventCounts::default(), |sum, member| {
            checked_add_counts(sum, member.source.counts)
        })?;
    if aggregate.raw_notes != parent.source_raw_notes
        || aggregate.compound_events != parent.source_compound_events
        || aggregate.kick_only_events != parent.source_kick_only_events
        || aggregate.hat_only_events != parent.source_hat_only_events
    {
        return Err("source MIDI aggregate differs from the B-550 receipt".to_string());
    }
    Ok(())
}

fn checked_add_counts(left: EventCounts, right: EventCounts) -> Result<EventCounts, String> {
    Ok(EventCounts {
        raw_notes: left
            .raw_notes
            .checked_add(right.raw_notes)
            .ok_or("raw-note sum overflow")?,
        compound_events: left
            .compound_events
            .checked_add(right.compound_events)
            .ok_or("compound sum overflow")?,
        kick_only_events: left
            .kick_only_events
            .checked_add(right.kick_only_events)
            .ok_or("kick sum overflow")?,
        hat_only_events: left
            .hat_only_events
            .checked_add(right.hat_only_events)
            .ok_or("hat sum overflow")?,
    })
}

fn archive_receipt_input(archive: &ArchiveVerification) -> ArchiveReceiptInput {
    ArchiveReceiptInput {
        archive_sha256: archive.archive_sha256.clone(),
        archive_bytes: archive.archive_bytes,
        central_directory_entries: archive.central_directory_entries,
        selected_uncompressed_bytes: archive.selected_uncompressed_bytes,
        selected_compressed_bytes: archive.selected_compressed_bytes,
        member_name_prefix: ARCHIVE_MEMBER_PREFIX.to_string(),
    }
}

#[cfg(test)]
#[path = "run_tests.rs"]
mod tests;
