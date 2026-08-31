use std::fs;

use tempfile::tempdir;

use super::*;

#[test]
fn synthetic_receipt_is_byte_identical_and_never_authorizes_scoring() {
    let parent = parent();
    let mut members = members();
    let first = render_receipt(input(&parent, &members)).unwrap();
    members.reverse();
    let second = render_receipt(input(&parent, &members)).unwrap();
    assert_eq!(first, second);
    assert!(first.ends_with(b"\n"));

    let value: serde_json::Value = serde_json::from_slice(&first).unwrap();
    assert_eq!(value["schema"], OUTPUT_SCHEMA);
    assert_eq!(value["authorization"]["component_verified"], true);
    assert_eq!(
        value["authorization"]["full_layout_ledger_source_pinned"],
        true
    );
    assert_eq!(
        value["authorization"]["overall_formal_authorization"],
        false
    );
    assert_eq!(value["authorization"]["formal_scoring_allowed"], false);
    assert_eq!(value["authorization"]["context_evaluator_ready"], false);
    assert_eq!(value["midi_binding_audit"]["status"], "pass");
    assert_eq!(
        value["duplicate_audit"]["raw_wav_members"]["status"],
        "pass"
    );
    assert_eq!(value["silence_audit"]["status"], "pass");
    assert!(!blockers(&value).contains(&"full_archive_layout_ledger_not_source_pinned"));
    assert_eq!(
        value["members"].as_array().unwrap().len(),
        PINNED_MANIFEST_ROWS
    );
    assert_eq!(
        value["isolation"]["evidence_class"],
        "operational_assertions_not_evidence"
    );
    let text = std::str::from_utf8(&first).unwrap();
    for forbidden in ["timestamp", "created_at", "hostname", "/Users/"] {
        assert!(!text.contains(forbidden), "receipt contains {forbidden}");
    }
}

#[test]
fn real_egmd_filename_shape_is_accepted() {
    let parent = parent();
    let mut evidence = members();
    let midi = "drummer1/session1/1_funk_80_beat_4-4.midi";
    let audio = "drummer1/session1/1_funk_80_beat_4-4.wav";
    evidence[0].manifest_midi_filename = midi.to_string();
    evidence[0].manifest_audio_filename = audio.to_string();
    evidence[0].bundled_midi.archive_member_name = format!("{ARCHIVE_MEMBER_PREFIX}{midi}");
    evidence[0].archive_member_name = format!("{ARCHIVE_MEMBER_PREFIX}{audio}");
    assert!(render_receipt(input(&parent, &evidence)).is_ok());
}

#[test]
fn publish_is_atomic_create_new_and_never_overwrites() {
    let directory = tempdir().unwrap();
    let result = directory.path().join("receipt.json");
    let bytes = b"deterministic audio receipt\n";
    assert_eq!(
        publish_receipt_create_new(&result, bytes).unwrap(),
        sha256_bytes(bytes)
    );
    assert_eq!(fs::read(&result).unwrap(), bytes);
    let error = publish_receipt_create_new(&result, b"replacement\n").unwrap_err();
    assert!(error.contains("receipt already exists"), "{error}");
    assert_eq!(fs::read(&result).unwrap(), bytes);
}

#[test]
fn duplicate_and_constant_audio_are_recorded_without_replacing_ids() {
    let parent = parent();
    let mut evidence = members();
    evidence[1].member_sha256 = evidence[0].member_sha256.clone();
    evidence[2].core_pcm.statistics = constant_statistics(evidence[2].core_pcm.samples, 7 * 256);
    evidence[3].source_pcm.statistics = constant_statistics(evidence[3].source_pcm.samples, 0);
    evidence[3].guard_pcm.statistics = evidence[3].source_pcm.statistics;
    let bytes = render_receipt(input(&parent, &evidence)).unwrap();
    let value: serde_json::Value = serde_json::from_slice(&bytes).unwrap();
    assert_eq!(value["authorization"]["component_verified"], false);
    assert_eq!(
        value["members"].as_array().unwrap().len(),
        PINNED_MANIFEST_ROWS
    );
    assert_eq!(
        value["duplicate_audit"]["raw_wav_members"]["status"],
        "fail"
    );
    assert_eq!(value["silence_audit"]["status"], "fail");
    assert_eq!(
        value["silence_audit"]["source_all_zero_performance_ids"]
            .as_array()
            .unwrap()
            .len(),
        1
    );
    assert!(blockers(&value).contains(&"audio_duplicate_audit_failed"));
    assert!(blockers(&value).contains(&"audio_silence_or_constant_audit_failed"));
}

