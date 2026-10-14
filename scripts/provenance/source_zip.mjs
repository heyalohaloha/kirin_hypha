// Source ZIP subset: PKWARE APPNOTE 6.3.10 sections 4.3/4.4/4.5.7.
// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
// zlib bounds/consumed-input API: https://nodejs.org/api/zlib.html
// This reader never extracts files. Unsupported or ambiguous layouts fail closed.
import { openSync, fstatSync, readSync, closeSync } from 'node:fs';
import { inflateRawSync } from 'node:zlib';
import { TextDecoder } from 'node:util';

const ENTRY_LIMIT = 128 * 1024 * 1024;
const TOTAL_LIMIT = 512 * 1024 * 1024;
const ARCHIVE_LIMIT = TOTAL_LIMIT + 16 * 1024 * 1024;
const UTF8 = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true });
const CRC_TABLE = Uint32Array.from({ length: 256 }, (_, byte) => {
  let value = byte;
  for (let bit = 0; bit < 8; bit++) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  return value >>> 0;
});

function fail(message) { throw new Error(`Source ZIP: ${message}`); }
export function crc32(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) value = (value >>> 8) ^ CRC_TABLE[(value ^ byte) & 0xff];
  return (value ^ 0xffffffff) >>> 0;
}
function span(bytes, start, length, limit = bytes.length) {
  if (!Number.isSafeInteger(start) || start < 0 || length < 0 || start + length > limit)
    fail('truncated or overlapping record');
  return bytes.subarray(start, start + length);
}
function u16(bytes, offset) { return span(bytes, offset, 2).readUInt16LE(); }
function u32(bytes, offset) { return span(bytes, offset, 4).readUInt32LE(); }

function load(file) {
  let fd;
  try {
    fd = openSync(file, 'r');
    const before = fstatSync(fd);
    if (!before.isFile() || before.size > ARCHIVE_LIMIT) fail('archive size limit or non-file input');
    const bytes = Buffer.allocUnsafe(before.size);
    let offset = 0;
    while (offset < bytes.length) {
      const count = readSync(fd, bytes, offset, bytes.length - offset, offset);
      if (!count) fail('file changed or truncated while reading');
      offset += count;
    }
    const after = fstatSync(fd);
    if (after.size !== before.size || after.mtimeMs !== before.mtimeMs || after.ctimeMs !== before.ctimeMs)
      fail('file changed while reading');
    return bytes;
  } catch (error) {
    if (error.message.startsWith('Source ZIP:')) throw error;
    fail('cannot read archive file');
  } finally {
    if (fd !== undefined) closeSync(fd);
  }
}

function endRecord(bytes) {
  for (let at = bytes.length - 22; at >= Math.max(0, bytes.length - 22 - 65535); at--) {
    if (u32(bytes, at) !== 0x06054b50 || at + 22 + u16(bytes, at + 20) !== bytes.length) continue;
    const count = u16(bytes, at + 10), size = u32(bytes, at + 12), offset = u32(bytes, at + 16);
    if (count === 0xffff || size === 0xffffffff || offset === 0xffffffff) fail('ZIP64 unsupported');
    if (u16(bytes, at + 4) || u16(bytes, at + 6) || u16(bytes, at + 8) !== count)
      fail('split or multi-disk ZIP unsupported');
    if (offset + size !== at) fail('central-directory boundary mismatch');
    return { count, offset, end: at };
  }
  fail('missing or truncated end record');
}

function entryName(raw, flags) {
  if (!raw.length || (!(flags & 0x0800) && raw.some(byte => byte > 0x7f)))
    fail('entry names must be UTF-8 flagged or ASCII');
  let name;
  try { name = UTF8.decode(raw); } catch { fail('invalid UTF-8 entry name'); }
  const directory = name.endsWith('/');
  const key = directory ? name.slice(0, -1) : name;
  if (!key || key.startsWith('/') || /[\\:\u0000-\u001f\u007f-\u009f]/u.test(key)
      || key.split('/').some(part => !part || part === '.' || part === '..'))
    fail('unsafe entry path');
  return { name, key, directory };
}

function extras(bytes) {
  let at = 0;
  while (at < bytes.length) {
    const id = u16(bytes, at), size = u16(bytes, at + 2);
    span(bytes, at + 4, size);
    if (id === 1) fail('ZIP64 unsupported');
    if (id === 0x7075) fail('alternate Unicode entry-name extra unsupported');
    if (id === 0x0017 || id === 0x9901) fail('encrypted ZIP unsupported');
    // UNIX variable data can name a link/device even when central mode says regular.
    if (id === 0x000d && size !== 12) fail('UNIX link/device extra unsupported');
    if (id === 0x756e) fail('ASi UNIX extra unsupported');
    at += 4 + size;
  }
}

function parameters(version, flags, method) {
  if (version > 20) fail('ZIP version unsupported (including ZIP64)');
  if (flags & (0x0001 | 0x0040 | 0x2000)) fail('encrypted ZIP unsupported');
  if (flags & ~0x080e) fail('unsupported entry flags');
  if (method !== 0 && method !== 8) fail('compression method unsupported');
  if (method === 0 && (flags & 6)) fail('deflate flags on stored entry');
}

