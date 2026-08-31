use std::fs::File;
use std::io::{Read, Seek, SeekFrom};

use crc32fast::hash as crc32;
use flate2::{Decompress, FlushDecompress, Status};
use sha2::{Digest, Sha256};

use super::catalog::SelectedSets;
use super::layout;
use super::{hash_length_prefixed, LocalLayout, SelectedMetadata, VerifiedMember};
use super::{FULL_LAYOUT_DOMAIN, STREAM_CHUNK_BYTES};

#[derive(Clone, Copy)]
enum CaptureTarget {
    Local(usize),
    Payload(usize),
    Directory,
}

#[derive(Clone, Copy)]
struct CaptureSpec {
    start: u64,
    end: u64,
    target: CaptureTarget,
}

pub(super) struct AuthenticatedCaptures {
    pub(super) archive_sha256: String,
    pub(super) local_headers: Vec<Vec<u8>>,
    pub(super) payloads: Vec<Vec<u8>>,
    pub(super) directory_region: Vec<u8>,
}

pub(super) fn capture_authenticated_ranges(
    file: &mut File,
    archive_bytes: u64,
    locals: &[LocalLayout],
    selected: &SelectedSets,
    central_offset: u64,
) -> Result<AuthenticatedCaptures, String> {
    let mut specs = Vec::with_capacity(locals.len() + selected.audio.len() * 2 + 1);
    for local in locals {
        specs.push(CaptureSpec {
            start: local.header_start,
            end: local.data_start,
            target: CaptureTarget::Local(local.entry_index),
        });
    }
    for (slot, item) in selected.audio.iter().chain(&selected.midi).enumerate() {
        specs.push(CaptureSpec {
            start: item.data_start,
            end: item
                .data_start
                .checked_add(item.entry.compressed_size)
                .ok_or("selected payload capture range overflow")?,
            target: CaptureTarget::Payload(slot),
        });
    }
    specs.push(CaptureSpec {
        start: central_offset,
        end: archive_bytes,
        target: CaptureTarget::Directory,
    });
    specs.sort_by_key(|item| item.start);
    for pair in specs.windows(2) {
        if pair[0].end > pair[1].start {
            return Err("authenticated capture ranges overlap".to_string());
        }
    }
    let mut local_headers = vec![Vec::new(); locals.len()];
    let mut payloads = vec![Vec::new(); selected.audio.len() + selected.midi.len()];
    let mut directory_region = Vec::new();
    file.seek(SeekFrom::Start(0))
        .map_err(|error| format!("cannot start full archive authentication: {error}"))?;
    let mut hasher = Sha256::new();
    let mut buffer = vec![0_u8; STREAM_CHUNK_BYTES];
    let mut position = 0_u64;
    let mut first = 0_usize;
    loop {
        let read = file
            .read(&mut buffer)
            .map_err(|error| format!("cannot hash official audio archive: {error}"))?;
        if read == 0 {
            break;
        }
        let chunk = &buffer[..read];
        hasher.update(chunk);
        let chunk_end = position
            .checked_add(read as u64)
            .ok_or("archive stream offset overflow")?;
        while first < specs.len() && specs[first].end <= position {
            first += 1;
        }
        let mut index = first;
        while index < specs.len() && specs[index].start < chunk_end {
            let spec = specs[index];
            let start = spec.start.max(position);
            let end = spec.end.min(chunk_end);
            if start < end {
                let slice = &chunk[(start - position) as usize..(end - position) as usize];
                match spec.target {
                    CaptureTarget::Local(slot) => local_headers[slot].extend_from_slice(slice),
                    CaptureTarget::Payload(slot) => payloads[slot].extend_from_slice(slice),
                    CaptureTarget::Directory => directory_region.extend_from_slice(slice),
                }
            }
            index += 1;
        }
        position = chunk_end;
    }
    validate_capture_lengths(
        position,
        archive_bytes,
        locals,
        selected,
        central_offset,
        &local_headers,
        &payloads,
        &directory_region,
    )?;
    Ok(AuthenticatedCaptures {
        archive_sha256: hex::encode(hasher.finalize()),
        local_headers,
        payloads,
        directory_region,
    })
}

