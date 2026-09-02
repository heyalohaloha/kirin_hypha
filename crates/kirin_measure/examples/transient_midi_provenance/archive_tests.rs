use std::collections::{BTreeMap, BTreeSet};
use std::io::{Cursor, Write};

use zip::write::SimpleFileOptions;
use zip::ZipWriter;

use super::*;

const SELECTED: &str = "drummer1/session1/1_rock_120_beat_4-4_1.midi";

#[test]
fn verifies_selected_members_in_request_order_from_one_buffer() {
    let second = "drummer2/session1/2_funk_100_fill_4-4_2.midi";
    let archive = make_zip(&[
        (full(SELECTED), b"first".as_slice()),
        (full(second), b"second".as_slice()),
        (
            "e-gmd-v1.0.0/drummer1/eval_session/unselected.midi".to_string(),
            b"opaque unselected bytes".as_slice(),
        ),
    ]);
    let result =
        verify_archive_bytes(&archive, &[second.to_string(), SELECTED.to_string()], 3).unwrap();
    assert_eq!(result.central_directory_entries, 3);
    assert_eq!(result.archive_bytes, archive.len() as u64);
    assert_eq!(result.archive_sha256, sha256_bytes(&archive));
    assert_eq!(result.selected_members[0].relative_name, second);
    assert_eq!(result.selected_members[0].bytes, b"second");
    assert_eq!(result.selected_members[1].bytes, b"first");
    assert_eq!(result.selected_uncompressed_bytes, 11);
    assert_eq!(result.selected_compressed_bytes, 11);
}

