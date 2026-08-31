use std::fs::{File, Metadata};
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;
use std::time::UNIX_EPOCH;

use sha2::{Digest, Sha256};
use zip::ZipArchive;

use crate::contract::{
    ARCHIVE_MEMBER_PREFIX, PINNED_ARCHIVE_ENTRIES, PINNED_AUDIO_ARCHIVE_BYTES,
    PINNED_AUDIO_ARCHIVE_SHA256, PINNED_AUDIO_SELECTED_COMPRESSED_BYTES,
    PINNED_AUDIO_SELECTED_LEDGER_SHA256, PINNED_AUDIO_SELECTED_UNCOMPRESSED_BYTES,
    PINNED_CENTRAL_OFFSET, PINNED_CENTRAL_SIZE, PINNED_FULL_LAYOUT_LEDGER_SHA256,
    PINNED_MANIFEST_ROWS, PINNED_MIDI_SELECTED_COMPRESSED_BYTES,
    PINNED_MIDI_SELECTED_LEDGER_SHA256, PINNED_MIDI_SELECTED_UNCOMPRESSED_BYTES,
};

#[path = "archive_capture.rs"]
mod capture;
#[path = "archive_catalog.rs"]
mod catalog;
#[path = "archive_layout.rs"]
mod layout;

const END_SCAN_BYTES: u64 = 128 * 1024;
const MAX_DIRECTORY_REGION_BYTES: u64 = 32 * 1024 * 1024;
const MAX_MEMBER_BYTES: u64 = 64 * 1024 * 1024;
const MAX_SELECTED_BYTES: u64 = 1024 * 1024 * 1024;
const MAX_LOCAL_HEADER_BYTES: u64 = 132 * 1024;
const MAX_LOCAL_HEADER_CAPTURE_BYTES: u64 = 32 * 1024 * 1024;
const MAX_COMPRESSION_RATIO: u64 = 16;
const STREAM_CHUNK_BYTES: usize = 8 * 1024 * 1024;
const FULL_LAYOUT_DOMAIN: &str = "attack-drum-audio-archive-full-local-layout-ledger-v1";
const AUDIO_LEDGER_DOMAIN: &str = "attack-drum-audio-selected-central-ledger-v1";
const MIDI_LEDGER_DOMAIN: &str = "attack-drum-audio-archive-selected-midi-central-ledger-v1";

#[derive(Clone, Debug)]
pub(crate) struct SelectionSummary {
    pub(crate) members: usize,
    pub(crate) uncompressed_bytes: u64,
    pub(crate) compressed_bytes: u64,
    pub(crate) central_ledger_sha256: String,
}

#[derive(Clone, Debug)]
pub(crate) struct ArchiveVerification {
    pub(crate) archive_sha256: String,
    pub(crate) archive_bytes: u64,
    pub(crate) central_directory_entries: usize,
    pub(crate) central_directory_offset: u64,
    pub(crate) central_directory_size: u64,
    pub(crate) authenticated_local_header_count: usize,
    pub(crate) overlapping_payload_ranges: usize,
    pub(crate) full_layout_ledger_sha256: String,
    pub(crate) audio: SelectionSummary,
    pub(crate) midi: SelectionSummary,
}

#[derive(Debug)]
pub(crate) struct VerifiedPair {
    pub(crate) audio: VerifiedMember,
    pub(crate) midi: VerifiedMember,
}

#[derive(Debug)]
pub(crate) struct VerifiedMember {
    pub(crate) relative_name: String,
    pub(crate) member_name: String,
    pub(crate) uncompressed_size: u64,
    pub(crate) compressed_size: u64,
    pub(crate) crc32: u32,
    pub(crate) compression: &'static str,
    pub(crate) bytes: Vec<u8>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum MemberKind {
    Audio,
    Midi,
}

#[derive(Clone, Debug)]
struct SelectedMetadata {
    kind: MemberKind,
    rank: usize,
    relative_name: String,
    entry_index: usize,
    data_start: u64,
    entry: layout::CentralEntry,
}

#[derive(Clone, Debug)]
struct LocalLayout {
    pub(super) entry_index: usize,
    pub(super) header_start: u64,
    pub(super) data_start: u64,
    pub(super) data_end: u64,
}

#[derive(Clone, Copy)]
struct ExpectedArchive<'a> {
    sha256: &'a str,
    bytes: u64,
    entries: usize,
    pinned_selection: bool,
}

