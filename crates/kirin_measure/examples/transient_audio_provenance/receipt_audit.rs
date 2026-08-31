use std::collections::{BTreeMap, BTreeSet};

use serde::Serialize;

use super::*;

pub(super) fn validate_parent(parent: &ParentEvidence) -> Result<(), String> {
    let expected = [
        (
            parent.development_receipt_sha256.as_str(),
            PINNED_DEVELOPMENT_RECEIPT_SHA256,
        ),
        (
            parent.development_manifest_sha256.as_str(),
            PINNED_MANIFEST_SHA256,
        ),
        (
            parent.development_folds_sha256.as_str(),
            PINNED_FOLDS_SHA256,
        ),
        (
            parent.midi_receipt_sha256.as_str(),
            PINNED_MIDI_RECEIPT_SHA256,
        ),
        (
            parent.midi_archive_sha256.as_str(),
            PINNED_MIDI_ARCHIVE_SHA256,
        ),
    ];
    if expected.iter().any(|(observed, pinned)| observed != pinned) {
        return Err("audio receipt parent chain differs from source-pinned B-550/B-551".into());
    }
    Ok(())
}

pub(super) fn validate_archive(archive: &ArchiveReceiptInput) -> Result<(), String> {
    if archive.archive_sha256 != PINNED_AUDIO_ARCHIVE_SHA256
        || archive.archive_bytes != PINNED_AUDIO_ARCHIVE_BYTES
        || archive.central_directory_entries != PINNED_ARCHIVE_ENTRIES
        || archive.central_directory_offset != PINNED_CENTRAL_OFFSET
        || archive.central_directory_size != PINNED_CENTRAL_SIZE
        || archive.authenticated_local_header_count != PINNED_ARCHIVE_ENTRIES
        || archive.overlapping_payload_ranges != 0
        || archive.full_layout_ledger_sha256 != PINNED_FULL_LAYOUT_LEDGER_SHA256
        || archive.selected_audio_member_count != PINNED_MANIFEST_ROWS
        || archive.selected_audio_uncompressed_bytes != PINNED_AUDIO_SELECTED_UNCOMPRESSED_BYTES
        || archive.selected_audio_compressed_bytes != PINNED_AUDIO_SELECTED_COMPRESSED_BYTES
        || archive.selected_audio_ledger_sha256 != PINNED_AUDIO_SELECTED_LEDGER_SHA256
        || archive.selected_midi_member_count != PINNED_MANIFEST_ROWS
        || archive.selected_midi_uncompressed_bytes != PINNED_MIDI_SELECTED_UNCOMPRESSED_BYTES
        || archive.selected_midi_compressed_bytes != PINNED_MIDI_SELECTED_COMPRESSED_BYTES
        || archive.selected_midi_ledger_sha256 != PINNED_MIDI_SELECTED_LEDGER_SHA256
        || archive.member_name_prefix != ARCHIVE_MEMBER_PREFIX
    {
        return Err("audio archive evidence differs from the pinned contract".into());
    }
    Ok(())
}

pub(super) fn validate_members(members: &[&MemberEvidence], prefix: &str) -> Result<(), String> {
    if members.len() != PINNED_MANIFEST_ROWS {
        return Err(format!(
            "audio receipt requires exactly {PINNED_MANIFEST_ROWS} members"
        ));
    }
    let (mut ids, mut keys, mut audio_names, mut midi_names, mut archive_names) = (
        BTreeSet::new(),
        BTreeSet::new(),
        BTreeSet::new(),
        BTreeSet::new(),
        BTreeSet::new(),
    );
    let mut folds = [0_usize; 5];
    let mut splits = BTreeMap::new();
    for (index, member) in members.iter().enumerate() {
        validate_member(member, index + 1, prefix)?;
        if !ids.insert(&member.performance_id)
            || !keys.insert(&member.selection_key)
            || !audio_names.insert(&member.manifest_audio_filename)
            || !midi_names.insert(&member.manifest_midi_filename)
            || !archive_names.insert(&member.archive_member_name)
            || !archive_names.insert(&member.bundled_midi.archive_member_name)
        {
            return Err("audio receipt member identity is not one-to-one".into());
        }
        folds[usize::from(member.fold)] += 1;
        *splits.entry(member.split).or_insert(0_usize) += 1;
    }
    if folds != [58; 5]
        || splits.get(&DevelopmentSplit::Train) != Some(&PINNED_TRAIN_ROWS)
        || splits.get(&DevelopmentSplit::Validation) != Some(&PINNED_VALIDATION_ROWS)
    {
        return Err("audio receipt fold or train/validation coverage mismatch".into());
    }
    Ok(())
}

