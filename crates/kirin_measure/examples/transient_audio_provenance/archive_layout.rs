use std::collections::BTreeSet;

const EOCD_BYTES: usize = 22;
const ZIP64_LOCATOR_BYTES: usize = 20;
const ZIP64_EOCD_BYTES: usize = 56;
const CENTRAL_HEADER_BYTES: usize = 46;
const LOCAL_HEADER_BYTES: usize = 30;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(super) struct ArchiveLayout {
    pub(super) entries: usize,
    pub(super) central_offset: u64,
    pub(super) central_size: u64,
    pub(super) central_end: u64,
    pub(super) zip64: bool,
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
    pub(super) unix_mode: Option<u32>,
    pub(super) directory: bool,
}

#[derive(Default)]
struct CatalogGuard {
    exact: BTreeSet<String>,
    canonical: BTreeSet<String>,
    casefold: BTreeSet<String>,
}

pub(super) fn parse_end_records(
    tail: &[u8],
    tail_start: u64,
    archive_bytes: u64,
) -> Result<ArchiveLayout, String> {
    if tail_start
        .checked_add(tail.len() as u64)
        .filter(|end| *end == archive_bytes)
        .is_none()
    {
        return Err("ZIP tail range does not end at the archive boundary".to_string());
    }
    if tail.len() < EOCD_BYTES {
        return Err("ZIP is too short for EOCD".to_string());
    }
    let mut rejected = None;
    for offset in (0..=tail.len() - EOCD_BYTES).rev() {
        if tail[offset..offset + 4] != [0x50, 0x4b, 0x05, 0x06] {
            continue;
        }
        let comment = le_u16(tail, offset + 20)? as usize;
        if offset + EOCD_BYTES + comment != tail.len() {
            continue;
        }
        match validate_eocd(tail, tail_start, offset) {
            Ok(layout) => return Ok(layout),
            Err(error) => rejected = Some(error),
        }
    }
    Err(rejected.unwrap_or_else(|| "valid terminal EOCD was not found".to_string()))
}

fn validate_eocd(tail: &[u8], tail_start: u64, offset: usize) -> Result<ArchiveLayout, String> {
    let disk = le_u16(tail, offset + 4)?;
    let central_disk = le_u16(tail, offset + 6)?;
    let disk_entries = le_u16(tail, offset + 8)?;
    let total_entries = le_u16(tail, offset + 10)?;
    let central_size32 = le_u32(tail, offset + 12)?;
    let central_offset32 = le_u32(tail, offset + 16)?;
    if disk != 0 || central_disk != 0 || disk_entries != total_entries {
        return Err("multi-disk or inconsistent EOCD is forbidden".to_string());
    }
    let eocd_global = tail_start
        .checked_add(offset as u64)
        .ok_or("EOCD global offset overflow")?;
    let uses_zip64 =
        total_entries == u16::MAX || central_size32 == u32::MAX || central_offset32 == u32::MAX;
    if !uses_zip64 {
        let central_offset = u64::from(central_offset32);
        let central_size = u64::from(central_size32);
        require_central_end(central_offset, central_size, eocd_global)?;
        return Ok(ArchiveLayout {
            entries: usize::from(total_entries),
            central_offset,
            central_size,
            central_end: eocd_global,
            zip64: false,
        });
    }
    if offset < ZIP64_LOCATOR_BYTES {
        return Err("ZIP64 EOCD locator is missing".to_string());
    }
    let locator = offset - ZIP64_LOCATOR_BYTES;
    if tail[locator..locator + 4] != [0x50, 0x4b, 0x06, 0x07]
        || le_u32(tail, locator + 4)? != 0
        || le_u32(tail, locator + 16)? != 1
    {
        return Err("invalid single-disk ZIP64 EOCD locator".to_string());
    }
    let zip64_global = le_u64(tail, locator + 8)?;
    let zip64_relative = zip64_global
        .checked_sub(tail_start)
        .and_then(|value| usize::try_from(value).ok())
        .ok_or("ZIP64 EOCD lies outside the bounded tail")?;
    let zip64_end = zip64_relative
        .checked_add(ZIP64_EOCD_BYTES)
        .ok_or("ZIP64 EOCD range overflow")?;
    if zip64_end != locator || tail.get(zip64_relative..zip64_relative + 4) != Some(b"PK\x06\x06") {
        return Err("ZIP64 EOCD does not end exactly at its locator".to_string());
    }
    if le_u64(tail, zip64_relative + 4)? != 44
        || le_u32(tail, zip64_relative + 16)? != 0
        || le_u32(tail, zip64_relative + 20)? != 0
    {
        return Err("noncanonical or multi-disk ZIP64 EOCD".to_string());
    }
    let disk_entries64 = le_u64(tail, zip64_relative + 24)?;
    let total_entries64 = le_u64(tail, zip64_relative + 32)?;
    if disk_entries64 != total_entries64 || total_entries64 == 0 {
        return Err("inconsistent or empty ZIP64 central directory".to_string());
    }
    let central_size = le_u64(tail, zip64_relative + 40)?;
    let central_offset = le_u64(tail, zip64_relative + 48)?;
    if (total_entries != u16::MAX && u64::from(total_entries) != total_entries64)
        || (central_size32 != u32::MAX && u64::from(central_size32) != central_size)
        || (central_offset32 != u32::MAX && u64::from(central_offset32) != central_offset)
    {
        return Err("classic EOCD fields disagree with ZIP64 EOCD".to_string());
    }
    require_central_end(central_offset, central_size, zip64_global)?;
    Ok(ArchiveLayout {
        entries: usize::try_from(total_entries64).map_err(|_| "ZIP64 entry count is too large")?,
        central_offset,
        central_size,
        central_end: zip64_global,
        zip64: true,
    })
}

