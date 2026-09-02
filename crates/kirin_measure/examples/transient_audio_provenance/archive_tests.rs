use std::io::{Cursor, Write};

use tempfile::NamedTempFile;
use zip::write::SimpleFileOptions;
use zip::{CompressionMethod, ZipWriter};

use super::*;
use crate::sha256_bytes;

const AUDIO: &str = "drummer1/session1/1_rock_120_beat_4-4_1.wav";
const MIDI: &str = "drummer1/session1/1_rock_120_beat_4-4_1.midi";

#[test]
fn full_path_hashes_one_file_and_returns_only_authenticated_selected_payloads() {
    let bytes = pair_zip(false);
    let (archive, output) = process_bytes(&bytes, |pair| {
        assert_eq!(pair.audio.bytes, b"audio payload");
        assert_eq!(pair.midi.bytes, b"midi payload");
        Ok(pair.audio.bytes.len() + pair.midi.bytes.len())
    })
    .unwrap();
    assert_eq!(archive.archive_sha256, sha256_bytes(&bytes));
    assert_eq!(archive.central_directory_entries, 2);
    assert_eq!(archive.authenticated_local_header_count, 2);
    assert_eq!(archive.overlapping_payload_ranges, 0);
    assert_eq!(output, [25]);
}

#[test]
fn pinned_summary_rejects_full_layout_ledger_drift() {
    let mut archive = ArchiveVerification {
        archive_sha256: PINNED_AUDIO_ARCHIVE_SHA256.to_string(),
        archive_bytes: PINNED_AUDIO_ARCHIVE_BYTES,
        central_directory_entries: PINNED_ARCHIVE_ENTRIES,
        central_directory_offset: PINNED_CENTRAL_OFFSET,
        central_directory_size: PINNED_CENTRAL_SIZE,
        authenticated_local_header_count: PINNED_ARCHIVE_ENTRIES,
        overlapping_payload_ranges: 0,
        full_layout_ledger_sha256: PINNED_FULL_LAYOUT_LEDGER_SHA256.to_string(),
        audio: SelectionSummary {
            members: PINNED_MANIFEST_ROWS,
            uncompressed_bytes: PINNED_AUDIO_SELECTED_UNCOMPRESSED_BYTES,
            compressed_bytes: PINNED_AUDIO_SELECTED_COMPRESSED_BYTES,
            central_ledger_sha256: PINNED_AUDIO_SELECTED_LEDGER_SHA256.to_string(),
        },
        midi: SelectionSummary {
            members: PINNED_MANIFEST_ROWS,
            uncompressed_bytes: PINNED_MIDI_SELECTED_UNCOMPRESSED_BYTES,
            compressed_bytes: PINNED_MIDI_SELECTED_COMPRESSED_BYTES,
            central_ledger_sha256: PINNED_MIDI_SELECTED_LEDGER_SHA256.to_string(),
        },
    };
    validate_pinned_summary(&archive).unwrap();
    archive.full_layout_ledger_sha256 = sha256_bytes(b"drifted full layout");
    let error = validate_pinned_summary(&archive).unwrap_err();
    assert!(error.contains("source-pinned"), "{error}");
}

#[test]
fn selected_requests_reject_test_unsafe_duplicate_and_wrong_suffix_names() {
    for audio in [
        "drummer1/eval_session/a.wav",
        "drummer1/session1/a:wav.wav",
        "../a.wav",
        "drummer1\\a.wav",
        "drummer1/session1/a.midi",
    ] {
        assert!(catalog::requested_members(&[audio.to_string()], &[MIDI.to_string()]).is_err());
    }
    assert!(catalog::requested_members(
        &[AUDIO.to_string(), AUDIO.to_string()],
        &[MIDI.to_string(), MIDI.to_string()],
    )
    .is_err());
}

#[test]
fn independent_catalog_rejects_exact_canonical_casefold_and_unsafe_names() {
    for names in [
        vec!["same", "same"],
        vec!["same", "same/"],
        vec!["safe/Name", "safe/name"],
        vec!["../escape"],
        vec!["C:relative"],
        vec!["a\\b"],
    ] {
        let central = central_only(&names);
        assert!(
            layout::parse_central_directory(&central, names.len()).is_err(),
            "{names:?}"
        );
    }
}