fn validate_member(member: &MemberEvidence, rank: usize, prefix: &str) -> Result<(), String> {
    if member.selection_rank != rank
        || member.selection_key.is_empty()
        || member.fold >= 5
        || member.drummer.is_empty()
        || member.session.is_empty()
        || member.performance_id.is_empty()
        || !valid_member_identity(member)
        || !safe_relative_name(&member.manifest_audio_filename)
        || !member.manifest_audio_filename.ends_with(".wav")
        || !safe_relative_name(&member.manifest_midi_filename)
        || !member.manifest_midi_filename.ends_with(".midi")
        || member.archive_member_name != format!("{prefix}{}", member.manifest_audio_filename)
        || member.bundled_midi.archive_member_name
            != format!("{prefix}{}", member.manifest_midi_filename)
        || member.uncompressed_bytes == 0
        || member.compressed_bytes == 0
        || member.compression != MemberCompression::Deflated
        || member.bundled_midi.uncompressed_bytes == 0
        || member.bundled_midi.compressed_bytes == 0
        || member.bundled_midi.compression != MemberCompression::Deflated
        || member.declared_source_samples_44100 == 0
        || member.manifest_core_start_sample_44100 >= member.manifest_core_end_sample_44100
    {
        return Err(format!(
            "invalid audio member evidence at selection rank {rank}"
        ));
    }
    for (label, digest) in [
        ("selection key", member.selection_key.as_str()),
        ("raw WAV member", member.member_sha256.as_str()),
        ("source PCM", member.source_pcm.canonical_sha256.as_str()),
        ("core PCM", member.core_pcm.canonical_sha256.as_str()),
        ("guard PCM", member.guard_pcm.canonical_sha256.as_str()),
        (
            "B-551 MIDI member",
            member.bundled_midi.b551_member_sha256.as_str(),
        ),
        (
            "bundled MIDI member",
            member.bundled_midi.member_sha256.as_str(),
        ),
    ] {
        require_sha256(digest, label)?;
    }
    validate_decode(member, rank)?;
    validate_regions(member, rank)
}

fn validate_decode(member: &MemberEvidence, rank: usize) -> Result<(), String> {
    let decode = member.decode;
    let data_bytes = decode
        .actual_samples
        .checked_mul(u64::from(decode.bits_per_sample / 8));
    let expected_wav_bytes = data_bytes
        .and_then(|bytes| bytes.checked_add(bytes & 1))
        .and_then(|bytes| bytes.checked_add(44));
    if decode.container != AudioContainer::RiffWave
        || decode.encoding != SampleEncoding::SignedLinearPcmIntegerLittleEndian
        || decode.channels != 1
        || decode.sample_rate_hz != 44_100
        || !matches!(decode.bits_per_sample, 16 | 24)
        || decode.actual_samples == 0
        || expected_wav_bytes != Some(member.uncompressed_bytes)
        || decode
            .actual_samples
            .abs_diff(member.declared_source_samples_44100)
            > DURATION_TOLERANCE_SAMPLES
        || member.manifest_core_end_sample_44100 > decode.actual_samples
    {
        return Err(format!(
            "invalid WAV decode evidence at selection rank {rank}"
        ));
    }
    Ok(())
}