#[test]
fn eocd_count_is_independent_and_comment_signatures_are_not_eocd() {
    let mut archive = make_zip(&[(full(SELECTED), b"midi")]);
    set_zip_comment(
        &mut archive,
        b"comment-prefix-PK\x05\x06\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    );
    assert_eq!(eocd_entry_count(&archive).unwrap(), 1);
    let error = verify_archive_bytes(&archive, &[SELECTED.to_string()], 2).unwrap_err();
    assert!(error.contains("EOCD entry count mismatch"), "{error}");
}

#[test]
fn duplicate_decoded_central_name_cannot_be_hidden_by_zip_index() {
    let a = "drummer1/session1/a.midi";
    let b = "drummer1/session1/b.midi";
    let mut archive = make_zip(&[(full(a), b"a"), (full(b), b"b")]);
    let central = central_headers(&archive);
    assert_eq!(central.len(), 2);
    let second_name = central[1] + 46;
    let marker = archive[second_name..]
        .iter()
        .position(|byte| *byte == b'b')
        .unwrap();
    archive[second_name + marker] = b'a';
    let error = verify_archive_bytes(&archive, &[a.to_string()], 2).unwrap_err();
    assert!(
        error.contains("entry count disagrees") || error.contains("invalid MIDI ZIP"),
        "{error}"
    );
}

#[test]
fn overlapping_local_member_ranges_fail_before_payload_read() {
    let a = "drummer1/session1/a.midi";
    let b = "drummer1/session1/b.midi";
    let mut archive = make_zip(&[(full(a), b"aaaa"), (full(b), b"bbbb")]);
    let central = central_headers(&archive);
    let first_offset = archive[central[0] + 42..central[0] + 46].to_vec();
    archive[central[1] + 42..central[1] + 46].copy_from_slice(&first_offset);
    let error = verify_archive_bytes(&archive, &[a.to_string()], 2).unwrap_err();
    assert!(error.contains("overlapping"), "{error}");
}

#[test]
fn selected_local_and_central_filenames_must_match_byte_for_byte() {
    let mut archive = make_zip(&[(full(SELECTED), b"midi")]);
    let local_name = 30;
    assert_eq!(archive[local_name], b'e');
    archive[local_name] = b'E';
    let error = verify_archive_bytes(&archive, &[SELECTED.to_string()], 1).unwrap_err();
    assert!(
        error.contains("local filename disagrees") || error.contains("cannot inspect ZIP member"),
        "{error}"
    );
}

#[test]
fn corrupt_payload_crc_fails_through_the_full_archive_path() {
    let mut archive = make_zip(&[(full(SELECTED), b"midi")]);
    let name_length = u16::from_le_bytes(archive[26..28].try_into().unwrap()) as usize;
    let extra_length = u16::from_le_bytes(archive[28..30].try_into().unwrap()) as usize;
    let data_start = 30 + name_length + extra_length;
    archive[data_start] ^= 0xff;
    let error = verify_archive_bytes(&archive, &[SELECTED.to_string()], 1).unwrap_err();
    assert!(
        error.contains("CRC32")
            || error.contains("checksum")
            || error.contains("shorter than declared"),
        "{error}"
    );
}

#[test]
fn encrypted_flags_fail_through_the_full_archive_path() {
    let mut archive = make_zip(&[(full(SELECTED), b"midi")]);
    let central = central_headers(&archive)[0];
    let local_flags = u16::from_le_bytes(archive[6..8].try_into().unwrap()) | 1;
    let central_flags =
        u16::from_le_bytes(archive[central + 8..central + 10].try_into().unwrap()) | 1;
    archive[6..8].copy_from_slice(&local_flags.to_le_bytes());
    archive[central + 8..central + 10].copy_from_slice(&central_flags.to_le_bytes());
    let error = verify_archive_bytes(&archive, &[SELECTED.to_string()], 1).unwrap_err();
    let lowercase = error.to_ascii_lowercase();
    assert!(
        lowercase.contains("non-encrypted")
            || lowercase.contains("encrypted")
            || lowercase.contains("password"),
        "{error}"
    );
}

#[test]
fn every_catalog_name_is_exact_safe_ascii_and_case_unique() {
    for invalid in [
        "",
        "/absolute.midi",
        "C:/absolute.midi",
        "a\\b.midi",
        "a/../b.midi",
        "a/./b.midi",
        "a//b.midi",
        "nul\0name.midi",
        "nøn-ascii.midi",
    ] {
        assert!(canonical_archive_name(invalid).is_err(), "{invalid:?}");
    }
    let mut guard = CatalogGuard::default();
    guard.insert("safe/path.midi").unwrap();
    assert!(guard
        .insert("SAFE/PATH.MIDI")
        .unwrap_err()
        .contains("case-fold"));

    let mut guard = CatalogGuard::default();
    guard.insert("safe/path").unwrap();
    assert!(guard
        .insert("safe/path/")
        .unwrap_err()
        .contains("canonical"));
    let mut guard = CatalogGuard::default();
    guard.insert("same.midi").unwrap();
    assert!(guard.insert("same.midi").unwrap_err().contains("exact"));
}

#[test]
fn selected_request_rejects_duplicates_test_namespace_and_non_midi() {
    let duplicate = vec![SELECTED.to_string(), SELECTED.to_string()];
    assert!(requested_members(&duplicate)
        .unwrap_err()
        .contains("duplicate"));
    assert!(
        requested_members(&["drummer1/eval_session/a.midi".to_string()])
            .unwrap_err()
            .contains("test MIDI")
    );
    assert!(requested_members(&["drummer1/session1/audio.wav".to_string()]).is_err());
    assert!(requested_members(&[]).is_err());
}

#[test]
fn selected_metadata_rejects_special_encrypted_unsupported_and_bombs() {
    let mut directory = metadata();
    directory.is_file = false;
    assert!(directory.validate().is_err());
    let mut symlink = metadata();
    symlink.is_symlink = true;
    assert!(symlink.validate().is_err());
    let mut encrypted = metadata();
    encrypted.encrypted = true;
    assert!(encrypted.validate().is_err());
    let mut too_large = metadata();
    too_large.size = MAX_MEMBER_BYTES + 1;
    assert!(too_large.validate().is_err());
    let mut bomb = metadata();
    bomb.compression = "deflated";
    bomb.size = 101;
    bomb.compressed_size = 1;
    assert!(bomb.validate().unwrap_err().contains("ratio"));
    assert!(compression_name(CompressionMethod::BZIP2).is_err());
}

#[test]
fn selected_symlink_archive_is_rejected() {
    let mut cursor = Cursor::new(Vec::new());
    {
        let mut writer = ZipWriter::new(&mut cursor);
        writer
            .add_symlink(
                full(SELECTED),
                "target",
                SimpleFileOptions::default().compression_method(CompressionMethod::Stored),
            )
            .unwrap();
        writer.finish().unwrap();
    }
    let bytes = cursor.into_inner();
    let error = verify_archive_bytes(&bytes, &[SELECTED.to_string()], 1).unwrap_err();
    assert!(error.contains("regular file"), "{error}");
}

#[test]
fn selected_offsets_and_checked_aggregate_are_bounded() {
    let mut requested = BTreeSet::new();
    let mut selected = BTreeMap::new();
    for index in 0..2 {
        let name = format!("{MEMBER_PREFIX}drummer1/session1/{index}.midi");
        requested.insert(name.clone());
        let mut item = metadata();
        item.name = name.clone();
        item.header_start = 7;
        item.data_start = index + 10;
        selected.insert(name, item);
    }
    assert!(validate_selected_catalog(&requested, &selected)
        .unwrap_err()
        .contains("share a local"));

    requested.clear();
    selected.clear();
    for index in 0..321_u64 {
        let name = format!("{MEMBER_PREFIX}drummer1/session1/{index}.midi");
        requested.insert(name.clone());
        let mut item = metadata();
        item.name = name.clone();
        item.size = MAX_MEMBER_BYTES;
        item.compressed_size = MAX_MEMBER_BYTES;
        item.header_start = index * 2;
        item.data_start = index * 2 + 1;
        selected.insert(name, item);
    }
    assert!(validate_selected_catalog(&requested, &selected)
        .unwrap_err()
        .contains("aggregate"));
}

#[test]
fn bounded_member_read_checks_short_trailing_and_crc() {
    let expected = crc32(b"abc");
    assert_eq!(
        read_member_bounded(&mut Cursor::new(b"abc"), 3, expected, "x").unwrap(),
        b"abc"
    );
    assert!(
        read_member_bounded(&mut Cursor::new(b"ab"), 3, expected, "x")
            .unwrap_err()
            .contains("shorter")
    );
    assert!(
        read_member_bounded(&mut Cursor::new(b"abcd"), 3, expected, "x")
            .unwrap_err()
            .contains("exceeds")
    );
    assert!(read_member_bounded(&mut Cursor::new(b"abc"), 3, 0, "x")
        .unwrap_err()
        .contains("CRC32"));
}

fn metadata() -> SelectedMetadata {
    SelectedMetadata {
        name: full(SELECTED),
        size: 4,
        compressed_size: 4,
        crc32: crc32(b"midi"),
        compression: "stored",
        header_start: 0,
        data_start: 1,
        is_file: true,
        is_symlink: false,
        encrypted: false,
    }
}

fn full(relative: &str) -> String {
    format!("{MEMBER_PREFIX}{relative}")
}

fn make_zip(entries: &[(String, &[u8])]) -> Vec<u8> {
    let mut cursor = Cursor::new(Vec::new());
    {
        let mut writer = ZipWriter::new(&mut cursor);
        let options = SimpleFileOptions::default().compression_method(CompressionMethod::Stored);
        for (name, bytes) in entries {
            writer.start_file(name, options).unwrap();
            writer.write_all(bytes).unwrap();
        }
        writer.finish().unwrap();
    }
    cursor.into_inner()
}

fn central_headers(bytes: &[u8]) -> Vec<usize> {
    bytes
        .windows(4)
        .enumerate()
        .filter_map(|(index, value)| (value == b"PK\x01\x02").then_some(index))
        .collect()
}

fn set_zip_comment(bytes: &mut Vec<u8>, comment: &[u8]) {
    let eocd = bytes
        .windows(4)
        .rposition(|value| value == b"PK\x05\x06")
        .unwrap();
    bytes[eocd + 20..eocd + 22].copy_from_slice(&(comment.len() as u16).to_le_bytes());
    bytes.extend_from_slice(comment);
}
