use std::collections::BTreeMap;
use std::io::{Read, Seek};

use zip::{CompressionMethod, ZipArchive};

use super::layout;
use super::MAX_MEMBER_BYTES;
use super::{LocalLayout, MemberKind, SelectedMetadata, ARCHIVE_MEMBER_PREFIX};
use super::{MAX_COMPRESSION_RATIO, MAX_LOCAL_HEADER_BYTES, MAX_LOCAL_HEADER_CAPTURE_BYTES};

#[derive(Clone)]
pub(super) struct Requested {
    kind: MemberKind,
    rank: usize,
    relative_name: String,
}

pub(super) struct SelectedSets {
    pub(super) audio: Vec<SelectedMetadata>,
    pub(super) midi: Vec<SelectedMetadata>,
}

pub(super) struct Catalog {
    pub(super) locals: Vec<LocalLayout>,
    pub(super) selected: SelectedSets,
}

pub(super) fn requested_members(
    audio: &[String],
    midi: &[String],
) -> Result<BTreeMap<String, Requested>, String> {
    if audio.is_empty() || audio.len() != midi.len() {
        return Err("selected audio/MIDI request lists must be nonempty pairs".to_string());
    }
    let mut requested = BTreeMap::new();
    for (kind, names, suffix) in [
        (MemberKind::Audio, audio, ".wav"),
        (MemberKind::Midi, midi, ".midi"),
    ] {
        for (index, relative) in names.iter().enumerate() {
            validate_relative_name(relative, suffix)?;
            let full = format!("{ARCHIVE_MEMBER_PREFIX}{relative}");
            if requested
                .insert(
                    full.clone(),
                    Requested {
                        kind,
                        rank: index + 1,
                        relative_name: relative.clone(),
                    },
                )
                .is_some()
            {
                return Err(format!("duplicate requested archive member: {full}"));
            }
        }
    }
    Ok(requested)
}

fn validate_relative_name(name: &str, suffix: &str) -> Result<(), String> {
    if layout::canonical_name(name)? != name || !name.ends_with(suffix) {
        return Err(format!("selected member has invalid relative name: {name}"));
    }
    if name.split('/').any(|part| part == "eval_session") {
        return Err(format!("test namespace is forbidden: {name}"));
    }
    Ok(())
}

pub(super) fn scan_catalog<R: Read + Seek>(
    central: &[layout::CentralEntry],
    archive: &mut ZipArchive<R>,
    requested: &BTreeMap<String, Requested>,
    central_offset: u64,
) -> Result<Catalog, String> {
    let mut locals = Vec::with_capacity(central.len());
    let mut selected = BTreeMap::<String, SelectedMetadata>::new();
    for (index, entry) in central.iter().enumerate() {
        let file = archive
            .by_index_raw(index)
            .map_err(|error| format!("cannot inspect ZIP member {index}: {error}"))?;
        let data_start = file
            .data_start()
            .ok_or_else(|| format!("ZIP member has no data offset: {}", entry.raw_name))?;
        if file.name().as_bytes() != entry.raw_name.as_bytes()
            || file.name_raw() != entry.raw_name.as_bytes()
            || file.header_start() != entry.local_header_offset
            || file.size() != entry.uncompressed_size
            || file.compressed_size() != entry.compressed_size
            || file.crc32() != entry.crc32
            || compression_code(file.compression())? != entry.method
            || file.encrypted() != (entry.flags & 1 != 0)
        {
            return Err(format!("ZIP parser disagrees with central header {index}"));
        }
        let data_end = data_start
            .checked_add(entry.compressed_size)
            .ok_or("ZIP member data range overflow")?;
        if entry.local_header_offset >= data_start || data_end > central_offset {
            return Err(format!(
                "ZIP member range is outside local payload area: {}",
                entry.raw_name
            ));
        }
        locals.push(LocalLayout {
            entry_index: index,
            header_start: entry.local_header_offset,
            data_start,
            data_end,
        });
        if let Some(request) = requested.get(&entry.raw_name) {
            validate_selected(entry, &file)?;
            selected.insert(
                entry.raw_name.clone(),
                SelectedMetadata {
                    kind: request.kind,
                    rank: request.rank,
                    relative_name: request.relative_name.clone(),
                    entry_index: index,
                    data_start,
                    entry: entry.clone(),
                },
            );
        }
    }
    if selected.len() != requested.len() {
        let missing = requested
            .keys()
            .find(|name| !selected.contains_key(*name))
            .map(String::as_str)
            .unwrap_or("unknown");
        return Err(format!("selected archive member is missing: {missing}"));
    }
    let ordered = |kind| -> Result<Vec<SelectedMetadata>, String> {
        (1..=requested.len() / 2)
            .map(|rank| {
                selected
                    .values()
                    .find(|item| item.kind == kind && item.rank == rank)
                    .cloned()
                    .ok_or_else(|| format!("selected rank {rank} is missing"))
            })
            .collect()
    };
    Ok(Catalog {
        locals,
        selected: SelectedSets {
            audio: ordered(MemberKind::Audio)?,
            midi: ordered(MemberKind::Midi)?,
        },
    })
}

