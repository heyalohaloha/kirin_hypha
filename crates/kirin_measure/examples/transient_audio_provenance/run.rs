use crate::archive::{process_pinned_archive, ArchiveVerification, VerifiedPair};
use crate::contract::{
    read_manifest_bytes, verify_parent_chain, Cli, VerifiedMidiMember, VerifiedParents,
    ARCHIVE_MEMBER_PREFIX, DURATION_TOLERANCE_SAMPLES, MIDI_RANGE_TOLERANCE_MICROS,
    PINNED_CORE_SAMPLES, PINNED_DECLARED_SOURCE_SAMPLES,
};
use crate::drum_midi::parse_drum_midi;
use crate::manifest::{parse_pinned_manifest, ManifestRow, VerifiedManifest};
use crate::pcm::{full_source_and_guard_evidence, region_evidence};
use crate::receipt::{
    publish_receipt_create_new, render_receipt, ArchiveReceiptInput, AudioContainer,
    BundledMidiEvidence, DecodeEvidence, DevelopmentSplit, MemberCompression, MemberEvidence,
    ParentEvidence, ReceiptInput, SampleEncoding, CORE_PCM_DOMAIN, GUARD_PCM_DOMAIN,
    SOURCE_PCM_DOMAIN,
};
use crate::sha256_bytes;
use crate::wav::decode_mono_integer_pcm;

#[derive(Clone, Debug)]
pub(crate) struct Completed {
    pub(crate) members: usize,
    pub(crate) member_bytes: u64,
    pub(crate) source_samples: u64,
    pub(crate) receipt_sha256: String,
}

pub(crate) fn execute(cli: &Cli) -> Result<Completed, String> {
    let parents = verify_parent_chain(&cli.development_receipt, &cli.midi_receipt)?;
    let manifest_bytes = read_manifest_bytes(&cli.manifest)?;
    let manifest = parse_pinned_manifest(&manifest_bytes)?;
    if parents.development_manifest_sha256 != sha256_bytes(&manifest_bytes) {
        return Err("manifest bytes disagree with the verified B-550 parent".to_string());
    }
    verify_midi_parent_members(&manifest, &parents.midi_members)?;
    let audio_names = manifest
        .rows
        .iter()
        .map(|row| row.audio_relative_name.clone())
        .collect::<Vec<_>>();
    let midi_names = manifest
        .rows
        .iter()
        .map(|row| row.midi_relative_name.clone())
        .collect::<Vec<_>>();
    let mut rank = 0_usize;
    let (archive, members) =
        process_pinned_archive(&cli.audio_archive, &audio_names, &midi_names, |pair| {
            let row = manifest
                .rows
                .get(rank)
                .ok_or("archive produced more selected pairs than the manifest")?;
            let b551 = parents
                .midi_members
                .get(rank)
                .ok_or("B-551 parent member disappeared")?;
            rank += 1;
            verify_member(row, b551, pair)
        })?;
    if rank != manifest.rows.len() || members.len() != manifest.rows.len() {
        return Err("archive selected-pair coverage differs from manifest".to_string());
    }
    verify_aggregates(&members)?;
    let receipt = render_receipt(ReceiptInput {
        parent: &receipt_parent(&parents),
        archive: archive_receipt_input(&archive),
        members: &members,
    })?;
    let receipt_sha256 = publish_receipt_create_new(&cli.result, &receipt)?;
    Ok(Completed {
        members: members.len(),
        member_bytes: archive.audio.uncompressed_bytes,
        source_samples: members
            .iter()
            .map(|member| member.decode.actual_samples)
            .sum(),
        receipt_sha256,
    })
}

fn verify_midi_parent_members(
    manifest: &VerifiedManifest,
    members: &[VerifiedMidiMember],
) -> Result<(), String> {
    if members.len() != manifest.rows.len() {
        return Err("B-551 receipt member coverage differs from B-550 manifest".to_string());
    }
    for (row, member) in manifest.rows.iter().zip(members) {
        if member.selection_rank != row.selection_rank
            || member.performance_id != row.performance_id
            || member.manifest_midi_filename != row.midi_relative_name
            || member.member_sha256 != row.midi_sha256
        {
            return Err(format!(
                "B-551 member chain mismatch at selection rank {}",
                row.selection_rank
            ));
        }
    }
    Ok(())
}