function centralEntries(bytes, end) {
  const entries = [], paths = new Map();
  let at = end.offset, declaredTotal = 0;
  for (let index = 0; index < end.count; index++) {
    span(bytes, at, 46, end.end);
    if (u32(bytes, at) !== 0x02014b50) fail('bad central-directory signature');
    const version = u16(bytes, at + 6), flags = u16(bytes, at + 8), method = u16(bytes, at + 10);
    parameters(version, flags, method);
    const crc = u32(bytes, at + 16), compressed = u32(bytes, at + 20), expanded = u32(bytes, at + 24);
    const nameLength = u16(bytes, at + 28), extraLength = u16(bytes, at + 30), commentLength = u16(bytes, at + 32);
    const local = u32(bytes, at + 42), mode = (u32(bytes, at + 38) >>> 16) & 0xf000;
    if (compressed === 0xffffffff || expanded === 0xffffffff || local === 0xffffffff)
      fail('ZIP64 unsupported');
    if (u16(bytes, at + 34)) fail('multi-disk entry unsupported');
    if (mode === 0xa000) fail('symlink entry unsupported');
    if (mode && mode !== 0x8000 && mode !== 0x4000) fail('non-regular entry unsupported');
    if (expanded > ENTRY_LIMIT || compressed > ENTRY_LIMIT + 1024 * 1024) fail('entry size limit');
    declaredTotal += expanded;
    if (declaredTotal > TOTAL_LIMIT) fail('total expanded size limit');
    const raw = span(bytes, at + 46, nameLength, end.end);
    const named = entryName(raw, flags);
    if ((mode === 0x4000 || (u32(bytes, at + 38) & 0x10)) && !named.directory)
      fail('directory attributes/name mismatch');
    if (mode === 0x8000 && named.directory) fail('regular-file attributes on directory');
    if (paths.has(named.key)) fail('duplicate entry path');
    paths.set(named.key, named.directory);
    extras(span(bytes, at + 46 + nameLength, extraLength, end.end));
    span(bytes, at + 46 + nameLength + extraLength, commentLength, end.end);
    entries.push({ ...named, raw, version, flags, method, crc, compressed, expanded, local });
    at += 46 + nameLength + extraLength + commentLength;
  }
  if (at !== end.end) fail('unexpected central-directory records');
  for (const key of paths.keys()) {
    const pieces = key.split('/');
    pieces.pop();
    while (pieces.length) {
      if (paths.get(pieces.join('/')) === false) fail('file/directory path collision');
      pieces.pop();
    }
  }
  return entries;
}

function localData(bytes, entry, centralStart) {
  const at = entry.local;
  span(bytes, at, 30, centralStart);
  if (u32(bytes, at) !== 0x04034b50) fail('bad local-header signature');
  if (u16(bytes, at + 4) !== entry.version || u16(bytes, at + 6) !== entry.flags
      || u16(bytes, at + 8) !== entry.method) fail('local/central parameter mismatch');
  const localCrc = u32(bytes, at + 14), localCompressed = u32(bytes, at + 18), localExpanded = u32(bytes, at + 22);
  if (localCompressed === 0xffffffff || localExpanded === 0xffffffff) fail('ZIP64 unsupported');
  const descriptor = Boolean(entry.flags & 8);
  for (const [actual, expected] of [[localCrc, entry.crc], [localCompressed, entry.compressed], [localExpanded, entry.expanded]]) {
    if (actual !== expected && !(descriptor && actual === 0)) fail('local/central size or CRC mismatch');
  }
  const nameLength = u16(bytes, at + 26), extraLength = u16(bytes, at + 28);
  if (!span(bytes, at + 30, nameLength, centralStart).equals(entry.raw)) fail('local/central name mismatch');
  extras(span(bytes, at + 30 + nameLength, extraLength, centralStart));
  const start = at + 30 + nameLength + extraLength;
  const data = span(bytes, start, entry.compressed, centralStart);
  let end = start + entry.compressed;
  if (descriptor) {
    const signed = u32(bytes, end) === 0x08074b50;
    const values = end + (signed ? 4 : 0);
    span(bytes, values, 12, centralStart);
    if (u32(bytes, values) !== entry.crc || u32(bytes, values + 4) !== entry.compressed
        || u32(bytes, values + 8) !== entry.expanded) fail('data-descriptor mismatch');
    end = values + 12;
  }
  return { data, end };
}

/** Return validated file entries as Map(relative POSIX path, Buffer); directories are skipped. */
export function readSourceZip(file) {
  const bytes = load(file), end = endRecord(bytes), entries = centralEntries(bytes, end);
  entries.sort((a, b) => a.local - b.local);
  const files = new Map();
  let cursor = 0, actualTotal = 0;
  for (const entry of entries) {
    if (entry.local !== cursor) fail('overlapping, unreferenced or prefixed local records');
    const local = localData(bytes, entry, end.offset);
    let expanded;
    if (entry.method === 0) expanded = local.data;
    else {
      try {
        const result = inflateRawSync(local.data, { info: true, maxOutputLength: entry.expanded + 1 });
        if (result.engine.bytesWritten !== local.data.length) fail('trailing compressed bytes');
        expanded = result.buffer;
      } catch (error) {
        if (error.message.startsWith('Source ZIP:')) throw error;
        fail('invalid or oversized deflate stream');
      }
    }
    actualTotal += expanded.length;
    if (expanded.length > ENTRY_LIMIT || actualTotal > TOTAL_LIMIT) fail('actual expanded size limit');
    if (expanded.length !== entry.expanded) fail('expanded size mismatch');
    if (crc32(expanded) !== entry.crc) fail('CRC32 mismatch');
    if (entry.directory && expanded.length) fail('non-empty directory entry');
    if (!entry.directory) files.set(entry.name, expanded);
    cursor = local.end;
  }
  if (cursor !== end.offset) fail('unreferenced data before central directory');
  return files;
}