pub(crate) fn process_pinned_archive<T>(
    path: &Path,
    audio_names: &[String],
    midi_names: &[String],
    processor: impl FnMut(VerifiedPair) -> Result<T, String>,
) -> Result<(ArchiveVerification, Vec<T>), String> {
    if audio_names.len() != PINNED_MANIFEST_ROWS || midi_names.len() != PINNED_MANIFEST_ROWS {
        return Err("pinned archive requires exactly 290 audio/MIDI pairs".to_string());
    }
    let result = process_archive(
        path,
        audio_names,
        midi_names,
        ExpectedArchive {
            sha256: PINNED_AUDIO_ARCHIVE_SHA256,
            bytes: PINNED_AUDIO_ARCHIVE_BYTES,
            entries: PINNED_ARCHIVE_ENTRIES,
            pinned_selection: true,
        },
        processor,
    )?;
    validate_pinned_summary(&result.0)?;
    Ok(result)
}

fn process_archive<T>(
    path: &Path,
    audio_names: &[String],
    midi_names: &[String],
    expected: ExpectedArchive<'_>,
    mut processor: impl FnMut(VerifiedPair) -> Result<T, String>,
) -> Result<(ArchiveVerification, Vec<T>), String> {
    let requested = catalog::requested_members(audio_names, midi_names)?;
    let mut file =
        File::open(path).map_err(|error| format!("cannot open official audio archive: {error}"))?;
    let before = FileIdentity::from_metadata(
        &file
            .metadata()
            .map_err(|error| format!("cannot inspect official audio archive: {error}"))?,
    )?;
    if before.length != expected.bytes {
        return Err(format!(
            "official audio archive byte length mismatch: expected {}, got {}",
            expected.bytes, before.length
        ));
    }
    let provisional = read_directory_region(&mut file, before.length)?;
    if provisional.layout.entries != expected.entries {
        return Err(format!(
            "central entry count mismatch: expected {}, got {}",
            expected.entries, provisional.layout.entries
        ));
    }
    let central = parse_central(&provisional)?;
    let mut archive = ZipArchive::new(file)
        .map_err(|error| format!("invalid official audio ZIP archive: {error}"))?;
    if archive.offset() != 0 || archive.len() != central.len() {
        return Err("ZIP parser disagrees with the independent central layout".to_string());
    }
    if archive
        .has_overlapping_files()
        .map_err(|error| format!("cannot audit overlapping ZIP members: {error}"))?
    {
        return Err("audio ZIP contains overlapping member data".to_string());
    }
    let catalog = catalog::scan_catalog(
        &central,
        &mut archive,
        &requested,
        provisional.layout.central_offset,
    )?;
    file = archive.into_inner();
    catalog::validate_gapless_layout(&catalog.locals, provisional.layout.central_offset)?;
    catalog::validate_capture_budget(&catalog.locals)?;
    let audio_summary = selection_summary(&catalog.selected.audio, AUDIO_LEDGER_DOMAIN)?;
    let midi_summary = selection_summary(&catalog.selected.midi, MIDI_LEDGER_DOMAIN)?;
    if audio_summary
        .compressed_bytes
        .checked_add(midi_summary.compressed_bytes)
        .filter(|total| *total <= MAX_SELECTED_BYTES)
        .is_none()
    {
        return Err("combined selected compressed capture exceeds fixed bound".to_string());
    }
    if expected.pinned_selection {
        validate_pinned_selection(&audio_summary, &midi_summary)?;
    }
    let mut captures = capture::capture_authenticated_ranges(
        &mut file,
        before.length,
        &catalog.locals,
        &catalog.selected,
        provisional.layout.central_offset,
    )?;
    if captures.archive_sha256 != expected.sha256 {
        return Err(format!(
            "official audio archive SHA-256 mismatch: expected {}, got {}",
            expected.sha256, captures.archive_sha256
        ));
    }
    let after = FileIdentity::from_metadata(
        &file
            .metadata()
            .map_err(|error| format!("cannot reinspect official audio archive: {error}"))?,
    )?;
    if before != after || captures.directory_region != provisional.bytes {
        return Err("official archive changed between preflight and authentication".to_string());
    }
    let authenticated = parse_captured_region(&captures.directory_region, before.length)?;
    if authenticated != provisional.layout {
        return Err("authenticated ZIP64 layout differs from preflight".to_string());
    }
    let authenticated_central = layout::parse_central_directory(
        &captures.directory_region[..authenticated.central_size as usize],
        authenticated.entries,
    )?;
    if authenticated_central != central {
        return Err("authenticated central entries differ from preflight".to_string());
    }
    capture::authenticate_local_headers(
        &captures.local_headers,
        &catalog.locals,
        &authenticated_central,
    )?;
    let verification = ArchiveVerification {
        archive_sha256: captures.archive_sha256.clone(),
        archive_bytes: before.length,
        central_directory_entries: authenticated.entries,
        central_directory_offset: authenticated.central_offset,
        central_directory_size: authenticated.central_size,
        authenticated_local_header_count: catalog.locals.len(),
        overlapping_payload_ranges: 0,
        full_layout_ledger_sha256: capture::full_layout_ledger(
            &captures.local_headers,
            &catalog.locals,
            &authenticated_central,
        )?,
        audio: audio_summary,
        midi: midi_summary,
    };
    let mut output = Vec::with_capacity(audio_names.len());
    for index in 0..audio_names.len() {
        let audio = capture::inflate_member(
            &catalog.selected.audio[index],
            &captures.local_headers,
            std::mem::take(&mut captures.payloads[index]),
        )?;
        let midi_slot = audio_names.len() + index;
        let midi = capture::inflate_member(
            &catalog.selected.midi[index],
            &captures.local_headers,
            std::mem::take(&mut captures.payloads[midi_slot]),
        )?;
        output.push(processor(VerifiedPair { audio, midi })?);
    }
    Ok((verification, output))
}