fn require_central_end(offset: u64, size: u64, expected: u64) -> Result<(), String> {
    if offset
        .checked_add(size)
        .filter(|end| *end == expected)
        .is_none()
    {
        return Err("central directory does not end exactly at its end record".to_string());
    }
    Ok(())
}

pub(super) fn parse_central_directory(
    bytes: &[u8],
    expected_entries: usize,
) -> Result<Vec<CentralEntry>, String> {
    let mut entries = Vec::with_capacity(expected_entries);
    let mut names = CatalogGuard::default();
    let mut cursor = 0_usize;
    for index in 0..expected_entries {
        let fixed_end = cursor
            .checked_add(CENTRAL_HEADER_BYTES)
            .ok_or("central fixed-header range overflow")?;
        let fixed = bytes
            .get(cursor..fixed_end)
            .ok_or_else(|| format!("central header {index} is truncated"))?;
        if fixed[..4] != [0x50, 0x4b, 0x01, 0x02] {
            return Err(format!("central header {index} signature mismatch"));
        }
        let made_by = u16::from_le_bytes(fixed[4..6].try_into().unwrap());
        let flags = u16::from_le_bytes(fixed[8..10].try_into().unwrap());
        let method = u16::from_le_bytes(fixed[10..12].try_into().unwrap());
        let crc32 = u32::from_le_bytes(fixed[16..20].try_into().unwrap());
        let compressed32 = u32::from_le_bytes(fixed[20..24].try_into().unwrap());
        let uncompressed32 = u32::from_le_bytes(fixed[24..28].try_into().unwrap());
        let name_len = u16::from_le_bytes(fixed[28..30].try_into().unwrap()) as usize;
        let extra_len = u16::from_le_bytes(fixed[30..32].try_into().unwrap()) as usize;
        let comment_len = u16::from_le_bytes(fixed[32..34].try_into().unwrap()) as usize;
        let disk32 = u16::from_le_bytes(fixed[34..36].try_into().unwrap());
        let external = u32::from_le_bytes(fixed[38..42].try_into().unwrap());
        let offset32 = u32::from_le_bytes(fixed[42..46].try_into().unwrap());
        let name_end = fixed_end
            .checked_add(name_len)
            .ok_or("central name overflow")?;
        let extra_end = name_end
            .checked_add(extra_len)
            .ok_or("central extra overflow")?;
        let next = extra_end
            .checked_add(comment_len)
            .ok_or("central comment overflow")?;
        if next > bytes.len() {
            return Err(format!(
                "central header {index} variable fields are truncated"
            ));
        }
        let raw_name = std::str::from_utf8(&bytes[fixed_end..name_end])
            .map_err(|_| format!("central header {index} literal name is not UTF-8"))?;
        names.insert(raw_name)?;
        let zip64 = parse_zip64_extra(
            &bytes[name_end..extra_end],
            uncompressed32,
            compressed32,
            offset32,
            disk32,
            index,
        )?;
        let uncompressed_size = zip64.uncompressed.unwrap_or(u64::from(uncompressed32));
        let compressed_size = zip64.compressed.unwrap_or(u64::from(compressed32));
        let local_header_offset = zip64.offset.unwrap_or(u64::from(offset32));
        let disk = zip64.disk.unwrap_or(u32::from(disk32));
        if disk != 0 {
            return Err(format!("central header {index} uses another disk"));
        }
        let creator = (made_by >> 8) as u8;
        let unix_mode = (creator == 3).then_some(external >> 16);
        let directory = raw_name.ends_with('/');
        entries.push(CentralEntry {
            raw_name: raw_name.to_string(),
            flags,
            method,
            crc32,
            compressed_size,
            uncompressed_size,
            local_header_offset,
            unix_mode,
            directory,
        });
        cursor = next;
    }
    if cursor != bytes.len() {
        return Err("central directory contains uncounted bytes or entries".to_string());
    }
    Ok(entries)
}

