import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { collectRecords } from './collect_records.mjs';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-acceptance-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true })); return root;
}
// Independent encoder with hand-selected sample values, not the reader's decoder/formula.
function wav(bits, tag, bytes, { channels = 1, rate = 48000, extensible = false } = {}) {
  const fmt = Buffer.alloc(extensible ? 40 : 16);
  fmt.writeUInt16LE(extensible ? 65534 : tag, 0); fmt.writeUInt16LE(channels, 2);
  fmt.writeUInt32LE(rate, 4); fmt.writeUInt32LE(rate * channels * bits / 8, 8);
  fmt.writeUInt16LE(channels * bits / 8, 12); fmt.writeUInt16LE(bits, 14);
  if (extensible) { fmt.writeUInt16LE(22, 16); fmt.writeUInt16LE(bits, 18);
    Buffer.from(tag === 3 ? '0300000000001000800000aa00389b71' : '0100000000001000800000aa00389b71', 'hex').copy(fmt, 24); }
  const chunk = (name, data) => { const h = Buffer.alloc(8); h.write(name); h.writeUInt32LE(data.length, 4);
    return Buffer.concat([h, data, Buffer.alloc(data.length % 2)]); };
  const body = Buffer.concat([Buffer.from('WAVE'), chunk('JUNK', Buffer.from('x')), chunk('fmt ', fmt), chunk('data', bytes)]);
  const header = Buffer.alloc(8); header.write('RIFF'); header.writeUInt32LE(body.length, 4);
  return Buffer.concat([header, body]);
}
function compare(root, a, b) {
  fs.writeFileSync(path.join(root, 'a.wav'), a); fs.writeFileSync(path.join(root, 'b.wav'), b);
  const r = spawnSync('python3', [path.join(import.meta.dirname, 'wav_null.py'), path.join(root, 'a.wav'), path.join(root, 'b.wav')],
    { encoding: 'utf8' });
  return { status: r.status, result: r.stdout ? JSON.parse(r.stdout) : null };
}
const float32 = values => { const b = Buffer.alloc(values.length * 4); values.forEach((v, i) => b.writeFloatLE(v, i * 4)); return b; };
const float64 = values => { const b = Buffer.alloc(values.length * 8); values.forEach((v, i) => b.writeDoubleLE(v, i * 8)); return b; };

test('WAV null comparison accepts exact decoded samples across supported integer/float containers', t => {
  const root = fixture(t), expected = [0, 0.5, -0.5, -1];
  const reference = wav(64, 3, float64(expected));
  const encodings = [[16, 1, Buffer.from('0000004000c00080', 'hex')],
    [24, 1, Buffer.from('0000000000400000c0000080', 'hex')],
    [32, 1, Buffer.from('0000000000000040000000c000000080', 'hex')],
    [32, 3, float32(expected)], [64, 3, float64(expected)]];
  for (const [bits, tag, bytes] of encodings) {
    const { status, result } = compare(root, reference, wav(bits, tag, bytes, { extensible: true }));
    assert.equal(status, 0); assert.equal(result.frames, 4);
    assert.equal(result.mismatchedSamples, 0); assert.equal(result.nonFiniteSamples, 0); assert.equal(result.latencySamples, 0);
  }
});
test('one changed low double bit, a shifted signal and non-finite values cannot pass', t => {
  const root = fixture(t), ref = wav(64, 3, float64([0, 0.5, -0.5, -1]));
  const oneBit = compare(root, ref, wav(64, 3, float64([0, 0.5 + 2 ** -53, -0.5, -1])));
  assert.equal(oneBit.status, 1); assert.equal(oneBit.result.mismatchedSamples, 1);
  assert.equal(oneBit.result.latencySamples, null);
  const shifted = compare(root, ref, wav(64, 3, float64([-1, 0, 0.5, -0.5])));
  assert.equal(shifted.status, 1); assert.equal(shifted.result.mismatchedSamples, 4);
  const bad = wav(32, 3, float32([NaN, Infinity, -Infinity]));
  const r = compare(root, bad, bad); assert.equal(r.status, 1); assert.equal(r.result.nonFiniteSamples, 3);
});
test('WAV geometry, truncation, unsupported format and empty audio fail closed', t => {
  const root = fixture(t), ref = wav(32, 3, float32([0, 0.5]));
  for (const bad of [ref.subarray(0, ref.length - 1), wav(32, 3, float32([0, 0.5]), { rate: 96000 }),
    wav(32, 3, float32([0, 0.5]), { channels: 2 }), wav(32, 3, float32([0])),
    wav(8, 1, Buffer.from([0, 128])), wav(32, 3, Buffer.alloc(0))]) {
    assert.equal(compare(root, ref, bad).status, 2);
  }
});
test('record collection copies hidden and nested files, proves bytes and never overwrites or changes the source', t => {
  const root = fixture(t), source = path.join(root, 'closed'), output = path.join(root, 'evidence');
  fs.mkdirSync(path.join(source, 'nested'), { recursive: true });
  fs.writeFileSync(path.join(source, '.pair_committed'), 'closed');
  fs.writeFileSync(path.join(source, 'nested', 'record.json'), '{"closed":true}');
  const manifest = collectRecords(source, output);
  assert.deepEqual(manifest.files.map(r => r.path), ['.pair_committed', path.join('nested', 'record.json')]);
  for (const row of manifest.files) assert.deepEqual(fs.readFileSync(path.join(source, row.path)), fs.readFileSync(path.join(output, 'files', row.path)));
  assert.throws(() => collectRecords(source, output));
  assert.equal(JSON.parse(fs.readFileSync(path.join(output, 'manifest.json'))).schema, manifest.schema);
  assert.throws(() => collectRecords(source, path.join(source, 'nested', 'copy')));
  fs.symlinkSync(path.join(source, 'nested', 'record.json'), path.join(source, 'link'));
  assert.throws(() => collectRecords(source, path.join(root, 'rejected')));
  assert.equal(fs.existsSync(path.join(root, 'rejected')), false);
});

test('selected record copies require the exact previously validated bytes', async t => {
  const { createHash } = await import('node:crypto');
  const { collectSelectedRecords } = await import('./collect_records.mjs');
  const root = fixture(t), source = path.join(root, 'closed'); fs.mkdirSync(source);
  const file = path.join(source, 'record.json'); fs.writeFileSync(file, 'original');
  const fact = { path: file, sha256: createHash('sha256').update('original').digest('hex') };
  fs.writeFileSync(file, 'changed');
  const output = path.join(root, 'evidence');
  assert.throws(() => collectSelectedRecords(source, output, [fact]), /changed|hash/i);
  assert.equal(fs.existsSync(output), false);
});