#[test]
fn bundled_midi_mismatch_and_out_of_bounds_annotation_are_audited() {
    let parent = parent();
    let mut evidence = members();
    evidence[0].bundled_midi.member_sha256 = sha256_bytes(b"wrong bundled MIDI");
    evidence[1].bundled_midi.last_note_micros = u64::MAX;
    evidence[1].bundled_midi.annotation_bounds_pass = false;
    let bytes = render_receipt(input(&parent, &evidence)).unwrap();
    let value: serde_json::Value = serde_json::from_slice(&bytes).unwrap();
    assert_eq!(value["authorization"]["component_verified"], false);
    assert_eq!(value["midi_binding_audit"]["status"], "fail");
    assert!(blockers(&value).contains(&"bundled_midi_b551_raw_sha_binding_failed"));
    assert!(blockers(&value).contains(&"bundled_midi_audio_annotation_bounds_audit_failed"));
}

#[test]
fn structural_parent_duration_and_guard_fail_closed() {
    let mut bad_parent = parent();
    bad_parent.midi_receipt_sha256 = sha256_bytes(b"untrusted");
    let evidence = members();
    assert!(render_receipt(input(&bad_parent, &evidence))
        .unwrap_err()
        .contains("parent chain"));

    let parent = parent();
    let mut duration = members();
    duration[0].decode.actual_samples =
        duration[0].declared_source_samples_44100 + DURATION_TOLERANCE_SAMPLES + 1;
    duration[0].source_pcm.end_sample_44100 = duration[0].decode.actual_samples;
    duration[0].source_pcm.samples = duration[0].decode.actual_samples;
    duration[0].guard_pcm = duration[0].source_pcm.clone();
    duration[0].guard_pcm.canonical_sha256 = sha256_bytes(b"duration guard");
    assert!(render_receipt(input(&parent, &duration))
        .unwrap_err()
        .contains("decode evidence"));

    let mut guard = members();
    guard[0].guard_pcm.start_sample_44100 = 1;
    assert!(render_receipt(input(&parent, &guard))
        .unwrap_err()
        .contains("guard PCM"));

    let mut identity = members();
    identity[0].session = "drummer2/session1".to_string();
    assert!(render_receipt(input(&parent, &identity))
        .unwrap_err()
        .contains("audio member evidence"));
}

#[test]
fn full_archive_layout_ledger_drift_fails_closed() {
    let parent = parent();
    let evidence = members();
    let mut receipt_input = input(&parent, &evidence);
    receipt_input.archive.full_layout_ledger_sha256 = sha256_bytes(b"different layout");
    let error = render_receipt(receipt_input).unwrap_err();
    assert!(error.contains("archive evidence"), "{error}");
}

#[test]
fn odd_pcm24_member_requires_the_riff_pad_byte() {
    let parent = parent();
    let mut evidence = members();
    let member = &mut evidence[101];
    let actual = member.decode.actual_samples;
    assert_eq!(actual % 2, 1, "fixture needs an odd 24-bit data length");
    member.decode.bits_per_sample = 24;
    member.uncompressed_bytes = 44 + actual * 3;
    let error = render_receipt(input(&parent, &evidence)).unwrap_err();
    assert!(error.contains("decode evidence"), "{error}");
    evidence[101].uncompressed_bytes += 1;
    let error = render_receipt(input(&parent, &evidence)).unwrap_err();
    assert!(error.contains("aggregate"), "{error}");
}

fn blockers(value: &serde_json::Value) -> Vec<&str> {
    value["downstream_blockers"]
        .as_array()
        .unwrap()
        .iter()
        .map(|item| item.as_str().unwrap())
        .collect()
}

fn input<'a>(parent: &'a ParentEvidence, members: &'a [MemberEvidence]) -> ReceiptInput<'a> {
    ReceiptInput {
        parent,
        archive: ArchiveReceiptInput {
            archive_sha256: PINNED_AUDIO_ARCHIVE_SHA256.to_string(),
            archive_bytes: PINNED_AUDIO_ARCHIVE_BYTES,
            central_directory_entries: PINNED_ARCHIVE_ENTRIES,
            central_directory_offset: PINNED_CENTRAL_OFFSET,
            central_directory_size: PINNED_CENTRAL_SIZE,
            authenticated_local_header_count: PINNED_ARCHIVE_ENTRIES,
            overlapping_payload_ranges: 0,
            full_layout_ledger_sha256: PINNED_FULL_LAYOUT_LEDGER_SHA256.to_string(),
            selected_audio_member_count: PINNED_MANIFEST_ROWS,
            selected_audio_uncompressed_bytes: PINNED_AUDIO_SELECTED_UNCOMPRESSED_BYTES,
            selected_audio_compressed_bytes: PINNED_AUDIO_SELECTED_COMPRESSED_BYTES,
            selected_audio_ledger_sha256: PINNED_AUDIO_SELECTED_LEDGER_SHA256.to_string(),
            selected_midi_member_count: PINNED_MANIFEST_ROWS,
            selected_midi_uncompressed_bytes: PINNED_MIDI_SELECTED_UNCOMPRESSED_BYTES,
            selected_midi_compressed_bytes: PINNED_MIDI_SELECTED_COMPRESSED_BYTES,
            selected_midi_ledger_sha256: PINNED_MIDI_SELECTED_LEDGER_SHA256.to_string(),
            member_name_prefix: ARCHIVE_MEMBER_PREFIX.to_string(),
        },
        members,
    }
}