fn validate_selected<R: Read>(
    entry: &layout::CentralEntry,
    file: &zip::read::ZipFile<'_, R>,
) -> Result<(), String> {
    let regular_mode = entry
        .unix_mode
        .map(|mode| mode & 0o170_000 == 0o100_000)
        .unwrap_or(true);
    if entry.directory
        || !file.is_file()
        || file.is_symlink()
        || file.encrypted()
        || !regular_mode
        || entry.flags != 0
        || !matches!(entry.method, 0 | 8)
    {
        return Err(format!(
            "selected member is not a plain regular file: {}",
            entry.raw_name
        ));
    }
    if entry.uncompressed_size == 0
        || entry.uncompressed_size > MAX_MEMBER_BYTES
        || entry.compressed_size == 0
        || (entry.method == 0 && entry.compressed_size != entry.uncompressed_size)
        || entry.uncompressed_size
            > entry
                .compressed_size
                .checked_mul(MAX_COMPRESSION_RATIO)
                .ok_or("selected compression-ratio overflow")?
    {
        return Err(format!(
            "selected member exceeds fixed size/ratio bounds: {}",
            entry.raw_name
        ));
    }
    Ok(())
}

pub(super) fn compression_code(method: CompressionMethod) -> Result<u16, String> {
    match method {
        CompressionMethod::Stored => Ok(0),
        CompressionMethod::Deflated => Ok(8),
        other => Err(format!("unsupported archive compression method: {other:?}")),
    }
}

pub(super) fn validate_gapless_layout(
    locals: &[LocalLayout],
    central_offset: u64,
) -> Result<(), String> {
    let mut sorted = locals.iter().collect::<Vec<_>>();
    sorted.sort_by_key(|item| item.header_start);
    if sorted.first().map(|item| item.header_start) != Some(0)
        || sorted.last().map(|item| item.data_end) != Some(central_offset)
    {
        return Err("local member area is not anchored to archive/central boundaries".to_string());
    }
    for pair in sorted.windows(2) {
        if pair[0].data_end != pair[1].header_start {
            return Err("local member area contains a gap or overlap".to_string());
        }
    }
    Ok(())
}

pub(super) fn validate_capture_budget(locals: &[LocalLayout]) -> Result<u64, String> {
    let mut total = 0_u64;
    for local in locals {
        let length = local
            .data_start
            .checked_sub(local.header_start)
            .ok_or("local header range is reversed")?;
        if length == 0 || length > MAX_LOCAL_HEADER_BYTES {
            return Err("local header exceeds fixed per-entry capture bound".to_string());
        }
        total = total
            .checked_add(length)
            .ok_or("local header capture aggregate overflow")?;
        if total > MAX_LOCAL_HEADER_CAPTURE_BYTES {
            return Err("local header capture aggregate exceeds fixed bound".to_string());
        }
    }
    Ok(total)
}