fn validate_regions(member: &MemberEvidence, rank: usize) -> Result<(), String> {
    let actual = member.decode.actual_samples;
    let bits = member.decode.bits_per_sample;
    validate_region(&member.source_pcm, "source", rank, bits)?;
    validate_region(&member.core_pcm, "core", rank, bits)?;
    validate_region(&member.guard_pcm, "guard", rank, bits)?;
    if (
        member.source_pcm.start_sample_44100,
        member.source_pcm.end_sample_44100,
    ) != (0, actual)
        || member.source_pcm.samples != actual
        || (
            member.core_pcm.start_sample_44100,
            member.core_pcm.end_sample_44100,
        ) != (
            member.manifest_core_start_sample_44100,
            member.manifest_core_end_sample_44100,
        )
        || (
            member.guard_pcm.start_sample_44100,
            member.guard_pcm.end_sample_44100,
        ) != (0, actual)
        || member.guard_pcm.samples != actual
        || member.source_pcm.statistics != member.guard_pcm.statistics
        || member.source_pcm.canonical_sha256 == member.guard_pcm.canonical_sha256
    {
        return Err(format!(
            "invalid PCM region binding at selection rank {rank}"
        ));
    }
    Ok(())
}

fn validate_region(
    region: &PcmRegionEvidence,
    label: &str,
    rank: usize,
    bits_per_sample: u16,
) -> Result<(), String> {
    if region.start_sample_44100 >= region.end_sample_44100
        || region.samples != region.end_sample_44100 - region.start_sample_44100
    {
        return Err(format!(
            "invalid {label} PCM interval at selection rank {rank}"
        ));
    }
    validate_statistics(
        region.statistics,
        region.samples,
        label,
        rank,
        bits_per_sample,
    )
}

fn validate_statistics(
    stats: PcmStatistics,
    samples: u64,
    label: &str,
    rank: usize,
    bits_per_sample: u16,
) -> Result<(), String> {
    const MINIMUM: i32 = -8_388_608;
    const MAXIMUM: i32 = 8_388_607;
    if stats.minimum_pcm24 < MINIMUM
        || stats.maximum_pcm24 > MAXIMUM
        || stats.minimum_pcm24 > stats.maximum_pcm24
        || stats.zero_samples > samples
        || (bits_per_sample == 16
            && (stats.minimum_pcm24 % 256 != 0
                || stats.maximum_pcm24 % 256 != 0
                || !stats.peak_abs_pcm24.is_multiple_of(256)
                || !stats.sum_squares_pcm24.is_multiple_of(65_536)))
    {
        return Err(format!(
            "invalid {label} PCM statistics at selection rank {rank}"
        ));
    }
    let minimum_abs = i64::from(stats.minimum_pcm24).unsigned_abs();
    let maximum_abs = i64::from(stats.maximum_pcm24).unsigned_abs();
    let peak = minimum_abs.max(maximum_abs);
    if u64::from(stats.peak_abs_pcm24) != peak {
        return Err(format!("invalid {label} PCM peak at selection rank {rank}"));
    }
    let minimum_energy = if stats.minimum_pcm24 == stats.maximum_pcm24 {
        u128::from(samples) * u128::from(peak).pow(2)
    } else {
        u128::from(minimum_abs).pow(2) + u128::from(maximum_abs).pow(2)
    };
    let maximum_energy = u128::from(samples) * u128::from(peak).pow(2);
    let zero_observed = stats.minimum_pcm24 == 0 || stats.maximum_pcm24 == 0;
    let zero_impossible = stats.minimum_pcm24 > 0 || stats.maximum_pcm24 < 0;
    if stats.sum_squares_pcm24 < minimum_energy
        || stats.sum_squares_pcm24 > maximum_energy
        || (zero_observed && stats.zero_samples == 0)
        || (zero_impossible && stats.zero_samples != 0)
        || (peak == 0 && stats.zero_samples != samples)
        || (peak != 0 && stats.zero_samples == samples)
    {
        return Err(format!(
            "inconsistent {label} PCM statistics at selection rank {rank}"
        ));
    }
    Ok(())
}

