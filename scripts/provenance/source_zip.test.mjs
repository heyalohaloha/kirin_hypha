import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, rmSync, openSync, closeSync, ftruncateSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { deflateRawSync } from 'node:zlib';
import { readSourceZip } from './source_zip.mjs';

// Fixture CRC uses the bit-at-a-time definition, independent of the reader's table.
function checksum(data) {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function extra(id, data = Buffer.alloc(0)) {
  const header = Buffer.alloc(4);
  header.writeUInt16LE(id, 0);
  header.writeUInt16LE(data.length, 2);
  return Buffer.concat([header, data]);
}

// Construct ZIP records directly: no OS archive program, extraction, or repository input.
function archive(specs, options = {}) {
  const localParts = [options.prefix ?? Buffer.alloc(0)], records = [];
  let localOffset = localParts[0].length;
  for (const spec of specs) {
    const raw = Buffer.isBuffer(spec.name) ? spec.name : Buffer.from(spec.name);
    const data = Buffer.from(spec.data ?? 'source bytes');
    const method = spec.method ?? 0, flags = spec.flags ?? (spec.descriptor ? 8 : 0);
    const version = spec.version ?? 20;
    const compressed = spec.compressed ?? (method === 8 ? deflateRawSync(data) : data);
    const expanded = spec.expanded ?? data.length, crc = spec.crc ?? checksum(data);
    const localName = spec.localName ?? raw;
    const localExtra = spec.localExtra ?? Buffer.alloc(0), centralExtra = spec.centralExtra ?? Buffer.alloc(0);
    const header = Buffer.alloc(30);
    header.writeUInt32LE(0x04034b50, 0);
    header.writeUInt16LE(version, 4);
    header.writeUInt16LE(flags, 6);
    header.writeUInt16LE(method, 8);
    header.writeUInt32LE(spec.descriptor ? 0 : crc, 14);
    header.writeUInt32LE(spec.descriptor ? 0 : compressed.length, 18);
    header.writeUInt32LE(spec.descriptor ? 0 : expanded, 22);
    header.writeUInt16LE(localName.length, 26);
    header.writeUInt16LE(localExtra.length, 28);
    const descriptor = Buffer.alloc(spec.descriptor ? (spec.descriptor === 'unsigned' ? 12 : 16) : 0);
    if (descriptor.length) {
      const values = descriptor.length === 16 ? 4 : 0;
      if (values) descriptor.writeUInt32LE(0x08074b50);
      descriptor.writeUInt32LE(crc, values);
      descriptor.writeUInt32LE(compressed.length, values + 4);
      descriptor.writeUInt32LE(expanded, values + 8);
    }
    const central = Buffer.alloc(46);
    central.writeUInt32LE(0x02014b50, 0);
    central.writeUInt16LE(0x0314, 4); // Unix creator; ZIP 2.0.
    central.writeUInt16LE(version, 6);
    central.writeUInt16LE(flags, 8);
    central.writeUInt16LE(method, 10);
    central.writeUInt32LE(crc, 16);
    central.writeUInt32LE(compressed.length, 20);
    central.writeUInt32LE(expanded, 24);
    central.writeUInt16LE(raw.length, 28);
    central.writeUInt16LE(centralExtra.length, 30);
    const directory = raw.at(-1) === 47;
    const mode = spec.mode ?? (directory ? 0o040755 : 0o100644);
    central.writeUInt32LE(((mode << 16) | (directory ? 0x10 : 0)) >>> 0, 38);
    central.writeUInt32LE(localOffset, 42);
    const payloadAt = localOffset + header.length + localName.length + localExtra.length;
    const size = header.length + localName.length + localExtra.length + compressed.length + descriptor.length;
    records.push({ localOffset, payloadAt, descriptorAt: payloadAt + compressed.length,
      central, raw, centralExtra, centralOffset: 0 });
    localParts.push(header, localName, localExtra, compressed, descriptor);
    localOffset += size;
  }
  const centralStart = localOffset, centralParts = [];
  const ordered = options.reverseCentral ? records.toReversed() : records;
  for (const record of ordered) {
    record.centralOffset = localOffset;
    centralParts.push(record.central, record.raw, record.centralExtra);
    localOffset += record.central.length + record.raw.length + record.centralExtra.length;
  }
  const comment = options.comment ?? Buffer.alloc(0), end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(records.length, 8);
  end.writeUInt16LE(records.length, 10);
  end.writeUInt32LE(localOffset - centralStart, 12);
  end.writeUInt32LE(centralStart, 16);
  end.writeUInt16LE(comment.length, 20);
  return { bytes: Buffer.concat([...localParts, ...centralParts, end, comment]), records,
    centralStart, endAt: localOffset };
}
function file(t, bytes) {
  const directory = mkdtempSync(join(tmpdir(), 'hypha-source-zip-'));
  t.after(() => rmSync(directory, { recursive: true, force: true }));
  const path = join(directory, 'source.zip');
  writeFileSync(path, bytes);
  return path;
}
function rejects(t, built, pattern = /^Source ZIP:/) {
  assert.throws(() => readSourceZip(file(t, built.bytes ?? built)), error => {
    assert.match(error.message, pattern);
    return true;
  });
}

test('store/deflate, binary and UTF-8 files retain exact bytes; directories are skipped', t => {
  const binary = Buffer.from([0, 1, 255, 128, 13, 10]);
  const built = archive([
    { name: 'source/', data: '' },
    { name: 'source/a.rs', data: 'fn main() {}\n' },
    { name: 'source/data.bin', data: binary, method: 8 },
    { name: 'source/計測.md', data: '計測 source\n', method: 8, flags: 0x0800 },
  ], { reverseCentral: true, comment: Buffer.from('source archive comment') });
  const files = readSourceZip(file(t, built.bytes));
  assert.ok(files instanceof Map);
  assert.deepEqual([...files.keys()], ['source/a.rs', 'source/data.bin', 'source/計測.md']);
  assert.deepEqual(files.get('source/data.bin'), binary);
  assert.deepEqual(files.get('source/a.rs'), Buffer.from('fn main() {}\n'));
  assert.deepEqual(files.get('source/計測.md'), Buffer.from('計測 source\n'));
});
test('standard CRC32 vector and empty files are accepted', t => {
  assert.equal(checksum(Buffer.from('123456789')), 0xcbf43926);
  const files = readSourceZip(file(t, archive([
    { name: 'crc.txt', data: '123456789', crc: 0xcbf43926,
      localExtra: extra(0x000d, Buffer.alloc(12)), centralExtra: extra(0x000d, Buffer.alloc(12)) },
    { name: 'empty', data: '', method: 8 },
  ]).bytes));
  assert.equal(files.get('crc.txt').toString(), '123456789');
  assert.equal(files.get('empty').length, 0);
});
test('empty archive with a full-length EOCD comment is accepted', t => {
  assert.equal(readSourceZip(file(t, archive([], { comment: Buffer.alloc(65535, 65) }).bytes)).size, 0);
});
for (const descriptor of ['signed', 'unsigned']) {
  test(`${descriptor} data descriptors validate exact bytes`, t => {
    const files = readSourceZip(file(t, archive([
      { name: 'streamed.rs', data: 'streamed source', method: 8, descriptor },
    ]).bytes));
    assert.equal(files.get('streamed.rs').toString(), 'streamed source');
  });
}

for (const name of ['../escape', '/absolute', 'C:/drive', 'a\\b', 'a/../b', './a', 'a//b',
  'a/./b', 'a:', 'a\0b', 'a\u001fb', '/']) {
  test(`unsafe path ${JSON.stringify(name)} is rejected`, t => rejects(t, archive([{ name }]), /unsafe entry path/));
}
test('non-ASCII name without UTF-8 flag is rejected', t => {
  rejects(t, archive([{ name: '計測.rs' }]), /UTF-8 flagged or ASCII/);
});
test('invalid flagged UTF-8 is rejected', t => {
  rejects(t, archive([{ name: Buffer.from([0xc3, 0x28]), flags: 0x0800 }]), /invalid UTF-8/);
});
test('alternate Unicode-name extra is rejected', t => {
  rejects(t, archive([{ name: 'a', centralExtra: extra(0x7075, Buffer.from('alternate')) }]), /alternate Unicode/);
});
for (const location of ['localExtra', 'centralExtra']) {
  for (const id of [0x000d, 0x756e]) {
    test(`${location} UNIX link-bearing extra ${id} is rejected despite regular central mode`, t => {
      const data = Buffer.concat([Buffer.alloc(12), Buffer.from('link-target')]);
      rejects(t, archive([{ name: 'regular.txt', mode: 0o100644, [location]: extra(id, data) }]), /UNIX.*extra unsupported/);
    });
  }
}
for (const names of [['same', 'same'], ['same/', 'same'], ['parent', 'parent/child']]) {
  test(`ambiguous namespace ${JSON.stringify(names)} is rejected`, t => {
    rejects(t, archive(names.map(name => ({ name, data: '' }))), /duplicate entry path|path collision/);
  });
}
for (const [mode, expected] of [[0o120777, /symlink/], [0o060644, /non-regular/], [0o140644, /non-regular/]]) {
  test(`non-regular mode ${mode.toString(8)} is rejected`, t => rejects(t, archive([{ name: 'a', mode }]), expected));
}
test('directory attributes require directory path and empty content', t => {
  rejects(t, archive([{ name: 'a', mode: 0o040755 }]), /directory attributes\/name/);
  rejects(t, archive([{ name: 'a/', mode: 0o100644, data: '' }]), /regular-file attributes/);
  rejects(t, archive([{ name: 'a/', data: 'hidden contents' }]), /non-empty directory/);
});

for (const flags of [1, 0x40, 0x2000]) {
  test(`encryption flag ${flags} is rejected`, t => rejects(t, archive([{ name: 'a', flags }]), /encrypted/));
}
for (const id of [0x0017, 0x9901]) {
  test(`encryption extra ${id} is rejected`, t => rejects(t, archive([{ name: 'a', localExtra: extra(id) }]), /encrypted/));
}
test('unsupported compression, flags and versions are rejected', t => {
  rejects(t, archive([{ name: 'a', method: 99 }]), /compression method/);
  rejects(t, archive([{ name: 'a', flags: 0x10 }]), /unsupported entry flags/);
  rejects(t, archive([{ name: 'a', flags: 2 }]), /deflate flags on stored/);
  rejects(t, archive([{ name: 'a', version: 45 }]), /version unsupported/);
});
test('ZIP64 local and central extras are rejected', t => {
  rejects(t, archive([{ name: 'a', localExtra: extra(1) }]), /ZIP64/);
  rejects(t, archive([{ name: 'a', centralExtra: extra(1) }]), /ZIP64/);
});
test('ZIP64 sentinel fields and split-disk records are rejected', t => {
  for (const field of ['size', 'offset', 'count', 'compressed', 'expanded', 'local', 'localSize', 'disk', 'entryDisk']) {
    const built = archive([{ name: 'a' }]), central = built.records[0].centralOffset;
    if (field === 'size') built.bytes.writeUInt32LE(0xffffffff, built.endAt + 12);
    if (field === 'offset') built.bytes.writeUInt32LE(0xffffffff, built.endAt + 16);
    if (field === 'count') built.bytes.writeUInt16LE(0xffff, built.endAt + 10);
    if (field === 'compressed') built.bytes.writeUInt32LE(0xffffffff, central + 20);
    if (field === 'expanded') built.bytes.writeUInt32LE(0xffffffff, central + 24);
    if (field === 'local') built.bytes.writeUInt32LE(0xffffffff, central + 42);
    if (field === 'localSize') built.bytes.writeUInt32LE(0xffffffff, 18);
    if (field === 'disk') built.bytes.writeUInt16LE(1, built.endAt + 4);
    if (field === 'entryDisk') built.bytes.writeUInt16LE(1, central + 34);
    rejects(t, built, /ZIP64|multi-disk/);
  }
});

test('modified stored data is rejected by CRC32', t => {
  const built = archive([{ name: 'a', data: 'checked source' }]);
  built.bytes[built.records[0].payloadAt] ^= 1;
  rejects(t, built, /CRC32 mismatch/);
});
test('header disagreement and changed descriptors are rejected', t => {
  rejects(t, archive([{ name: 'a', localName: Buffer.from('b') }]), /name mismatch/);
  for (const offset of [4, 6, 8, 14, 18, 22]) {
    const built = archive([{ name: 'a' }]);
    built.bytes[offset] ^= 1;
    rejects(t, built, /parameter mismatch|size or CRC mismatch/);
  }
  const streamed = archive([{ name: 'a', method: 8, descriptor: 'signed' }]);
  streamed.bytes[streamed.records[0].descriptorAt + 4] ^= 1;
  rejects(t, streamed, /data-descriptor mismatch/);
});
test('deflate corruption, truncation and unconsumed bytes are rejected', t => {
  const data = Buffer.from('repeatable source '.repeat(50)), compressed = deflateRawSync(data);
  rejects(t, archive([{ name: 'a', data, method: 8, compressed: Buffer.from([0x07]) }]), /deflate stream/);
  rejects(t, archive([{ name: 'a', data, method: 8, compressed: compressed.subarray(0, -1) }]), /deflate stream/);
  rejects(t, archive([{ name: 'a', data, method: 8, compressed: Buffer.concat([compressed, Buffer.from('extra')]) }]), /trailing compressed bytes/);
});
test('declared expanded size cannot hide actual output size', t => {
  rejects(t, archive([{ name: 'a', data: 'abcdefgh', expanded: 7 }]), /expanded size mismatch/);
  rejects(t, archive([{ name: 'a', data: 'abcdefgh', method: 8, expanded: 9 }]), /expanded size mismatch/);
  rejects(t, archive([{ name: 'a', data: 'a'.repeat(4096), method: 8, expanded: 1 }]), /oversized deflate/);
});
test('entry and aggregate limits are checked before expansion', t => {
  const limit = 128 * 1024 * 1024;
  rejects(t, archive([{ name: 'huge', expanded: limit + 1 }]), /entry size limit/);
  rejects(t, archive(Array.from({ length: 5 }, (_, index) => ({ name: `file${index}`, expanded: limit }))), /total expanded size limit/);
});
test('malformed and truncated directory, local header, extra and EOCD are rejected', t => {
  rejects(t, Buffer.alloc(0), /end record/);
  const normal = archive([{ name: 'a' }]);
  rejects(t, normal.bytes.subarray(0, -1), /end record/);
  for (const at of [0, normal.centralStart]) {
    const changed = Buffer.from(normal.bytes);
    changed[at] ^= 1;
    rejects(t, changed, /signature/);
  }
  const missingEntry = Buffer.from(normal.bytes);
  missingEntry.writeUInt16LE(2, normal.endAt + 8);
  missingEntry.writeUInt16LE(2, normal.endAt + 10);
  rejects(t, missingEntry, /truncated/);
  rejects(t, archive([{ name: 'a', centralExtra: Buffer.from([1, 2, 3]) }]), /truncated/);
  const tooLong = Buffer.from(normal.bytes);
  tooLong.writeUInt16LE(65535, normal.centralStart + 28);
  rejects(t, tooLong, /truncated/);
});
test('overlap, preamble, unreferenced local record and trailing archive bytes are rejected', t => {
  rejects(t, archive([{ name: 'a' }], { prefix: Buffer.from('preamble') }), /prefixed local records/);
  const overlapping = archive([{ name: 'a' }, { name: 'b' }]);
  overlapping.bytes.writeUInt32LE(0, overlapping.records[1].centralOffset + 42);
  rejects(t, overlapping, /overlapping/);
  const unreferenced = archive([{ name: 'a' }, { name: 'b' }]);
  const first = unreferenced.records[0], second = unreferenced.records[1];
  const centralBytes = unreferenced.bytes.subarray(second.centralOffset, unreferenced.endAt);
  const end = Buffer.from(unreferenced.bytes.subarray(unreferenced.endAt));
  end.writeUInt16LE(1, 8); end.writeUInt16LE(1, 10); end.writeUInt32LE(centralBytes.length, 12);
  rejects(t, Buffer.concat([unreferenced.bytes.subarray(0, unreferenced.centralStart), centralBytes, end]), /unreferenced|prefixed/);
  assert.equal(first.localOffset, 0);
  rejects(t, Buffer.concat([archive([{ name: 'a' }]).bytes, Buffer.from('garbage')]), /end record/);
});
test('oversized files are rejected using fstat before allocating archive memory', t => {
  const path = file(t, Buffer.alloc(0)), fd = openSync(path, 'r+');
  try { ftruncateSync(fd, (512 + 16) * 1024 * 1024 + 1); } finally { closeSync(fd); }
  assert.throws(() => readSourceZip(path), /archive size limit/);
});
test('unreadable/non-file inputs fail without exposing local path values', t => {
  const path = file(t, Buffer.alloc(0));
  assert.throws(() => readSourceZip(`${path}.missing`), error => error.message === 'Source ZIP: cannot read archive file');
  assert.throws(() => readSourceZip(join(path, '..')), /non-file input/);
});
