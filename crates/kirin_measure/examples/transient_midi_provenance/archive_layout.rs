const EOCD_BYTES: usize = 22;
const MAX_COMMENT_BYTES: usize = u16::MAX as usize;
const LOCAL_HEADER_BYTES: usize = 30;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct EocdLayout {
    pub(super) entries: usize,
    pub(super) central_offset: usize,
    pub(super) central_end: usize,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct CentralEntry {
    pub(super) raw_name: String,
    pub(super) flags: u16,
    pub(super) method: u16,
    pub(super) crc32: u32,
    pub(super) compressed_size: u64,
    pub(super) uncompressed_size: u64,
    pub(super) local_header_offset: u64,
}

pub(super) fn parse_eocd(bytes: &[u8]) -> Result<EocdLayout, String> {
    if bytes.len() < EOCD_BYTES {
        return Err("ZIP is too short for EOCD".to_string());
    }
    let search_start = bytes.len().saturating_sub(EOCD_BYTES + MAX_COMMENT_BYTES);
    let mut rejected = None;
    for offset in (search_start..=bytes.len() - EOCD_BYTES).rev() {
        if bytes[offset..offset + 4] != [0x50, 0x4b, 0x05, 0x06] {
            continue;
        }
        let comment_length = le_u16(bytes, offset + 20)? as usize;
        if offset + EOCD_BYTES + comment_length != bytes.len() {
            continue;
        }
        match validate_eocd_candidate(bytes, offset) {
            Ok(layout) => return Ok(layout),
            Err(error) => rejected = Some(error),
        }
    }
    Err(rejected.unwrap_or_else(|| "valid terminal EOCD was not found".to_string()))
}

fn validate_eocd_candidate(bytes: &[u8], offset: usize) -> Result<EocdLayout, String> {
    let disk = le_u16(bytes, offset + 4)?;
    let central_disk = le_u16(bytes, offset + 6)?;
    let disk_entries = le_u16(bytes, offset + 8)?;
    let total_entries = le_u16(bytes, offset + 10)?;
    let central_size = le_u32(bytes, offset + 12)?;
    let central_offset = le_u32(bytes, offset + 16)?;
    if disk != 0 || central_disk != 0 || disk_entries != total_entries {
        return Err("multi-disk or inconsistent EOCD is forbidden".to_string());
    }
    if total_entries == u16::MAX || central_size == u32::MAX || central_offset == u32::MAX {
        return Err("ZIP64 sentinel is forbidden for the pinned archive".to_string());
    }
    let central_end = u64::from(central_offset)
        .checked_add(u64::from(central_size))
        .ok_or("EOCD central-directory range overflow")?;
    if central_end != offset as u64 {
        return Err("central directory must end exactly at EOCD".to_string());
    }
    if total_entries == 0 {
        if central_offset != 0 || central_size != 0 || offset != 0 {
            return Err("noncanonical empty EOCD candidate".to_string());
        }
    } else if bytes.get(central_offset as usize..central_offset as usize + 4)
        != Some(&[0x50, 0x4b, 0x01, 0x02])
    {
        return Err("EOCD central-directory offset lacks a central header".to_string());
    }
    Ok(EocdLayout {
        entries: total_entries as usize,
        central_offset: central_offset as usize,
        central_end: offset,
    })
}

pub(super) fn parse_central_directory(
    bytes: &[u8],
    layout: EocdLayout,
) -> Result<Vec<CentralEntry>, String> {
    let mut cursor = layout.central_offset;
    let mut entries = Vec::with_capacity(layout.entries);
    for index in 0..layout.entries {
        let fixed_end = cursor
            .checked_add(46)
            .ok_or("central fixed-header range overflow")?;
        let fixed = bytes
            .get(cursor..fixed_end)
            .ok_or_else(|| format!("central header {index} is truncated"))?;
        if fixed[..4] != [0x50, 0x4b, 0x01, 0x02] {
            return Err(format!("central header {index} signature mismatch"));
        }
        let flags = u16::from_le_bytes(fixed[8..10].try_into().unwrap());
        let method = u16::from_le_bytes(fixed[10..12].try_into().unwrap());
        let crc32 = u32::from_le_bytes(fixed[16..20].try_into().unwrap());
        let compressed_size = u32::from_le_bytes(fixed[20..24].try_into().unwrap());
        let uncompressed_size = u32::from_le_bytes(fixed[24..28].try_into().unwrap());
        let name_length = u16::from_le_bytes(fixed[28..30].try_into().unwrap()) as usize;
        let extra_length = u16::from_le_bytes(fixed[30..32].try_into().unwrap()) as usize;
        let comment_length = u16::from_le_bytes(fixed[32..34].try_into().unwrap()) as usize;
        let disk_start = u16::from_le_bytes(fixed[34..36].try_into().unwrap());
        let local_offset = u32::from_le_bytes(fixed[42..46].try_into().unwrap());
        if disk_start != 0 {
            return Err(format!("central header {index} uses another disk"));
        }
        if compressed_size == u32::MAX || uncompressed_size == u32::MAX || local_offset == u32::MAX
        {
            return Err(format!(
                "central header {index} uses forbidden ZIP64 sentinel"
            ));
        }
        let name_start = fixed_end;
        let name_end = name_start
            .checked_add(name_length)
            .ok_or("central filename range overflow")?;
        let next = name_end
            .checked_add(extra_length)
            .and_then(|value| value.checked_add(comment_length))
            .ok_or("central entry range overflow")?;
        if next > layout.central_end {
            return Err(format!(
                "central header {index} variable fields are truncated"
            ));
        }
        let raw_name = std::str::from_utf8(&bytes[name_start..name_end])
            .map_err(|_| format!("central header {index} literal name is not UTF-8"))?;
        entries.push(CentralEntry {
            raw_name: raw_name.to_string(),
            flags,
            method,
            crc32,
            compressed_size: u64::from(compressed_size),
            uncompressed_size: u64::from(uncompressed_size),
            local_header_offset: u64::from(local_offset),
        });
        cursor = next;
    }
    if cursor != layout.central_end {
        return Err("central directory contains uncounted bytes or entries".to_string());
    }
    Ok(entries)
}

pub(super) fn verify_local_header(
    bytes: &[u8],
    header_start: u64,
    zip_data_start: u64,
    central_name: &[u8],
    central_method: u16,
    central_encrypted: bool,
) -> Result<(), String> {
    let start = usize::try_from(header_start).map_err(|_| "local header offset is too large")?;
    let fixed_end = start
        .checked_add(LOCAL_HEADER_BYTES)
        .ok_or("selected local-header range overflow")?;
    let fixed = bytes
        .get(start..fixed_end)
        .ok_or("selected local header is truncated")?;
    if fixed[..4] != [0x50, 0x4b, 0x03, 0x04] {
        return Err("selected local header signature mismatch".to_string());
    }
    let flags = u16::from_le_bytes(fixed[6..8].try_into().unwrap());
    let method = u16::from_le_bytes(fixed[8..10].try_into().unwrap());
    let name_length = u16::from_le_bytes(fixed[26..28].try_into().unwrap()) as usize;
    let extra_length = u16::from_le_bytes(fixed[28..30].try_into().unwrap()) as usize;
    if flags & 1 != u16::from(central_encrypted) {
        return Err("selected local encryption flag disagrees with central metadata".to_string());
    }
    if method != central_method {
        return Err(
            "selected local compression method disagrees with central metadata".to_string(),
        );
    }
    let name_start = start
        .checked_add(LOCAL_HEADER_BYTES)
        .ok_or("selected local-name offset overflow")?;
    let name_end = name_start
        .checked_add(name_length)
        .ok_or("selected local-name range overflow")?;
    let data_start = name_end
        .checked_add(extra_length)
        .ok_or("selected local-extra range overflow")?;
    let local_name = bytes
        .get(name_start..name_end)
        .ok_or("selected local filename is truncated")?;
    if local_name != central_name {
        return Err("selected local filename disagrees with central filename".to_string());
    }
    if data_start as u64 != zip_data_start {
        return Err("selected local data offset disagrees with ZIP parser".to_string());
    }
    Ok(())
}

fn le_u16(bytes: &[u8], offset: usize) -> Result<u16, String> {
    let value = bytes.get(offset..offset + 2).ok_or("truncated EOCD u16")?;
    Ok(u16::from_le_bytes(value.try_into().unwrap()))
}

fn le_u32(bytes: &[u8], offset: usize) -> Result<u32, String> {
    let value = bytes.get(offset..offset + 4).ok_or("truncated EOCD u32")?;
    Ok(u32::from_le_bytes(value.try_into().unwrap()))
}