#[derive(Default)]
struct Zip64Values {
    uncompressed: Option<u64>,
    compressed: Option<u64>,
    offset: Option<u64>,
    disk: Option<u32>,
}

fn parse_zip64_extra(
    extra: &[u8],
    uncompressed32: u32,
    compressed32: u32,
    offset32: u32,
    disk32: u16,
    index: usize,
) -> Result<Zip64Values, String> {
    let mut cursor = 0_usize;
    let mut zip64 = None;
    while cursor < extra.len() {
        let header = extra
            .get(cursor..cursor + 4)
            .ok_or_else(|| format!("central header {index} has truncated extra header"))?;
        let id = u16::from_le_bytes(header[..2].try_into().unwrap());
        let length = u16::from_le_bytes(header[2..].try_into().unwrap()) as usize;
        let start = cursor + 4;
        let end = start
            .checked_add(length)
            .ok_or("central extra field range overflow")?;
        let field = extra
            .get(start..end)
            .ok_or_else(|| format!("central header {index} has truncated extra field"))?;
        if id == 1 && zip64.replace(field).is_some() {
            return Err(format!("central header {index} repeats ZIP64 extra"));
        }
        cursor = end;
    }
    let mut values = Zip64Values::default();
    let required = [
        uncompressed32 == u32::MAX,
        compressed32 == u32::MAX,
        offset32 == u32::MAX,
        disk32 == u16::MAX,
    ];
    if required.iter().any(|value| *value) && zip64.is_none() {
        return Err(format!("central header {index} lacks required ZIP64 extra"));
    }
    if let Some(field) = zip64 {
        let mut position = 0_usize;
        if required[0] {
            values.uncompressed = Some(take_u64(field, &mut position, index)?);
        }
        if required[1] {
            values.compressed = Some(take_u64(field, &mut position, index)?);
        }
        if required[2] {
            values.offset = Some(take_u64(field, &mut position, index)?);
        }
        if required[3] {
            let bytes = field
                .get(position..position + 4)
                .ok_or_else(|| format!("central header {index} ZIP64 disk is truncated"))?;
            values.disk = Some(u32::from_le_bytes(bytes.try_into().unwrap()));
            position += 4;
        }
        if position != field.len() {
            return Err(format!(
                "central header {index} has noncanonical ZIP64 extra"
            ));
        }
    }
    Ok(values)
}