struct DirectoryRegion {
    layout: layout::ArchiveLayout,
    bytes: Vec<u8>,
}

fn read_directory_region(file: &mut File, archive_bytes: u64) -> Result<DirectoryRegion, String> {
    let tail_start = archive_bytes.saturating_sub(END_SCAN_BYTES);
    file.seek(SeekFrom::Start(tail_start))
        .map_err(|error| format!("cannot seek ZIP tail: {error}"))?;
    let mut tail = vec![0_u8; (archive_bytes - tail_start) as usize];
    file.read_exact(&mut tail)
        .map_err(|error| format!("cannot read ZIP tail: {error}"))?;
    let layout = layout::parse_end_records(&tail, tail_start, archive_bytes)?;
    let region_length = archive_bytes
        .checked_sub(layout.central_offset)
        .ok_or("central directory starts beyond archive")?;
    if region_length == 0 || region_length > MAX_DIRECTORY_REGION_BYTES {
        return Err("central directory and end records exceed the fixed bound".to_string());
    }
    file.seek(SeekFrom::Start(layout.central_offset))
        .map_err(|error| format!("cannot seek central directory: {error}"))?;
    let mut bytes = vec![0_u8; region_length as usize];
    file.read_exact(&mut bytes)
        .map_err(|error| format!("cannot read central directory: {error}"))?;
    Ok(DirectoryRegion { layout, bytes })
}

fn parse_central(region: &DirectoryRegion) -> Result<Vec<layout::CentralEntry>, String> {
    layout::parse_central_directory(
        &region.bytes[..region.layout.central_size as usize],
        region.layout.entries,
    )
}

fn parse_captured_region(
    bytes: &[u8],
    archive_bytes: u64,
) -> Result<layout::ArchiveLayout, String> {
    let start = archive_bytes
        .checked_sub(bytes.len() as u64)
        .ok_or("captured directory region is longer than archive")?;
    layout::parse_end_records(bytes, start, archive_bytes)
}