fn verify_member(
    row: &ManifestRow,
    b551: &VerifiedMidiMember,
    pair: VerifiedPair,
) -> Result<MemberEvidence, String> {
    verify_member_names(row, &pair)?;
    let audio_sha256 = sha256_bytes(&pair.audio.bytes);
    let decoded = decode_mono_integer_pcm(&pair.audio.bytes)
        .map_err(|error| format!("WAV rank {}: {error}", row.selection_rank))?;
    let actual_samples = decoded.metadata.sample_count;
    let data_bytes = actual_samples
        .checked_mul(u64::from(decoded.metadata.bits_per_sample / 8))
        .ok_or("decoded WAV byte-length binding overflow")?;
    let expected_bytes = data_bytes
        .checked_add(data_bytes & 1)
        .and_then(|value| value.checked_add(44))
        .ok_or("decoded WAV padded byte-length binding overflow")?;
    if expected_bytes != pair.audio.uncompressed_size
        || actual_samples.abs_diff(row.source_duration_samples_44100) > DURATION_TOLERANCE_SAMPLES
        || row.excerpt_end_sample_44100 > actual_samples
    {
        return Err(format!(
            "decoded WAV duration/core binding failed at rank {}",
            row.selection_rank
        ));
    }
    let (source_pcm, guard_pcm) = full_source_and_guard_evidence(
        &decoded.pcm24,
        SOURCE_PCM_DOMAIN,
        GUARD_PCM_DOMAIN,
        decoded.metadata.sample_rate,
        decoded.metadata.channels,
    )?;
    let core_pcm = region_evidence(
        &decoded.pcm24,
        row.excerpt_start_sample_44100,
        row.excerpt_end_sample_44100,
        CORE_PCM_DOMAIN,
        decoded.metadata.sample_rate,
        decoded.metadata.channels,
    )?;
    let midi_sha256 = sha256_bytes(&pair.midi.bytes);
    if midi_sha256 != row.midi_sha256 || midi_sha256 != b551.member_sha256 {
        return Err(format!(
            "bundled MIDI raw SHA chain failed at rank {}",
            row.selection_rank
        ));
    }
    let midi = parse_drum_midi(&pair.midi.bytes)
        .map_err(|error| format!("bundled MIDI rank {}: {error}", row.selection_rank))?;
    let first_note_micros = midi
        .notes
        .iter()
        .map(|note| note.time_micros)
        .min()
        .ok_or_else(|| {
            format!(
                "bundled MIDI has no raw notes at rank {}",
                row.selection_rank
            )
        })?;
    let last_note_micros = midi
        .notes
        .iter()
        .map(|note| note.time_micros)
        .max()
        .ok_or_else(|| {
            format!(
                "bundled MIDI has no raw notes at rank {}",
                row.selection_rank
            )
        })?;
    let annotation_bounds_pass =
        annotation_bounds_pass(first_note_micros, last_note_micros, actual_samples)?;
    let (drummer, session) = identity_parts(&row.performance_id)?;
    Ok(MemberEvidence {
        selection_rank: row.selection_rank,
        selection_key: row.selection_key.clone(),
        fold: row.fold,
        drummer,
        session,
        performance_id: row.performance_id.clone(),
        split: split(&row.split)?,
        manifest_audio_filename: row.audio_relative_name.clone(),
        manifest_midi_filename: row.midi_relative_name.clone(),
        archive_member_name: pair.audio.member_name,
        declared_source_samples_44100: row.source_duration_samples_44100,
        manifest_core_start_sample_44100: row.excerpt_start_sample_44100,
        manifest_core_end_sample_44100: row.excerpt_end_sample_44100,
        member_sha256: audio_sha256,
        uncompressed_bytes: pair.audio.uncompressed_size,
        compressed_bytes: pair.audio.compressed_size,
        crc32: pair.audio.crc32,
        compression: compression(pair.audio.compression)?,
        decode: DecodeEvidence {
            container: AudioContainer::RiffWave,
            encoding: SampleEncoding::SignedLinearPcmIntegerLittleEndian,
            channels: decoded.metadata.channels,
            sample_rate_hz: decoded.metadata.sample_rate,
            bits_per_sample: decoded.metadata.bits_per_sample,
            actual_samples,
        },
        source_pcm,
        core_pcm,
        guard_pcm,
        bundled_midi: BundledMidiEvidence {
            archive_member_name: pair.midi.member_name,
            b551_member_sha256: b551.member_sha256.clone(),
            member_sha256: midi_sha256,
            uncompressed_bytes: pair.midi.uncompressed_size,
            compressed_bytes: pair.midi.compressed_size,
            crc32: pair.midi.crc32,
            compression: compression(pair.midi.compression)?,
            first_note_micros,
            last_note_micros,
            annotation_bounds_pass,
        },
    })
}

fn verify_member_names(row: &ManifestRow, pair: &VerifiedPair) -> Result<(), String> {
    if pair.audio.relative_name != row.audio_relative_name
        || pair.midi.relative_name != row.midi_relative_name
        || pair.audio.member_name != format!("{ARCHIVE_MEMBER_PREFIX}{}", row.audio_relative_name)
        || pair.midi.member_name != format!("{ARCHIVE_MEMBER_PREFIX}{}", row.midi_relative_name)
    {
        return Err(format!(
            "archive pair identity/order mismatch at rank {}",
            row.selection_rank
        ));
    }
    Ok(())
}

