use std::collections::{BTreeMap, BTreeSet};
use std::fs::File;
use std::io::{Cursor, Read};
use std::path::Path;

use crc32fast::hash as crc32;
use zip::{CompressionMethod, ZipArchive};

use crate::contract::{
    PINNED_ARCHIVE_ENTRIES, PINNED_MIDI_ARCHIVE_BYTES, PINNED_MIDI_ARCHIVE_SHA256,
};
use crate::sha256_bytes;

#[path = "archive_layout.rs"]
mod layout;

const MEMBER_PREFIX: &str = "e-gmd-v1.0.0/";
const MAX_MEMBER_BYTES: u64 = 128 * 1024;
const MAX_SELECTED_BYTES: u64 = 40 * 1024 * 1024;
const MAX_COMPRESSION_RATIO: u64 = 100;

#[derive(Debug)]
pub(crate) struct ArchiveVerification {
    pub(crate) archive_sha256: String,
    pub(crate) archive_bytes: u64,
    pub(crate) central_directory_entries: usize,
    pub(crate) selected_members: Vec<VerifiedMember>,
    pub(crate) selected_uncompressed_bytes: u64,
    pub(crate) selected_compressed_bytes: u64,
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

#[derive(Clone, Debug)]
struct SelectedMetadata {
    name: String,
    size: u64,
    compressed_size: u64,
    crc32: u32,
    compression: &'static str,
    header_start: u64,
    data_start: u64,
    is_file: bool,
    is_symlink: bool,
    encrypted: bool,
}

#[derive(Default)]
struct CatalogGuard {
    exact_names: BTreeSet<String>,
    canonical_names: BTreeSet<String>,
    casefold_names: BTreeSet<String>,
}

pub(crate) fn verify_pinned_archive(
    path: &Path,
    selected_relative_names: &[String],
) -> Result<ArchiveVerification, String> {
    let bytes = read_archive_once(path)?;
    let digest = sha256_bytes(&bytes);
    if digest != PINNED_MIDI_ARCHIVE_SHA256 {
        return Err(format!(
            "official MIDI archive SHA-256 mismatch: expected {PINNED_MIDI_ARCHIVE_SHA256}, got {digest}"
        ));
    }
    verify_archive_bytes(&bytes, selected_relative_names, PINNED_ARCHIVE_ENTRIES)
}

fn read_archive_once(path: &Path) -> Result<Vec<u8>, String> {
    let mut file =
        File::open(path).map_err(|error| format!("cannot open official MIDI archive: {error}"))?;
    let length = file
        .metadata()
        .map_err(|error| format!("cannot inspect official MIDI archive: {error}"))?
        .len();
    if length != PINNED_MIDI_ARCHIVE_BYTES {
        return Err(format!(
            "official MIDI archive byte length mismatch: expected {PINNED_MIDI_ARCHIVE_BYTES}, got {length}"
        ));
    }
    let length = usize::try_from(length).map_err(|_| "MIDI archive cannot fit in memory")?;
    let mut bytes = vec![0_u8; length];
    file.read_exact(&mut bytes)
        .map_err(|error| format!("cannot read official MIDI archive: {error}"))?;
    let mut extra = [0_u8; 1];
    if file
        .read(&mut extra)
        .map_err(|error| format!("cannot finish official MIDI archive read: {error}"))?
        != 0
    {
        return Err("official MIDI archive changed while being read".to_string());
    }
    Ok(bytes)
}

fn verify_archive_bytes(
    bytes: &[u8],
    selected_relative_names: &[String],
    expected_entries: usize,
) -> Result<ArchiveVerification, String> {
    let eocd = layout::parse_eocd(bytes)?;
    let eocd_entries = eocd.entries;
    if eocd_entries != expected_entries {
        return Err(format!(
            "EOCD entry count mismatch: expected {expected_entries}, got {eocd_entries}"
        ));
    }
    let central_entries = layout::parse_central_directory(bytes, eocd)?;
    let requested = requested_members(selected_relative_names)?;
    let mut archive = ZipArchive::new(Cursor::new(bytes))
        .map_err(|error| format!("invalid MIDI ZIP archive: {error}"))?;
    if archive.offset() != 0 {
        return Err("MIDI ZIP must not contain prepended bytes".to_string());
    }
    if archive.len() != eocd_entries {
        return Err(format!(
            "central-directory entry count disagrees with EOCD: EOCD={eocd_entries}, parsed={}",
            archive.len()
        ));
    }
    if archive
        .has_overlapping_files()
        .map_err(|error| format!("cannot audit overlapping ZIP members: {error}"))?
    {
        return Err("MIDI ZIP contains overlapping member data".to_string());
    }

    let selected_catalog = scan_catalog(bytes, &central_entries, &mut archive, &requested)?;
    let (declared_uncompressed, declared_compressed) =
        validate_selected_catalog(&requested, &selected_catalog)?;
    let members = read_selected_members(&mut archive, selected_relative_names, &selected_catalog)?;
    let actual_total = members.iter().try_fold(0_u64, |total, member| {
        total
            .checked_add(member.uncompressed_size)
            .ok_or("selected actual-size total overflow")
    })?;
    if actual_total != declared_uncompressed {
        return Err("selected actual bytes disagree with declared aggregate".to_string());
    }
    Ok(ArchiveVerification {
        archive_sha256: sha256_bytes(bytes),
        archive_bytes: bytes.len() as u64,
        central_directory_entries: eocd_entries,
        selected_members: members,
        selected_uncompressed_bytes: actual_total,
        selected_compressed_bytes: declared_compressed,
    })
}

fn requested_members(relative_names: &[String]) -> Result<BTreeSet<String>, String> {
    if relative_names.is_empty() {
        return Err("selected MIDI member list is empty".to_string());
    }
    let mut requested = BTreeSet::new();
    for relative in relative_names {
        validate_relative_midi_name(relative)?;
        let full = format!("{MEMBER_PREFIX}{relative}");
        if !requested.insert(full.clone()) {
            return Err(format!("duplicate requested MIDI member: {full}"));
        }
    }
    Ok(requested)
}

fn scan_catalog<R: Read + std::io::Seek>(
    archive_bytes: &[u8],
    central_entries: &[layout::CentralEntry],
    archive: &mut ZipArchive<R>,
    requested: &BTreeSet<String>,
) -> Result<BTreeMap<String, SelectedMetadata>, String> {
    let mut names = CatalogGuard::default();
    let mut selected = BTreeMap::new();
    if central_entries.len() != archive.len() {
        return Err("independent central-directory walk disagrees with ZIP parser".to_string());
    }
    for (index, central) in central_entries.iter().enumerate() {
        let file = archive
            .by_index(index)
            .map_err(|error| format!("cannot inspect ZIP member {index}: {error}"))?;
        let raw_name = central.raw_name.as_str();
        if file.name().as_bytes() != raw_name.as_bytes()
            || file.name_raw() != raw_name.as_bytes()
            || file.header_start() != central.local_header_offset
        {
            return Err(format!(
                "ZIP parser identity disagrees with literal central header {index}"
            ));
        }
        names.insert(raw_name)?;
        if requested.contains(raw_name) {
            let data_start = file
                .data_start()
                .ok_or_else(|| format!("selected ZIP member has no data offset: {raw_name}"))?;
            layout::verify_local_header(
                archive_bytes,
                file.header_start(),
                data_start,
                raw_name.as_bytes(),
                central.method,
                file.encrypted(),
            )?;
            if compression_code(file.compression())? != central.method
                || file.size() != central.uncompressed_size
                || file.compressed_size() != central.compressed_size
                || file.crc32() != central.crc32
                || u16::from(file.encrypted()) != (central.flags & 1)
            {
                return Err(format!(
                    "selected ZIP metadata disagrees with literal central header: {raw_name}"
                ));
            }
            selected.insert(
                raw_name.to_string(),
                SelectedMetadata {
                    name: raw_name.to_string(),
                    size: file.size(),
                    compressed_size: file.compressed_size(),
                    crc32: file.crc32(),
                    compression: compression_name(file.compression())?,
                    header_start: file.header_start(),
                    data_start,
                    is_file: file.is_file(),
                    is_symlink: file.is_symlink(),
                    encrypted: file.encrypted(),
                },
            );
        }
    }
    Ok(selected)
}

fn validate_selected_catalog(
    requested: &BTreeSet<String>,
    selected: &BTreeMap<String, SelectedMetadata>,
) -> Result<(u64, u64), String> {
    if selected.len() != requested.len() {
        let missing = requested
            .iter()
            .find(|name| !selected.contains_key(*name))
            .map(String::as_str)
            .unwrap_or("unknown");
        return Err(format!("selected MIDI member is missing: {missing}"));
    }
    let mut headers = BTreeSet::new();
    let mut data_offsets = BTreeSet::new();
    let mut uncompressed = 0_u64;
    let mut compressed = 0_u64;
    for metadata in selected.values() {
        metadata.validate()?;
        if !headers.insert(metadata.header_start) || !data_offsets.insert(metadata.data_start) {
            return Err("selected MIDI members share a local ZIP offset".to_string());
        }
        uncompressed = uncompressed
            .checked_add(metadata.size)
            .ok_or("selected declared-size total overflow")?;
        compressed = compressed
            .checked_add(metadata.compressed_size)
            .ok_or("selected compressed-size total overflow")?;
        if uncompressed > MAX_SELECTED_BYTES {
            return Err("selected MIDI aggregate exceeds the fixed output budget".to_string());
        }
    }
    Ok((uncompressed, compressed))
}

fn read_selected_members<R: Read + std::io::Seek>(
    archive: &mut ZipArchive<R>,
    relative_names: &[String],
    catalog: &BTreeMap<String, SelectedMetadata>,
) -> Result<Vec<VerifiedMember>, String> {
    relative_names
        .iter()
        .map(|relative_name| {
            let member_name = format!("{MEMBER_PREFIX}{relative_name}");
            let expected = catalog
                .get(&member_name)
                .ok_or_else(|| format!("selected MIDI member disappeared: {member_name}"))?;
            let mut file = archive
                .by_name(&member_name)
                .map_err(|error| format!("cannot open selected member {member_name}: {error}"))?;
            if file.size() != expected.size
                || file.compressed_size() != expected.compressed_size
                || file.crc32() != expected.crc32
                || file.header_start() != expected.header_start
                || file.data_start() != Some(expected.data_start)
            {
                return Err(format!("selected member metadata changed: {member_name}"));
            }
            let bytes =
                read_member_bounded(&mut file, expected.size, expected.crc32, &member_name)?;
            Ok(VerifiedMember {
                relative_name: relative_name.clone(),
                member_name,
                uncompressed_size: expected.size,
                compressed_size: expected.compressed_size,
                crc32: expected.crc32,
                compression: expected.compression,
                bytes,
            })
        })
        .collect()
}

fn read_member_bounded(
    reader: &mut impl Read,
    declared_size: u64,
    expected_crc32: u32,
    name: &str,
) -> Result<Vec<u8>, String> {
    let length = usize::try_from(declared_size).map_err(|_| format!("member too large: {name}"))?;
    let mut bytes = vec![0_u8; length];
    reader
        .read_exact(&mut bytes)
        .map_err(|error| format!("selected member is shorter than declared {name}: {error}"))?;
    let mut trailing = [0_u8; 1];
    if reader
        .read(&mut trailing)
        .map_err(|error| format!("cannot finish selected member {name}: {error}"))?
        != 0
    {
        return Err(format!("selected member exceeds its declared size: {name}"));
    }
    let actual_crc32 = crc32(&bytes);
    if actual_crc32 != expected_crc32 {
        return Err(format!(
            "selected member CRC32 mismatch {name}: expected {expected_crc32:08x}, got {actual_crc32:08x}"
        ));
    }
    Ok(bytes)
}

impl SelectedMetadata {
    fn validate(&self) -> Result<(), String> {
        if !self.is_file || self.is_symlink || self.encrypted {
            return Err(format!(
                "selected member must be a non-encrypted regular file: {}",
                self.name
            ));
        }
        if self.size == 0 || self.size > MAX_MEMBER_BYTES {
            return Err(format!(
                "selected member exceeds fixed size bounds: {}",
                self.name
            ));
        }
        if self.compressed_size == 0 {
            return Err(format!(
                "selected member has zero compressed bytes: {}",
                self.name
            ));
        }
        if self.compression == "stored" && self.compressed_size != self.size {
            return Err(format!("stored member size mismatch: {}", self.name));
        }
        let expansion_limit = self
            .compressed_size
            .checked_mul(MAX_COMPRESSION_RATIO)
            .ok_or("selected compression-ratio bound overflow")?;
        if self.size > expansion_limit {
            return Err(format!(
                "selected member compression ratio is excessive: {}",
                self.name
            ));
        }
        Ok(())
    }
}

impl CatalogGuard {
    fn insert(&mut self, name: &str) -> Result<(), String> {
        let canonical = canonical_archive_name(name)?;
        if !self.exact_names.insert(name.to_string()) {
            return Err(format!("duplicate exact ZIP member name: {name}"));
        }
        if !self.canonical_names.insert(canonical.clone()) {
            return Err(format!("duplicate canonical ZIP member name: {name}"));
        }
        let casefold = canonical.to_ascii_lowercase();
        if !self.casefold_names.insert(casefold) {
            return Err(format!("case-fold duplicate ZIP member name: {name}"));
        }
        Ok(())
    }
}

fn canonical_archive_name(name: &str) -> Result<String, String> {
    if name.is_empty() || !name.is_ascii() || name.contains(['\0', '\\']) || name.starts_with('/') {
        return Err(format!("unsafe ZIP member name: {name:?}"));
    }
    let core = name.strip_suffix('/').unwrap_or(name);
    if core.is_empty() || core.ends_with('/') {
        return Err(format!("unsafe ZIP member name: {name:?}"));
    }
    for (index, component) in core.split('/').enumerate() {
        if component.is_empty()
            || matches!(component, "." | "..")
            || (index == 0 && component.ends_with(':'))
        {
            return Err(format!("unsafe ZIP member name: {name:?}"));
        }
    }
    Ok(core.to_string())
}

fn validate_relative_midi_name(name: &str) -> Result<(), String> {
    let canonical = canonical_archive_name(name)?;
    if canonical != name || !name.ends_with(".midi") {
        return Err(format!(
            "selected member is not an exact relative MIDI path: {name}"
        ));
    }
    if name.split('/').any(|component| component == "eval_session") {
        return Err(format!("test MIDI namespace is forbidden: {name}"));
    }
    Ok(())
}

fn compression_name(method: CompressionMethod) -> Result<&'static str, String> {
    match method {
        CompressionMethod::Stored => Ok("stored"),
        CompressionMethod::Deflated => Ok("deflated"),
        other => Err(format!(
            "unsupported selected-member compression: {other:?}"
        )),
    }
}

fn compression_code(method: CompressionMethod) -> Result<u16, String> {
    match method {
        CompressionMethod::Stored => Ok(0),
        CompressionMethod::Deflated => Ok(8),
        other => Err(format!(
            "unsupported selected-member compression: {other:?}"
        )),
    }
}

#[cfg(test)]
fn eocd_entry_count(bytes: &[u8]) -> Result<usize, String> {
    Ok(layout::parse_eocd(bytes)?.entries)
}

#[cfg(test)]
#[path = "archive_tests.rs"]
mod tests;
