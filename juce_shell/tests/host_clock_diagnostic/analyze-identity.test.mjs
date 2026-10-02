import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { analyzeIdentity, decodeIdentity, readTrace } from './analyze-identity.mjs';

const bits = f => { const b = Buffer.alloc(4); b.writeFloatLE(f); return b.readUInt32LE(); };
const encode = id => {
  const value = (Math.imul(id, 0x9e3779b1) ^ 0x80008000) >>> 0;
  return [bits(((value & 0xffff) - 32768) / 4194304), bits(((value >>> 16) - 32768) / 4194304)];
};
const row = (id, auxiliary, index = 0) => {
  const a = encode(id), b = encode(id + 127);
  return { index, project: (id - 1) % 256, auxiliary, frames: 128, channels: 2, rate: 48000,
    flags: 1 | 2 | 4 | 8 | 64, aux_source: 1,
    first_left: a[0], first_right: a[1], last_left: b[0], last_right: b[1] };
};
const trace = rows => ({ file: 'synthetic-only', sha256: 'synthetic-only', rows });

test('diagnostic decoder preserves frame identities and rejects silence/nonfinite/out-of-range', () => {
  for (const id of [1, 65535, 65536, 123456789, 0x80000000, 0xffffffff])
    assert.equal(decodeIdentity(...encode(id)), id);
  for (const pair of [[0, 0], [bits(NaN), 0], [bits(Infinity), 0], [bits(0.25), 0], [bits(1e-20), 0]])
    assert.equal(decodeIdentity(...pair), null);
});
test('one-lap clock error cannot hide behind repeating project positions', () => {
  const source = trace(Array.from({ length: 8 }, (_, n) => row(n * 128 + 1, 10000 + n * 128, n)));
  const correct = trace([row(257, 10256), row(385, 10384, 1)]);
  const report = analyzeIdentity(source, correct);
  assert.deepEqual(report.contentClockOffsets, { 0: 2 });
  assert.equal(report.bothBoundariesLocated, 2);
  assert.equal(report.firstPlayingLooping, true);
  const wrong = trace(correct.rows.map(r => ({ ...r, auxiliary: r.auxiliary + 256 })));
  assert.deepEqual(analyzeIdentity(source, wrong).contentClockOffsets, { 256: 2 });
  assert.throws(() => analyzeIdentity(trace([row(1, 10000), row(1, 10128, 1)]), correct), /overlap/);
  const corrupt = { ...row(1, 10000), first_left: bits(NaN), last_left: bits(NaN) };
  assert.throws(() => analyzeIdentity(trace([corrupt, ...source.rows.slice(1)]), correct), /malformed/);
  const unavailable = trace([row(4097, 14096)]);
  assert.equal(analyzeIdentity(source, unavailable).sourceNotObserved, 1);
  const missing = trace(correct.rows.map(r => ({ ...r, flags: r.flags & ~8 })));
  assert.equal(analyzeIdentity(source, missing).clockMissing, 2);
  const broken = structuredClone(correct);
  [broken.rows[0].last_left, broken.rows[0].last_right] = encode(1000);
  assert.equal(analyzeIdentity(source, broken).identitySpanMismatch, 1);
});

test('identity trace schema rejects malformed, missing, nonfinite and inconsistent rows', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-identity-parser-'));
  const file = path.join(directory, 'fixture.csv');
  const fields = 'index,project,auxiliary,rate,frames,channels,flags,input_latency,output_latency,aux_source,presentation_source,first_left,last_left,first_right,last_right,host_ns,ppq,bpm,loop_start,loop_end';
  const line = '0,0,123,48000,128,2,79,0,0,1,1,0,0,0,0,123456789,0.0,120.0,0.0,0.25';
  const valid = fields + '\n' + line + '\n';
  try {
    fs.writeFileSync(file, valid);
    assert.equal(readTrace(file).rows.length, 1);
    const todValid = fields + ',tod_samples\n' + line.replace(',79,', ',4175,') + ',-1024\n';
    fs.writeFileSync(file, todValid);
    assert.equal(readTrace(file).rows[0].tod_samples, -1024);
    for (const text of [fields + '\n', valid.replace('host_ns', 'wrong'),
      valid.replace('48000', ''), valid.replace('48000', 'NaN'), valid.replace('128,2', '0,2'),
      valid.replace('128,2', '128,1'), valid.replace('120.0', 'Infinity'),
      valid.replace('0,0,123', '1,0,123'), valid.replace('123456789', '9007199254740992'),
      valid + line.replace('0,0,123', '1,0,251').replace('48000', '44100') + '\n',
      valid.replace(',79,', ',4175,'), todValid.replace(',-1024\n', ',1.5\n'),
      todValid.replace(',-1024\n', ',9007199254740992\n')]) {
      fs.writeFileSync(file, text);
      assert.throws(() => readTrace(file));
    }
  } finally {
    fs.rmSync(directory, { recursive: true }); // exclusive test-owned temporary fixture only
  }
});

