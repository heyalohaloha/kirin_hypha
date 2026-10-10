// The bounded ZIP dialect accepted by source_zip.mjs; no directory or private extras.
import fs from 'node:fs';
import { deflateRawSync } from 'node:zlib';
import { crc32, readSourceZip } from './source_zip.mjs';

export function writeSourceZip(file, entries) {
  const sorted = [...entries].sort((a, b) => a.archive < b.archive ? -1 : a.archive > b.archive ? 1 : 0);
  if (sorted.length > 65535) throw new Error('Source ZIP entry limit exceeded');
  const names = new Set();
  const central = [];
  let expanded = 0, offset = 0;
  const fd = fs.openSync(file, 'wx', 0o600);
  let complete = false;
  try {
    const write = bytes => {
      let at = 0;
      while (at < bytes.length) at += fs.writeSync(fd, bytes, at, bytes.length - at);
      offset += bytes.length;
    };
    for (const entry of sorted) {
      const relative = entry.archive;
      if (!relative || /[\\:\x00-\x1f]/.test(relative) || relative.startsWith('/')
          || relative.split('/').some(p => !p || p === '.' || p === '..') || names.has(relative)) {
        throw new Error('Unsafe or duplicate Corresponding Source path');
      }
      names.add(relative);
      const bytes = fs.readFileSync(entry.file);
      expanded += bytes.length;
      if (bytes.length > 128 * 1024 * 1024 || expanded > 512 * 1024 * 1024) {
        throw new Error('Corresponding Source exceeds reader bounds');
      }
      const name = Buffer.from(relative, 'utf8');
      if (name.length > 65535) throw new Error('Source ZIP filename limit exceeded');
      const compressed = deflateRawSync(bytes);
      const crc = crc32(bytes), start = offset;
      const header = Buffer.alloc(30);
      header.writeUInt32LE(0x04034b50, 0);
      header.writeUInt16LE(20, 4); header.writeUInt16LE(0x0800, 6);
      header.writeUInt16LE(8, 8); header.writeUInt16LE(33, 12);
      header.writeUInt32LE(crc, 14); header.writeUInt32LE(compressed.length, 18);
      header.writeUInt32LE(bytes.length, 22); header.writeUInt16LE(name.length, 26);
      write(header); write(name); write(compressed);
      const record = Buffer.alloc(46);
      record.writeUInt32LE(0x02014b50, 0);
      record.writeUInt16LE(20, 4); record.writeUInt16LE(20, 6);
      record.writeUInt16LE(0x0800, 8); record.writeUInt16LE(8, 10);
      record.writeUInt16LE(33, 14); record.writeUInt32LE(crc, 16);
      record.writeUInt32LE(compressed.length, 20); record.writeUInt32LE(bytes.length, 24);
      record.writeUInt16LE(name.length, 28); record.writeUInt32LE(start, 42);
      central.push(record, name);
    }
    const start = offset;
    for (const record of central) write(record);
    const end = Buffer.alloc(22);
    end.writeUInt32LE(0x06054b50, 0);
    end.writeUInt16LE(sorted.length, 8); end.writeUInt16LE(sorted.length, 10);
    end.writeUInt32LE(offset - start, 12); end.writeUInt32LE(start, 16);
    write(end);
    fs.fsyncSync(fd);
    complete = true;
  } finally {
    fs.closeSync(fd);
    if (!complete) fs.rmSync(file);
  }
  const result = readSourceZip(file);
  if (result.size !== sorted.length) throw new Error('Written source inventory mismatch');
  for (const entry of sorted) {
    if (!result.get(entry.archive)?.equals(fs.readFileSync(entry.file))) {
      throw new Error(`Written source bytes mismatch: ${entry.archive}`);
    }
  }
  return result;
}