fn take_u64(field: &[u8], position: &mut usize, index: usize) -> Result<u64, String> {
    let bytes = field
        .get(*position..*position + 8)
        .ok_or_else(|| format!("central header {index} ZIP64 value is truncated"))?;
    *position += 8;
    Ok(u64::from_le_bytes(bytes.try_into().unwrap()))
}

pub(super) fn verify_captured_local_header(
    header: &[u8],
    central: &CentralEntry,
) -> Result<(), String> {
    let fixed = header
        .get(..LOCAL_HEADER_BYTES)
        .ok_or("selected local header is truncated")?;
    if fixed[..4] != [0x50, 0x4b, 0x03, 0x04] {
        return Err("selected local header signature mismatch".to_string());
    }
    let flags = u16::from_le_bytes(fixed[6..8].try_into().unwrap());
    let method = u16::from_le_bytes(fixed[8..10].try_into().unwrap());
    let crc32 = u32::from_le_bytes(fixed[14..18].try_into().unwrap());
    let compressed = u32::from_le_bytes(fixed[18..22].try_into().unwrap());
    let uncompressed = u32::from_le_bytes(fixed[22..26].try_into().unwrap());
    let name_len = u16::from_le_bytes(fixed[26..28].try_into().unwrap()) as usize;
    let extra_len = u16::from_le_bytes(fixed[28..30].try_into().unwrap()) as usize;
    let name_end = LOCAL_HEADER_BYTES
        .checked_add(name_len)
        .ok_or("selected local name range overflow")?;
    let data_start = name_end
        .checked_add(extra_len)
        .ok_or("selected local extra range overflow")?;
    let name = header
        .get(LOCAL_HEADER_BYTES..name_end)
        .ok_or("selected local filename is truncated")?;
    if name != central.raw_name.as_bytes()
        || flags != central.flags
        || method != central.method
        || flags & 9 != 0
        || crc32 != central.crc32
        || u64::from(compressed) != central.compressed_size
        || u64::from(uncompressed) != central.uncompressed_size
    {
        return Err(format!(
            "selected local metadata disagrees with central header: {}",
            central.raw_name
        ));
    }
    if data_start != header.len() {
        return Err("captured local-header length disagrees with its fields".to_string());
    }
    Ok(())
}

impl CatalogGuard {
    fn insert(&mut self, name: &str) -> Result<(), String> {
        let canonical = canonical_name(name)?;
        if !self.exact.insert(name.to_string()) {
            return Err(format!("duplicate exact ZIP member name: {name}"));
        }
        if !self.canonical.insert(canonical.clone()) {
            return Err(format!("duplicate canonical ZIP member name: {name}"));
        }
        if !self.casefold.insert(canonical.to_ascii_lowercase()) {
            return Err(format!("case-fold duplicate ZIP member name: {name}"));
        }
        Ok(())
    }
}

pub(super) fn canonical_name(name: &str) -> Result<String, String> {
    if name.is_empty()
        || !name.is_ascii()
        || name.contains(['\0', '\\', ':'])
        || name.starts_with('/')
    {
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

fn le_u16(bytes: &[u8], offset: usize) -> Result<u16, String> {
    let value = bytes.get(offset..offset + 2).ok_or("truncated ZIP u16")?;
    Ok(u16::from_le_bytes(value.try_into().unwrap()))
}

fn le_u32(bytes: &[u8], offset: usize) -> Result<u32, String> {
    let value = bytes.get(offset..offset + 4).ok_or("truncated ZIP u32")?;
    Ok(u32::from_le_bytes(value.try_into().unwrap()))
}

fn le_u64(bytes: &[u8], offset: usize) -> Result<u64, String> {
    let value = bytes.get(offset..offset + 8).ok_or("truncated ZIP u64")?;
    Ok(u64::from_le_bytes(value.try_into().unwrap()))
}