test('AAX TOD is a separate raw observation, never substituted for the native clock', () => {
  const source = trace(Array.from({ length: 8 }, (_, n) => ({ ...row(n * 128 + 1, n * 128, n),
    flags: 1 | 2 | 4 | 8 | 64 | 4096, tod_samples: 10000 + n * 128 })));
  const dest = trace([row(257, 0), row(385, 128, 1)].map((r, n) => ({ ...r,
    flags: r.flags | 4096, tod_samples: 10768 + n * 128 })));
  const report = analyzeIdentity(source, dest);
  assert.deepEqual(report.contentClockOffsets, { '-256': 2 });
  assert.deepEqual(report.todContentOffsets, { 512: 2 });
  assert.equal(report.todMissing, 0);
  assert.equal(report.runs[0].todJumps, 0);
  const missing = trace(dest.rows.map(r => ({ ...r, flags: r.flags & ~4096 })));
  assert.equal(analyzeIdentity(source, missing).todMissing, 2);
  assert.deepEqual(analyzeIdentity(source, missing).todContentOffsets, {});
  const jumped = structuredClone(dest);
  jumped.rows[1].tod_samples += 256;
  assert.equal(analyzeIdentity(source, jumped).runs[0].todJumps, 1);
  assert.deepEqual(analyzeIdentity(source, jumped).todContentOffsets, { 512: 1, 768: 1 });
  const noAuxiliary = trace(dest.rows.map(r => ({ ...r, flags: r.flags & ~8 })));
  assert.equal(analyzeIdentity(source, noAuxiliary).clockMissing, 2);
  assert.deepEqual(analyzeIdentity(source, noAuxiliary).todContentOffsets, { 512: 2 });
});

const audited = (id, clock, index = 0) => ({ ...row(id, clock, index), flags: 79 | 2 | 8192 | 16384,
  add_clock_samples: clock, identity_first: id, identity_last: id + 127,
  identity_frames: 128, silent_prefix: 0, identity_errors: 0 });

test('AddClock and full-frame evidence remain distinct from TOD and boundary-only traces', () => {
  const source = trace(Array.from({ length: 8 }, (_, n) => audited(n * 128 + 1, 10000 + n * 128, n)));
  const dest = trace([audited(257, 10256), audited(385, 10384, 1)]);
  const report = analyzeIdentity(source, dest);
  assert.deepEqual(report.addClockContentOffsets, { 0: 2 });
  assert.equal(report.fullFrameVerifiedFrames, 256);
  assert.equal(report.fullFrameVerifiedBlocks, 2);
  assert.equal(report.todMissing, 2);
  const interiorFault = structuredClone(dest);
  interiorFault.rows[0].identity_errors = 1; // boundaries stay correct
  const rejected = analyzeIdentity(source, interiorFault);
  assert.equal(rejected.interiorErrors, 1);
  assert.equal(rejected.fullFrameVerifiedFrames, 128);
  const wrongLap = trace(dest.rows.map(r => ({ ...r, add_clock_samples: r.add_clock_samples + 256 })));
  assert.deepEqual(analyzeIdentity(source, wrongLap).addClockContentOffsets, { 256: 2 });
  const missing = trace(dest.rows.map(r => ({ ...r, flags: r.flags & ~8192 })));
  assert.equal(analyzeIdentity(source, missing).addClockMissing, 2);
  const old = trace(dest.rows.map(r => ({ ...r, flags: r.flags & ~16384 })));
  assert.equal(analyzeIdentity(source, old).fullFrameVerifiedFrames, 0);
  assert.equal(analyzeIdentity(source, old).fullFrameUnavailable, 2);
  const split = { ...audited(257, 10239), identity_frames: 111, silent_prefix: 17 };
  [split.first_left, split.first_right] = [0, 0];
  split.identity_last = 367;
  [split.last_left, split.last_right] = encode(367);
  const startup = analyzeIdentity(source, trace([split]));
  assert.equal(startup.fullFrameVerifiedFrames, 111);
  assert.deepEqual(startup.addClockContentOffsets, { 0: 1 });
  const corruptSource = structuredClone(source);
  corruptSource.rows[1].identity_errors = 1;
  assert.throws(() => analyzeIdentity(corruptSource, dest), /malformed/);
});

test('new schema requires explicit full-frame and AddClock fields and bounded audit counts', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-full-parser-'));
  const file = path.join(directory, 'fixture.csv');
  const fields = 'index,project,auxiliary,rate,frames,channels,flags,input_latency,output_latency,aux_source,presentation_source,first_left,last_left,first_right,last_right,host_ns,ppq,bpm,loop_start,loop_end,tod_samples,add_clock_samples,identity_first,identity_last,identity_frames,silent_prefix,identity_errors';
  const line = '0,0,123,48000,128,2,24655,0,0,3,0,0,0,0,0,0,0,120,0,0.25,0,-1024,1,128,128,0,0';
  try {
    fs.writeFileSync(file, fields + '\n' + line + '\n');
    assert.equal(readTrace(file).rows[0].add_clock_samples, -1024);
    for (const text of [line.replace(',-1024,', ',1.5,'), line.replace(',128,0,0', ',129,0,0'),
      line.replace(',128,0,0', ',128,1,0'), line.replace(',128,0,0', ',128,0,-1')]) {
      fs.writeFileSync(file, fields + '\n' + text + '\n');
      assert.throws(() => readTrace(file));
    }
  } finally { fs.rmSync(directory, { recursive: true }); }
});

test('audited endpoints never hide an unaudited source callback inside a larger destination block', () => {
  const source = trace(Array.from({ length: 8 }, (_, n) => audited(n * 128 + 1, 10000 + n * 128, n)));
  const block = { ...audited(129, 10128), frames: 384, identity_frames: 384, identity_last: 512 };
  [block.last_left, block.last_right] = encode(512);
  assert.equal(analyzeIdentity(source, trace([block])).fullFrameVerifiedFrames, 384);
  source.rows[2].flags &= ~16384; // first and last source callbacks remain fully audited
  const report = analyzeIdentity(source, trace([block]));
  assert.equal(report.bothBoundariesLocated, 1);
  assert.equal(report.fullFrameVerifiedFrames, 0);
  assert.equal(report.fullFrameUnavailable, 1);
});