fn parent() -> ParentEvidence {
    ParentEvidence {
        development_receipt_sha256: PINNED_DEVELOPMENT_RECEIPT_SHA256.to_string(),
        development_manifest_sha256: PINNED_MANIFEST_SHA256.to_string(),
        development_folds_sha256: PINNED_FOLDS_SHA256.to_string(),
        midi_receipt_sha256: PINNED_MIDI_RECEIPT_SHA256.to_string(),
        midi_archive_sha256: PINNED_MIDI_ARCHIVE_SHA256.to_string(),
    }
}

fn members() -> Vec<MemberEvidence> {
    (0..PINNED_MANIFEST_ROWS)
        .map(|index| member(index, PINNED_MANIFEST_ROWS))
        .collect()
}

fn member(index: usize, count: usize) -> MemberEvidence {
    let rank = index + 1;
    let declared = share(PINNED_DECLARED_SOURCE_SAMPLES, index, count);
    let core_samples = share(PINNED_CORE_SAMPLES, index, count);
    let audio_bytes = 44 + 2 * declared;
    let audio_compressed = share(PINNED_AUDIO_SELECTED_COMPRESSED_BYTES, index, count);
    let midi_bytes = share(PINNED_MIDI_SELECTED_UNCOMPRESSED_BYTES, index, count);
    let midi_compressed = share(PINNED_MIDI_SELECTED_COMPRESSED_BYTES, index, count);
    let drummer = format!("drummer{}", index % 10 + 1);
    let session = format!("{drummer}/session{}", index % 20 + 1);
    let performance_id = format!("{session}/{rank}");
    let audio_name = format!("{session}/{rank}.wav");
    let midi_name = format!("{session}/{rank}.midi");
    let midi_sha = sha256_bytes(format!("MIDI-{rank}").as_bytes());
    MemberEvidence {
        selection_rank: rank,
        selection_key: sha256_bytes(format!("selection-{rank}").as_bytes()),
        fold: (index % 5) as u8,
        drummer,
        session,
        performance_id,
        split: if index < PINNED_TRAIN_ROWS {
            DevelopmentSplit::Train
        } else {
            DevelopmentSplit::Validation
        },
        manifest_audio_filename: audio_name.clone(),
        manifest_midi_filename: midi_name.clone(),
        archive_member_name: format!("{ARCHIVE_MEMBER_PREFIX}{audio_name}"),
        declared_source_samples_44100: declared,
        manifest_core_start_sample_44100: 0,
        manifest_core_end_sample_44100: core_samples,
        member_sha256: sha256_bytes(format!("WAV-{rank}").as_bytes()),
        uncompressed_bytes: audio_bytes,
        compressed_bytes: audio_compressed,
        crc32: rank as u32,
        compression: MemberCompression::Deflated,
        decode: DecodeEvidence {
            container: AudioContainer::RiffWave,
            encoding: SampleEncoding::SignedLinearPcmIntegerLittleEndian,
            channels: 1,
            sample_rate_hz: 44_100,
            bits_per_sample: 16,
            actual_samples: declared,
        },
        source_pcm: region(
            0,
            declared,
            sha256_bytes(format!("source-{rank}").as_bytes()),
        ),
        core_pcm: region(
            0,
            core_samples,
            sha256_bytes(format!("core-{rank}").as_bytes()),
        ),
        guard_pcm: region(
            0,
            declared,
            sha256_bytes(format!("guard-{rank}").as_bytes()),
        ),
        bundled_midi: BundledMidiEvidence {
            archive_member_name: format!("{ARCHIVE_MEMBER_PREFIX}{midi_name}"),
            b551_member_sha256: midi_sha.clone(),
            member_sha256: midi_sha,
            uncompressed_bytes: midi_bytes,
            compressed_bytes: midi_compressed,
            crc32: (rank as u32).wrapping_mul(17),
            compression: MemberCompression::Deflated,
            first_note_micros: 0,
            last_note_micros: 1_000,
            annotation_bounds_pass: true,
        },
    }
}

fn region(start: u64, samples: u64, digest: String) -> PcmRegionEvidence {
    PcmRegionEvidence {
        start_sample_44100: start,
        end_sample_44100: start + samples,
        samples,
        canonical_sha256: digest,
        statistics: PcmStatistics {
            zero_samples: samples - 2,
            minimum_pcm24: -256,
            maximum_pcm24: 256,
            peak_abs_pcm24: 256,
            sum_squares_pcm24: 2 * 65_536,
        },
    }
}

fn constant_statistics(samples: u64, value: i32) -> PcmStatistics {
    PcmStatistics {
        zero_samples: if value == 0 { samples } else { 0 },
        minimum_pcm24: value,
        maximum_pcm24: value,
        peak_abs_pcm24: value.unsigned_abs(),
        sum_squares_pcm24: u128::from(samples) * u128::from(value.unsigned_abs()).pow(2),
    }
}

fn share(total: u64, index: usize, count: usize) -> u64 {
    total / count as u64 + u64::from((index as u64) < total % count as u64)
}