#[derive(Serialize)]
pub(super) struct Aggregate {
    members: usize,
    train_members: usize,
    validation_members: usize,
    folds: [usize; 5],
    audio_uncompressed_bytes: u64,
    audio_compressed_bytes: u64,
    midi_uncompressed_bytes: u64,
    midi_compressed_bytes: u64,
    declared_source_samples_44100: u64,
    actual_source_samples_44100: u64,
    core_samples_44100: u64,
    guard_samples_44100: u64,
    exact_duration_members: usize,
    tolerance_duration_members: usize,
    maximum_duration_difference_samples_44100: u64,
    pcm16_members: usize,
    pcm24_members: usize,
    source_pcm: PcmAggregate,
    core_pcm: PcmAggregate,
    guard_pcm: PcmAggregate,
}

#[derive(Default, Serialize)]
struct PcmAggregate {
    samples: u64,
    zero_samples: u64,
    peak_abs_pcm24: u32,
    sum_squares_pcm24: String,
}

pub(super) fn aggregate(members: &[&MemberEvidence]) -> Result<Aggregate, String> {
    let mut value = Aggregate {
        members: members.len(),
        train_members: 0,
        validation_members: 0,
        folds: [0; 5],
        audio_uncompressed_bytes: 0,
        audio_compressed_bytes: 0,
        midi_uncompressed_bytes: 0,
        midi_compressed_bytes: 0,
        declared_source_samples_44100: 0,
        actual_source_samples_44100: 0,
        core_samples_44100: 0,
        guard_samples_44100: 0,
        exact_duration_members: 0,
        tolerance_duration_members: 0,
        maximum_duration_difference_samples_44100: 0,
        pcm16_members: 0,
        pcm24_members: 0,
        source_pcm: PcmAggregate::default(),
        core_pcm: PcmAggregate::default(),
        guard_pcm: PcmAggregate::default(),
    };
    let (mut source_squares, mut core_squares, mut guard_squares) = (0_u128, 0_u128, 0_u128);
    for member in members {
        match member.split {
            DevelopmentSplit::Train => value.train_members += 1,
            DevelopmentSplit::Validation => value.validation_members += 1,
        }
        value.folds[usize::from(member.fold)] += 1;
        add_u64(
            &mut value.audio_uncompressed_bytes,
            member.uncompressed_bytes,
            "WAV bytes",
        )?;
        add_u64(
            &mut value.audio_compressed_bytes,
            member.compressed_bytes,
            "WAV compressed bytes",
        )?;
        add_u64(
            &mut value.midi_uncompressed_bytes,
            member.bundled_midi.uncompressed_bytes,
            "MIDI bytes",
        )?;
        add_u64(
            &mut value.midi_compressed_bytes,
            member.bundled_midi.compressed_bytes,
            "MIDI compressed bytes",
        )?;
        add_u64(
            &mut value.declared_source_samples_44100,
            member.declared_source_samples_44100,
            "declared samples",
        )?;
        add_u64(
            &mut value.actual_source_samples_44100,
            member.decode.actual_samples,
            "actual samples",
        )?;
        add_u64(
            &mut value.core_samples_44100,
            member.core_pcm.samples,
            "core samples",
        )?;
        add_u64(
            &mut value.guard_samples_44100,
            member.guard_pcm.samples,
            "guard samples",
        )?;
        let difference = member
            .decode
            .actual_samples
            .abs_diff(member.declared_source_samples_44100);
        value.exact_duration_members += usize::from(difference == 0);
        value.tolerance_duration_members += usize::from(difference != 0);
        value.maximum_duration_difference_samples_44100 = value
            .maximum_duration_difference_samples_44100
            .max(difference);
        value.pcm16_members += usize::from(member.decode.bits_per_sample == 16);
        value.pcm24_members += usize::from(member.decode.bits_per_sample == 24);
        add_pcm(
            &mut value.source_pcm,
            &mut source_squares,
            &member.source_pcm,
        )?;
        add_pcm(&mut value.core_pcm, &mut core_squares, &member.core_pcm)?;
        add_pcm(&mut value.guard_pcm, &mut guard_squares, &member.guard_pcm)?;
    }
    value.source_pcm.sum_squares_pcm24 = source_squares.to_string();
    value.core_pcm.sum_squares_pcm24 = core_squares.to_string();
    value.guard_pcm.sum_squares_pcm24 = guard_squares.to_string();
    Ok(value)
}