fn annotation_bounds_pass(first: u64, last: u64, actual: u64) -> Result<bool, String> {
    let tolerance = u128::from(MIDI_RANGE_TOLERANCE_MICROS)
        .checked_mul(44_100)
        .ok_or("annotation tolerance overflow")?;
    let audio_end = u128::from(actual)
        .checked_mul(1_000_000)
        .and_then(|value| value.checked_add(tolerance))
        .ok_or("annotation audio range overflow")?;
    let last_scaled = u128::from(last)
        .checked_mul(44_100)
        .ok_or("annotation time range overflow")?;
    Ok(first <= last && last_scaled <= audio_end)
}

fn identity_parts(performance_id: &str) -> Result<(String, String), String> {
    let parts = performance_id.split('/').collect::<Vec<_>>();
    if parts.len() != 3 || parts.iter().any(|part| part.is_empty()) {
        return Err("performance ID does not have drummer/session/id hierarchy".to_string());
    }
    Ok((parts[0].to_string(), format!("{}/{}", parts[0], parts[1])))
}

fn split(value: &str) -> Result<DevelopmentSplit, String> {
    match value {
        "train" => Ok(DevelopmentSplit::Train),
        "validation" => Ok(DevelopmentSplit::Validation),
        _ => Err("manifest split escaped its train/validation contract".to_string()),
    }
}

fn compression(value: &str) -> Result<MemberCompression, String> {
    match value {
        "stored" => Ok(MemberCompression::Stored),
        "deflated" => Ok(MemberCompression::Deflated),
        _ => Err("archive returned an uncontracted compression method".to_string()),
    }
}

fn verify_aggregates(members: &[MemberEvidence]) -> Result<(), String> {
    let source = checked_sum(
        members.iter().map(|member| member.decode.actual_samples),
        "actual source samples",
    )?;
    let declared = checked_sum(
        members
            .iter()
            .map(|member| member.declared_source_samples_44100),
        "declared source samples",
    )?;
    let core = checked_sum(
        members.iter().map(|member| member.core_pcm.samples),
        "core samples",
    )?;
    if source != PINNED_DECLARED_SOURCE_SAMPLES
        || declared != PINNED_DECLARED_SOURCE_SAMPLES
        || core != PINNED_CORE_SAMPLES
    {
        return Err("decoded PCM aggregate differs from the fixed manifest contract".to_string());
    }
    Ok(())
}

fn checked_sum(mut values: impl Iterator<Item = u64>, label: &str) -> Result<u64, String> {
    values.try_fold(0_u64, |sum, value| {
        sum.checked_add(value)
            .ok_or_else(|| format!("{label} aggregate overflow"))
    })
}

fn receipt_parent(parent: &VerifiedParents) -> ParentEvidence {
    ParentEvidence {
        development_receipt_sha256: parent.development_receipt_sha256.clone(),
        development_manifest_sha256: parent.development_manifest_sha256.clone(),
        development_folds_sha256: parent.development_folds_sha256.clone(),
        midi_receipt_sha256: parent.midi_receipt_sha256.clone(),
        midi_archive_sha256: parent.midi_archive_sha256.clone(),
    }
}

fn archive_receipt_input(archive: &ArchiveVerification) -> ArchiveReceiptInput {
    ArchiveReceiptInput {
        archive_sha256: archive.archive_sha256.clone(),
        archive_bytes: archive.archive_bytes,
        central_directory_entries: archive.central_directory_entries,
        central_directory_offset: archive.central_directory_offset,
        central_directory_size: archive.central_directory_size,
        authenticated_local_header_count: archive.authenticated_local_header_count,
        overlapping_payload_ranges: archive.overlapping_payload_ranges,
        full_layout_ledger_sha256: archive.full_layout_ledger_sha256.clone(),
        selected_audio_member_count: archive.audio.members,
        selected_audio_uncompressed_bytes: archive.audio.uncompressed_bytes,
        selected_audio_compressed_bytes: archive.audio.compressed_bytes,
        selected_audio_ledger_sha256: archive.audio.central_ledger_sha256.clone(),
        selected_midi_member_count: archive.midi.members,
        selected_midi_uncompressed_bytes: archive.midi.uncompressed_bytes,
        selected_midi_compressed_bytes: archive.midi.compressed_bytes,
        selected_midi_ledger_sha256: archive.midi.central_ledger_sha256.clone(),
        member_name_prefix: ARCHIVE_MEMBER_PREFIX.to_string(),
    }
}

#[cfg(test)]
#[path = "run_tests.rs"]
mod tests;