#[test]
fn zip64_classic_fields_cannot_disagree_with_zip64_values() {
    let mut tail = vec![0_u8; 98];
    tail[0..4].copy_from_slice(b"PK\x06\x06");
    tail[4..12].copy_from_slice(&44_u64.to_le_bytes());
    tail[24..32].copy_from_slice(&1_u64.to_le_bytes());
    tail[32..40].copy_from_slice(&1_u64.to_le_bytes());
    tail[56..60].copy_from_slice(b"PK\x06\x07");
    tail[64..72].copy_from_slice(&0_u64.to_le_bytes());
    tail[72..76].copy_from_slice(&1_u32.to_le_bytes());
    tail[76..80].copy_from_slice(b"PK\x05\x06");
    tail[84..86].copy_from_slice(&u16::MAX.to_le_bytes());
    tail[86..88].copy_from_slice(&u16::MAX.to_le_bytes());
    tail[88..92].copy_from_slice(&1_u32.to_le_bytes());
    let error = layout::parse_end_records(&tail, 0, tail.len() as u64).unwrap_err();
    assert!(error.contains("disagree"), "{error}");
}

#[test]
fn encrypted_unsupported_symlink_and_local_name_mismatch_fail_full_path() {
    let base = pair_zip(false);
    let positions = entry_positions(&base, &full(AUDIO));

    let mut encrypted = base.clone();
    set_u16_or(&mut encrypted, positions.local + 6, 1);
    set_u16_or(&mut encrypted, positions.central + 8, 1);
    assert!(process_bytes(&encrypted, |_| Ok(())).is_err());

    let mut unsupported = base.clone();
    set_u16(&mut unsupported, positions.local + 8, 12);
    set_u16(&mut unsupported, positions.central + 10, 12);
    assert!(process_bytes(&unsupported, |_| Ok(())).is_err());

    let mut mismatched = base;
    mismatched[positions.local + 30] ^= 0x20;
    assert!(process_bytes(&mismatched, |_| Ok(())).is_err());

    let symlink = pair_zip(true);
    assert!(process_bytes(&symlink, |_| Ok(())).is_err());
}

#[test]
fn selected_size_ratio_and_crc_corruption_fail_before_evidence() {
    let base = pair_zip(false);
    let positions = entry_positions(&base, &full(AUDIO));

    let mut oversized = base.clone();
    set_u32(
        &mut oversized,
        positions.local + 22,
        (MAX_MEMBER_BYTES + 1) as u32,
    );
    set_u32(
        &mut oversized,
        positions.central + 24,
        (MAX_MEMBER_BYTES + 1) as u32,
    );
    assert!(process_bytes(&oversized, |_| Ok(())).is_err());

    let compressed = u32::from_le_bytes(
        base[positions.central + 20..positions.central + 24]
            .try_into()
            .unwrap(),
    );
    let ratio_size = compressed.saturating_mul(MAX_COMPRESSION_RATIO as u32 + 1);
    let mut ratio = base.clone();
    set_u32(&mut ratio, positions.local + 22, ratio_size);
    set_u32(&mut ratio, positions.central + 24, ratio_size);
    assert!(process_bytes(&ratio, |_| Ok(())).is_err());

    let mut corrupt = base;
    corrupt[positions.data_start] ^= 0xff;
    assert!(process_bytes(&corrupt, |_| Ok(())).is_err());
}