#[allow(clippy::too_many_arguments)]
fn validate_capture_lengths(
    position: u64,
    archive_bytes: u64,
    locals: &[LocalLayout],
    selected: &SelectedSets,
    central_offset: u64,
    local_headers: &[Vec<u8>],
    payloads: &[Vec<u8>],
    directory: &[u8],
) -> Result<(), String> {
    if position != archive_bytes {
        return Err("official audio archive changed length during authentication".to_string());
    }
    for local in locals {
        if local_headers[local.entry_index].len() as u64 != local.data_start - local.header_start {
            return Err("authenticated local-header capture is incomplete".to_string());
        }
    }
    for (slot, item) in selected.audio.iter().chain(&selected.midi).enumerate() {
        if payloads[slot].len() as u64 != item.entry.compressed_size {
            return Err("authenticated selected-payload capture is incomplete".to_string());
        }
    }
    if directory.len() as u64 != archive_bytes - central_offset {
        return Err("authenticated central/tail capture is incomplete".to_string());
    }
    Ok(())
}

pub(super) fn authenticate_local_headers(
    headers: &[Vec<u8>],
    locals: &[LocalLayout],
    central: &[layout::CentralEntry],
) -> Result<(), String> {
    for local in locals {
        let entry = central
            .get(local.entry_index)
            .ok_or("local layout central index is outside catalog")?;
        layout::verify_captured_local_header(&headers[local.entry_index], entry)?;
    }
    Ok(())
}

pub(super) fn full_layout_ledger(
    headers: &[Vec<u8>],
    locals: &[LocalLayout],
    central: &[layout::CentralEntry],
) -> Result<String, String> {
    let mut hasher = Sha256::new();
    hash_length_prefixed(&mut hasher, FULL_LAYOUT_DOMAIN.as_bytes());
    hasher.update((locals.len() as u64).to_be_bytes());
    for local in locals {
        let entry = central
            .get(local.entry_index)
            .ok_or("local ledger index error")?;
        hasher.update((local.entry_index as u64).to_be_bytes());
        hash_length_prefixed(&mut hasher, entry.raw_name.as_bytes());
        hasher.update(local.header_start.to_be_bytes());
        hasher.update(local.data_start.to_be_bytes());
        hasher.update(local.data_end.to_be_bytes());
        hasher.update(Sha256::digest(&headers[local.entry_index]));
    }
    Ok(hex::encode(hasher.finalize()))
}

pub(super) fn inflate_member(
    metadata: &SelectedMetadata,
    headers: &[Vec<u8>],
    payload: Vec<u8>,
) -> Result<VerifiedMember, String> {
    let header = headers
        .get(metadata.entry_index)
        .ok_or("selected local header index is outside capture")?;
    layout::verify_captured_local_header(header, &metadata.entry)?;
    if payload.len() as u64 != metadata.entry.compressed_size {
        return Err("selected compressed capture length mismatch".to_string());
    }
    let bytes = match metadata.entry.method {
        0 => payload,
        8 => inflate_deflate_exact(
            &payload,
            metadata.entry.uncompressed_size,
            &metadata.entry.raw_name,
        )?,
        _ => return Err("selected compression method escaped catalog validation".to_string()),
    };
    if bytes.len() as u64 != metadata.entry.uncompressed_size {
        return Err(format!(
            "selected member has wrong actual size: {}",
            metadata.entry.raw_name
        ));
    }
    let actual_crc = crc32(&bytes);
    if actual_crc != metadata.entry.crc32 {
        return Err(format!(
            "selected member CRC32 mismatch {}: expected {:08x}, got {actual_crc:08x}",
            metadata.entry.raw_name, metadata.entry.crc32
        ));
    }
    Ok(VerifiedMember {
        relative_name: metadata.relative_name.clone(),
        member_name: metadata.entry.raw_name.clone(),
        uncompressed_size: metadata.entry.uncompressed_size,
        compressed_size: metadata.entry.compressed_size,
        crc32: metadata.entry.crc32,
        compression: if metadata.entry.method == 0 {
            "stored"
        } else {
            "deflated"
        },
        bytes,
    })
}

fn inflate_deflate_exact(
    payload: &[u8],
    declared_size: u64,
    name: &str,
) -> Result<Vec<u8>, String> {
    let length = usize::try_from(declared_size).map_err(|_| format!("member too large: {name}"))?;
    let capacity = length
        .checked_add(1)
        .ok_or_else(|| format!("member output bound overflow: {name}"))?;
    let mut output = Vec::with_capacity(capacity);
    let mut decoder = Decompress::new(false);
    let status = decoder
        .decompress_vec(payload, &mut output, FlushDecompress::Finish)
        .map_err(|error| format!("invalid deflate stream {name}: {error}"))?;
    if status != Status::StreamEnd
        || decoder.total_in() != payload.len() as u64
        || decoder.total_out() != declared_size
        || output.len() != length
    {
        return Err(format!(
            "deflate stream does not consume exact compressed/uncompressed bounds: {name}"
        ));
    }
    Ok(output)
}
