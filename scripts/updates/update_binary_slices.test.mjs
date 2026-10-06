import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { universalFixture } from './update_binary_fixture.mjs';
import { universalSlices } from './update_binary_slices.mjs';
import { inspectUpdateBinary, updateBinding, assertPackageUpdateBinding } from './update_key_binding.mjs';

const key = `10001,${'f'.repeat(512)}`, digest = updateBinding(key).publicKeySha256;
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function fixture(t) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-update-fat-test-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const binary = path.join(directory, 'TEST-fixture-only');
  return (bytes, expectedKey = key, universal = true) => {
    fs.writeFileSync(binary, bytes);
    return inspectUpdateBinary(binary, expectedKey, { universal });
  };
}

test('both fat widths/endian variants bind both thin architectures and retain exact slice/container hashes', t => {
  const inspect = fixture(t);
  for (const wide of [false, true]) for (const little of [false, true]) for (const sliceLittle of [false, true]) {
    const bytes = universalFixture([digest, digest], { wide, little, sliceLittle });
    assert.deepEqual(inspect(bytes), { publicKeySha256: digest, binarySha256: hash(bytes), architectures: [
      { architecture: 'arm64', sha256: hash(bytes.subarray(256, 448)) },
      { architecture: 'x86_64', sha256: hash(bytes.subarray(768, 960)) },
    ] });
  }
});

test('aggregate markers cannot hide a missing, disabled or differently keyed architecture', t => {
  const inspect = fixture(t);
  for (const markers of [[digest, null], [null, digest], [digest, 'disabled'], ['a'.repeat(64), digest]]) {
    assert.throws(() => inspect(universalFixture(markers)), /architecture.*missing or mismatched/);
  }
  const outside = universalFixture([null, null]);
  outside.write(`KirinHyphaUpdateKeySha256=${digest};`, 100);
  assert.throws(() => inspect(outside), /missing/);
  assert.throws(() => inspect(Buffer.from(`KirinHyphaUpdateKeySha256=${digest};`)), /Universal/);
  assert.equal(inspect(universalFixture(), '').publicKeySha256, '');
  assert.throws(() => inspect(universalFixture(), key), /mismatched/);
});

test('fat architecture tables reject truncated, oversized, overlapping, duplicate and mismatched slices', () => {
  for (const mutate of [
    b => b.writeUInt32BE(1, 4),
    b => b.writeUInt32BE(0, 16),
    b => b.writeUInt32BE(0xffffffff, 20),
    b => b.writeUInt32BE(8, 20),
    b => b.writeUInt32BE(64, 24),
    b => b.writeUInt32BE(769, 36),
    b => b.writeUInt32BE(256, 36),
    b => { b.writeUInt32BE(384, 36); b.writeUInt32BE(7, 44);
      b.writeUInt32LE(0xfeedfacf, 384); b.writeUInt32LE(0x01000007, 388); },
    b => { b.writeUInt32BE(0x0100000c, 28); b.writeUInt32LE(0x0100000c, 772); },
    b => b.writeUInt32LE(0x01000007, 260),
    b => b.writeUInt32LE(0xfeedface, 256),
  ]) {
    const bytes = universalFixture([digest, digest]); mutate(bytes);
    assert.throws(() => universalSlices(bytes, true), /Universal|architecture/);
  }
  assert.throws(() => universalSlices(universalFixture().subarray(0, 10), true), /Universal/);
  const wide = universalFixture([digest, digest], { wide: true });
  wide.writeBigUInt64BE(2n ** 63n, 16);
  assert.throws(() => universalSlices(wide, true), /bounds/);
  const reserved = universalFixture([digest, digest], { wide: true });
  reserved.writeUInt32BE(1, 36);
  assert.throws(() => universalSlices(reserved, true), /bounds/);
});

test('Universal package provenance requires both unique architecture hashes for every role/format', () => {
  const value = { ...updateBinding(key), binaries: [{ role: 'PRE', format: 'AU',
    binarySha256: 'a'.repeat(64), publicKeySha256: digest,
    architectures: ['arm64', 'x86_64'].map(architecture => ({ architecture, sha256: 'b'.repeat(64) })) }] };
  const verify = () => assertPackageUpdateBinding(value, key, ['AU/PRE'], { universal: true });
  verify();
  value.binaries[0].architectures.pop(); assert.throws(verify, /architecture/);
  value.binaries[0].architectures.push({ architecture: 'arm64', sha256: 'b'.repeat(64) });
  assert.throws(verify, /architecture/);
  value.binaries[0].architectures[1] = { architecture: 'x86_64', sha256: '' };
  assert.throws(verify, /architecture/);
});