fn add_pcm(
    total: &mut PcmAggregate,
    squares: &mut u128,
    region: &PcmRegionEvidence,
) -> Result<(), String> {
    add_u64(&mut total.samples, region.samples, "PCM samples")?;
    add_u64(
        &mut total.zero_samples,
        region.statistics.zero_samples,
        "zero samples",
    )?;
    total.peak_abs_pcm24 = total.peak_abs_pcm24.max(region.statistics.peak_abs_pcm24);
    *squares = squares
        .checked_add(region.statistics.sum_squares_pcm24)
        .ok_or("PCM sum-of-squares overflow")?;
    Ok(())
}

fn add_u64(total: &mut u64, value: u64, label: &str) -> Result<(), String> {
    *total = total
        .checked_add(value)
        .ok_or_else(|| format!("{label} total overflow"))?;
    Ok(())
}

pub(super) fn validate_aggregate(
    aggregate: &Aggregate,
    archive: &ArchiveReceiptInput,
) -> Result<(), String> {
    if aggregate.members != PINNED_MANIFEST_ROWS
        || aggregate.train_members != PINNED_TRAIN_ROWS
        || aggregate.validation_members != PINNED_VALIDATION_ROWS
        || aggregate.folds != [58; 5]
        || aggregate.audio_uncompressed_bytes != archive.selected_audio_uncompressed_bytes
        || aggregate.audio_compressed_bytes != archive.selected_audio_compressed_bytes
        || aggregate.midi_uncompressed_bytes != archive.selected_midi_uncompressed_bytes
        || aggregate.midi_compressed_bytes != archive.selected_midi_compressed_bytes
        || aggregate.declared_source_samples_44100 != PINNED_DECLARED_SOURCE_SAMPLES
        || aggregate.actual_source_samples_44100 != PINNED_DECLARED_SOURCE_SAMPLES
        || aggregate.core_samples_44100 != PINNED_CORE_SAMPLES
        || aggregate.exact_duration_members != PINNED_MANIFEST_ROWS
        || aggregate.tolerance_duration_members != 0
        || aggregate.maximum_duration_difference_samples_44100 != 0
        || aggregate.source_pcm.samples != aggregate.actual_source_samples_44100
        || aggregate.guard_pcm.samples != aggregate.actual_source_samples_44100
        || aggregate.source_pcm.zero_samples != aggregate.guard_pcm.zero_samples
        || aggregate.source_pcm.peak_abs_pcm24 != aggregate.guard_pcm.peak_abs_pcm24
        || aggregate.source_pcm.sum_squares_pcm24 != aggregate.guard_pcm.sum_squares_pcm24
        || aggregate.pcm16_members != PINNED_MANIFEST_ROWS
        || aggregate.pcm24_members != 0
    {
        return Err("audio receipt aggregate does not bind to manifest/archive evidence".into());
    }
    Ok(())
}

fn require_sha256(value: &str, label: &str) -> Result<(), String> {
    if value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        Ok(())
    } else {
        Err(format!("{label} must be 64 lowercase hexadecimal digits"))
    }
}