fn selection_summary(
    selected: &[SelectedMetadata],
    domain: &str,
) -> Result<SelectionSummary, String> {
    let mut hasher = Sha256::new();
    hash_length_prefixed(&mut hasher, domain.as_bytes());
    hasher.update((selected.len() as u64).to_be_bytes());
    let mut uncompressed = 0_u64;
    let mut compressed = 0_u64;
    for item in selected {
        let entry = &item.entry;
        hasher.update((item.rank as u64).to_be_bytes());
        hash_length_prefixed(&mut hasher, entry.raw_name.as_bytes());
        hasher.update(entry.flags.to_be_bytes());
        hasher.update(entry.method.to_be_bytes());
        hasher.update(entry.crc32.to_be_bytes());
        hasher.update(entry.compressed_size.to_be_bytes());
        hasher.update(entry.uncompressed_size.to_be_bytes());
        hasher.update(entry.local_header_offset.to_be_bytes());
        uncompressed = uncompressed
            .checked_add(entry.uncompressed_size)
            .ok_or("selected uncompressed aggregate overflow")?;
        compressed = compressed
            .checked_add(entry.compressed_size)
            .ok_or("selected compressed aggregate overflow")?;
        if uncompressed > MAX_SELECTED_BYTES || compressed > MAX_SELECTED_BYTES {
            return Err("selected archive aggregate exceeds fixed bound".to_string());
        }
    }
    Ok(SelectionSummary {
        members: selected.len(),
        uncompressed_bytes: uncompressed,
        compressed_bytes: compressed,
        central_ledger_sha256: hex::encode(hasher.finalize()),
    })
}

fn hash_length_prefixed(hasher: &mut Sha256, bytes: &[u8]) {
    hasher.update((bytes.len() as u64).to_be_bytes());
    hasher.update(bytes);
}

fn validate_pinned_summary(archive: &ArchiveVerification) -> Result<(), String> {
    if archive.central_directory_offset != PINNED_CENTRAL_OFFSET
        || archive.central_directory_size != PINNED_CENTRAL_SIZE
        || archive.authenticated_local_header_count != PINNED_ARCHIVE_ENTRIES
        || archive.overlapping_payload_ranges != 0
        || archive.full_layout_ledger_sha256 != PINNED_FULL_LAYOUT_LEDGER_SHA256
        || validate_pinned_selection(&archive.audio, &archive.midi).is_err()
    {
        return Err(
            "authenticated archive summary differs from source-pinned contract".to_string(),
        );
    }
    Ok(())
}

fn validate_pinned_selection(
    audio: &SelectionSummary,
    midi: &SelectionSummary,
) -> Result<(), String> {
    if audio.members != PINNED_MANIFEST_ROWS
        || audio.uncompressed_bytes != PINNED_AUDIO_SELECTED_UNCOMPRESSED_BYTES
        || audio.compressed_bytes != PINNED_AUDIO_SELECTED_COMPRESSED_BYTES
        || audio.central_ledger_sha256 != PINNED_AUDIO_SELECTED_LEDGER_SHA256
        || midi.members != PINNED_MANIFEST_ROWS
        || midi.uncompressed_bytes != PINNED_MIDI_SELECTED_UNCOMPRESSED_BYTES
        || midi.compressed_bytes != PINNED_MIDI_SELECTED_COMPRESSED_BYTES
        || midi.central_ledger_sha256 != PINNED_MIDI_SELECTED_LEDGER_SHA256
    {
        return Err("selected archive summary differs from source pins".to_string());
    }
    Ok(())
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct FileIdentity {
    length: u64,
    modified_seconds: u64,
    modified_nanos: u32,
    #[cfg(unix)]
    device: u64,
    #[cfg(unix)]
    inode: u64,
}

impl FileIdentity {
    fn from_metadata(metadata: &Metadata) -> Result<Self, String> {
        let modified = metadata
            .modified()
            .map_err(|error| format!("archive modified time is unavailable: {error}"))?
            .duration_since(UNIX_EPOCH)
            .map_err(|_| "archive modified time predates UNIX epoch")?;
        #[cfg(unix)]
        use std::os::unix::fs::MetadataExt;
        Ok(Self {
            length: metadata.len(),
            modified_seconds: modified.as_secs(),
            modified_nanos: modified.subsec_nanos(),
            #[cfg(unix)]
            device: metadata.dev(),
            #[cfg(unix)]
            inode: metadata.ino(),
        })
    }
}

#[cfg(test)]
#[path = "archive_tests.rs"]
mod tests;