#[test]
fn selected_aggregate_is_bounded_before_capture_allocation() {
    let selected = (1..=17)
        .map(|rank| SelectedMetadata {
            kind: MemberKind::Audio,
            rank,
            relative_name: format!("d/s/{rank}.wav"),
            entry_index: rank - 1,
            data_start: 30,
            entry: layout::CentralEntry {
                raw_name: format!("{ARCHIVE_MEMBER_PREFIX}d/s/{rank}.wav"),
                flags: 0,
                method: 0,
                crc32: 0,
                compressed_size: MAX_MEMBER_BYTES,
                uncompressed_size: MAX_MEMBER_BYTES,
                local_header_offset: 0,
                unix_mode: Some(0o100_640),
                directory: false,
            },
        })
        .collect::<Vec<_>>();
    let error = selection_summary(&selected, AUDIO_LEDGER_DOMAIN).unwrap_err();
    assert!(error.contains("aggregate"), "{error}");
}

#[test]
fn gap_overlap_and_unanchored_local_layouts_are_rejected() {
    let local = |index, start, data, end| LocalLayout {
        entry_index: index,
        header_start: start,
        data_start: data,
        data_end: end,
    };
    let valid = [local(0, 0, 2, 10), local(1, 10, 12, 20)];
    catalog::validate_gapless_layout(&valid, 20).unwrap();
    assert!(
        catalog::validate_gapless_layout(&[local(0, 0, 2, 9), local(1, 10, 12, 20)], 20).is_err()
    );
    assert!(
        catalog::validate_gapless_layout(&[local(0, 0, 2, 11), local(1, 10, 12, 20)], 20).is_err()
    );
    assert!(
        catalog::validate_gapless_layout(&[local(0, 1, 2, 10), local(1, 10, 12, 20)], 20).is_err()
    );

    let oversized_header = [local(
        0,
        0,
        MAX_LOCAL_HEADER_BYTES + 1,
        MAX_LOCAL_HEADER_BYTES + 2,
    )];
    assert!(catalog::validate_capture_budget(&oversized_header).is_err());
    let many = (0..300)
        .map(|index| {
            let start = index * MAX_LOCAL_HEADER_BYTES;
            local(
                index as usize,
                start,
                start + MAX_LOCAL_HEADER_BYTES,
                start + MAX_LOCAL_HEADER_BYTES,
            )
        })
        .collect::<Vec<_>>();
    assert!(catalog::validate_capture_budget(&many).is_err());
}

#[test]
fn deflate_trailing_compressed_bytes_are_not_silently_consumed() {
    let bytes = pair_zip(false);
    let positions = entry_positions(&bytes, &full(AUDIO));
    let central = central_entries(&bytes);
    let mut entry = central[positions.index].clone();
    let compressed = entry.compressed_size as usize;
    let mut header = bytes[positions.local..positions.data_start].to_vec();
    let mut payload = bytes[positions.data_start..positions.data_start + compressed].to_vec();
    entry.compressed_size += 1;
    set_u32(&mut header, 18, entry.compressed_size as u32);
    payload.push(0);
    let metadata = SelectedMetadata {
        kind: MemberKind::Audio,
        rank: 1,
        relative_name: AUDIO.to_string(),
        entry_index: 0,
        data_start: header.len() as u64,
        entry,
    };
    let error = capture::inflate_member(&metadata, &[header], payload).unwrap_err();
    assert!(
        error.contains("consume")
            || error.contains("checksum")
            || error.contains("inflate")
            || error.contains("finish"),
        "{error}"
    );
}

#[test]
fn captured_local_header_must_match_authenticated_central_metadata() {
    let bytes = pair_zip(false);
    let positions = entry_positions(&bytes, &full(AUDIO));
    let central = central_entries(&bytes);
    let mut header = bytes[positions.local..positions.data_start].to_vec();
    header[14] ^= 1;
    let error =
        layout::verify_captured_local_header(&header, &central[positions.index]).unwrap_err();
    assert!(error.contains("disagrees"), "{error}");
}

fn pair_zip(audio_symlink: bool) -> Vec<u8> {
    let cursor = Cursor::new(Vec::new());
    let mut writer = ZipWriter::new(cursor);
    let options = SimpleFileOptions::default()
        .compression_method(CompressionMethod::Deflated)
        .unix_permissions(0o640);
    if audio_symlink {
        writer.add_symlink(full(AUDIO), "target", options).unwrap();
    } else {
        writer.start_file(full(AUDIO), options).unwrap();
        writer.write_all(b"audio payload").unwrap();
    }
    writer.start_file(full(MIDI), options).unwrap();
    writer.write_all(b"midi payload").unwrap();
    writer.finish().unwrap().into_inner()
}

fn process_bytes<T>(
    bytes: &[u8],
    processor: impl FnMut(VerifiedPair) -> Result<T, String>,
) -> Result<(ArchiveVerification, Vec<T>), String> {
    let mut file = NamedTempFile::new().unwrap();
    file.write_all(bytes).unwrap();
    file.flush().unwrap();
    process_archive(
        file.path(),
        &[AUDIO.to_string()],
        &[MIDI.to_string()],
        ExpectedArchive {
            sha256: &sha256_bytes(bytes),
            bytes: bytes.len() as u64,
            entries: 2,
            pinned_selection: false,
        },
        processor,
    )
}

#[derive(Clone, Copy)]
struct Positions {
    index: usize,
    central: usize,
    local: usize,
    data_start: usize,
}

fn entry_positions(bytes: &[u8], wanted: &str) -> Positions {
    let mut central =
        u32::from_le_bytes(bytes[bytes.len() - 6..bytes.len() - 2].try_into().unwrap()) as usize;
    let count = u16::from_le_bytes(
        bytes[bytes.len() - 12..bytes.len() - 10]
            .try_into()
            .unwrap(),
    ) as usize;
    for index in 0..count {
        let name_len =
            u16::from_le_bytes(bytes[central + 28..central + 30].try_into().unwrap()) as usize;
        let extra_len =
            u16::from_le_bytes(bytes[central + 30..central + 32].try_into().unwrap()) as usize;
        let comment_len =
            u16::from_le_bytes(bytes[central + 32..central + 34].try_into().unwrap()) as usize;
        let name = std::str::from_utf8(&bytes[central + 46..central + 46 + name_len]).unwrap();
        if name == wanted {
            let local =
                u32::from_le_bytes(bytes[central + 42..central + 46].try_into().unwrap()) as usize;
            let local_name =
                u16::from_le_bytes(bytes[local + 26..local + 28].try_into().unwrap()) as usize;
            let local_extra =
                u16::from_le_bytes(bytes[local + 28..local + 30].try_into().unwrap()) as usize;
            return Positions {
                index,
                central,
                local,
                data_start: local + 30 + local_name + local_extra,
            };
        }
        central += 46 + name_len + extra_len + comment_len;
    }
    panic!("missing {wanted}");
}

fn central_entries(bytes: &[u8]) -> Vec<layout::CentralEntry> {
    let region = {
        let mut file = tempfile::tempfile().unwrap();
        file.write_all(bytes).unwrap();
        read_directory_region(&mut file, bytes.len() as u64).unwrap()
    };
    parse_central(&region).unwrap()
}

fn central_only(names: &[&str]) -> Vec<u8> {
    let mut bytes = Vec::new();
    for (index, name) in names.iter().enumerate() {
        let mut fixed = [0_u8; 46];
        fixed[..4].copy_from_slice(b"PK\x01\x02");
        fixed[4..6].copy_from_slice(&0x0314_u16.to_le_bytes());
        fixed[6..8].copy_from_slice(&20_u16.to_le_bytes());
        fixed[28..30].copy_from_slice(&(name.len() as u16).to_le_bytes());
        fixed[42..46].copy_from_slice(&(index as u32).to_le_bytes());
        bytes.extend_from_slice(&fixed);
        bytes.extend_from_slice(name.as_bytes());
    }
    bytes
}

fn full(relative: &str) -> String {
    format!("{ARCHIVE_MEMBER_PREFIX}{relative}")
}

fn set_u16(bytes: &mut [u8], offset: usize, value: u16) {
    bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}

fn set_u16_or(bytes: &mut [u8], offset: usize, value: u16) {
    let current = u16::from_le_bytes(bytes[offset..offset + 2].try_into().unwrap());
    set_u16(bytes, offset, current | value);
}

fn set_u32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}
