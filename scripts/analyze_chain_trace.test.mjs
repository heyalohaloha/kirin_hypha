import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { analyze, loopCandidate } from './analyze_chain_trace.mjs';
import { annotate, identity, identityIndex, readTrace } from './chain_trace_io.mjs';

function encoded(id) {
  const v = (Math.imul(id, 0x9e3779b1) ^ 0x80008000) >>> 0;
  return [(v & 65535) - 32768, (v >>> 16) - 32768].map(n => n / 4194304);
}
function row(role, index, shift = 0, outputShift = 0) {
  const localFrames = index * 16;
  return { type: 'block', role, serial: index + 1, localFrames, frames: 16, rate: 48000, channels: 2,
    entryNanos: String(index * 1e6 + role * 1000), exitNanos: String(index * 1e6 + role * 1000 + 500),
    reset: 0, offline: false, bypassed: false, contentHeld: false,
    block: { project: localFrames, frames: 16, playing: true, projectValid: true,
      clockValid: false, basis: 0, authority: 3, loopActive: false, loopValid: true,
      loopStart: 0, loopEnd: 1, bpm: 120, gap: false },
    decision: { seen: false }, render: {}, preGain: 1,
    sample: (side, channel, frame) => encoded(localFrames + frame + 1 + shift + side * outputShift)[channel],
    bits(side, channel, frame) { const b = Buffer.alloc(4); b.writeFloatLE(this.sample(side, channel, frame)); return b.readUInt32LE(); } };
}
const trace = rows => ({ rows, integrity: { complete: true }, hashes: {} });

test('oracle inverts all bits, rejects silence and missing mono IDs', () => {
  const r = row(0, 0);
  for (let f = 0; f < 16; f++) assert.equal(identity(r, 0, f), f + 1);
  r.sample = () => 0; assert.equal(identity(r, 0, 0), null);
  r.channels = 1; r.sample = () => 4194303 / 536870912;
  assert.equal(identity(r, 0, 0), 4194303);
  r.sample = () => 4194304 / 536870912; assert.equal(identity(r, 0, 0), null);
});
test('identity lookup rejects repeated loop content as a unique occurrence', () => {
  assert.throws(() => identityIndex([row(0, 0), row(0, 0)]), /not unique/);
});
test('linear clocks agree with every frame and do not claim PRE was exercised', () => {
  const result = analyze(trace(Array.from({length: 12}, (_, i) => row(0, i))),
    trace(Array.from({length: 12}, (_, i) => row(1, i))));
  assert.equal(result.summary.oracleFrames, 192);
  assert.equal(result.summary.shadowFrames, 80);
  assert.equal(result.summary.shadowWrongFrames, 0);
  assert.equal(result.summary.changedBlocks, 0);
  assert.equal(result.summary.wrongPreFrames, null);
});
test('audio changes first: M1 detection latency and wrong shadow frames are measured', () => {
  const pre = Array.from({length: 20}, (_, i) => row(0, i));
  const post = Array.from({length: 20}, (_, i) => row(1, i, i >= 10 ? -16 : 0));
  for (let i = 12; i < post.length; i++) post[i].block.project -= 16;
  const r = analyze(trace(pre), trace(post));
  assert.equal(r.changes[0].serial, 11);
  assert.equal(r.changes[0].detectionSerial, 13);
  assert.equal(r.changes[0].detectionAudioMs, 32 * 1000 / 48000);
  assert.equal(r.summary.shadowWrongFrames, 32);
});
test('unreported delay is right censored, never converted to zero latency', () => {
  const pre = Array.from({length: 14}, (_, i) => row(0, i));
  const post = Array.from({length: 14}, (_, i) => row(1, i, i >= 10 ? -16 : 0));
  const r = analyze(trace(pre), trace(post));
  assert.equal(r.changes[0].censored, true);
  assert.equal(r.changes[0].detectionAudioMs, null);
});
test('an unrelated stop cannot detect an unreported delay, even with coincident reset and M1', () => {
  const pre = Array.from({length: 24}, (_, i) => row(0, i));
  const post = Array.from({length: 24}, (_, i) => row(1, i, i >= 10 ? -16 : 0));
  for (let i = 20; i < 24; i++) {
    post[i].block.playing = false; post[i].reset = 1;
    post[i].decision = { m1: true, timelineChanged: true };
  }
  const result = analyze(trace(pre), trace(post));
  assert.equal(result.changes[0].censored, true);
  assert.equal(result.changes[0].detectionSerial, null);
  assert.equal(result.changes[0].detectionAudioMs, null);
  assert.equal(result.changes[0].censorSerial, 21);
});
test('a later playback run cannot supply detection for the previous run', () => {
  const pre = Array.from({length: 28}, (_, i) => row(0, i));
  const post = Array.from({length: 28}, (_, i) => row(1, i, i >= 10 ? -16 : 0));
  post[20].block.playing = false;
  for (let i = 22; i < 28; i++) post[i].block.project += 4096;
  const result = analyze(trace(pre), trace(post));
  assert.equal(result.changes[0].censored, true);
  assert.equal(result.changes[0].detectionSerial, null);
});
test('a fixture setting before playback is not detected by transport start', () => {
  const pre = Array.from({length: 14}, (_, i) => row(0, i));
  const post = Array.from({length: 14}, (_, i) => row(1, i));
  for (let i = 0; i < 5; i++) pre[i].block.playing = post[i].block.playing = false;
  const delay = Array.from({length: 14}, (_, i) => ({ ...row(2, i),
    fixtureGeneration: i < 2 ? 1 : 2, fixtureDelay: i < 2 ? 0 : 16, fixtureReported: 16 }));
  const result = analyze(trace(pre), trace(post), trace(delay));
  assert.equal(result.fixtureChanges[0].detectionSerial, null);
  assert.equal(result.fixtureChanges[0].playing, false);
});
test('silence during delay reconfiguration widens the onset bound instead of hiding the window', () => {
  const pre = Array.from({length: 15}, (_, i) => row(0, i));
  const post = Array.from({length: 15}, (_, i) => row(1, i, i >= 12 ? -16 : 0));
  post[10].sample = post[11].sample = () => 0;
  post[11].reset = 1;
  for (let i = 12; i < post.length; i++) post[i].reset = 1;
  const r = analyze(trace(pre), trace(post));
  assert.equal(r.changes[0].onsetUncertain, true);
  assert.equal(r.changes[0].onsetLowerFrame, 160);
  assert.equal(r.changes[0].detectionAudioMs, 0);
  assert.equal(r.changes[0].detectionAudioMsUpper, 16 * 1000 / 48000);
});
test('interior wrong PRE is counted, even if block endpoints are correct', () => {
  const p = row(1, 1), original = p.sample;
  p.decision.seen = true; p.render = { preAudible: true, stable: true, gainSettled: true };
  p.sample = (s, c, f) => s === 1 && f === 8 ? encoded(5)[c] : original(s, c, f);
  const r = analyze(trace([row(0, 0), row(0, 1)]), trace([p]));
  assert.equal(r.summary.wrongPreFrames, 1);
  assert.equal(r.summary.changedBlocks, 1);
});
test('mixed/gained output is unknown, never reported as zero wrong PRE', () => {
  const p = row(1, 1); p.decision.seen = true;
  p.render = { preAudible: true, stable: false, gainSettled: false };
  assert.equal(analyze(trace([row(0, 1)]), trace([p])).summary.unknownPreFrames, 16);
});
test('a fade ending at POST retains unclassified frames even with preAudible false', () => {
  const p = row(1, 1, 0, -1); p.decision.seen = true;
  p.render = { preAudible: false, stable: false, gainSettled: true };
  const result = analyze(trace([row(0, 1)]), trace([p]));
  assert.equal(result.summary.unclassifiedRenderFrames, 16);
  assert.equal(result.summary.unclassifiedRenderBlocks, 1);
  assert.equal(result.summary.changedBlocks, 1);
  assert.equal(result.summary.wrongPreFrames, null);
});
test('first-loop alias needs one bounded candidate; both edges remain explicit', () => {
  assert.deepEqual(loopCandidate(100, 16, 1000, 16, 999), {k: 100, candidates: 1});
  assert.deepEqual(loopCandidate(100, 16, 1000, 16, 1016), {k: null, candidates: 2});
  assert.equal(loopCandidate(100, 16, 1000, 16, 15).k, null);
  assert.equal(loopCandidate(100, 16, 0, 16, 100).k, null);
  assert.deepEqual(loopCandidate(-900, -984, 1000, 16, 999), {k: 100, candidates: 1});
});
test('measured lap differs from unknown backward seek, anticipative wall time is no clock', () => {
  const rows = [row(0, 0), row(0, 1)];
  for (const r of rows) { r.block.loopActive = true; r.block.loopEnd = 32 / 24000; }
  rows[0].block.project = 16;
  rows[1].block.project = 0;
  annotate(rows); assert.equal(rows[1].lap, 1); assert.equal(rows[1].measuredLoop, 32);
  rows[1].block.project = -7;
  annotate(rows); assert.equal(rows[1].lap, 0); assert.ok(rows[1].clockEvents.includes('projectJump'));
});
test('future PRE writes cannot prove already played output', () => {
  const pre = row(0, 0); pre.exitNanos = '5000000';
  assert.equal(analyze(trace([pre]), trace([row(1, 0)])).summary.oracleFrames, 0);
});
test('a wall gap invalidates the local-counter shadow even if the shipping VST3 basis ignores it', () => {
  const pre = Array.from({length: 12}, (_, i) => row(0, i));
  const post = Array.from({length: 12}, (_, i) => row(1, i));
  post[9].wallGap = true;
  const r = analyze(trace(pre), trace(post));
  assert.equal(r.callbacks[9].shadowK, null);
  assert.ok(r.callbacks[9].reasons.includes('localCounterWallGap'));
  assert.equal(r.scheduling.post.wallGapFlags, 1);
});
test('PRE bypass or POST offline cannot retain an eligible shadow range', () => {
  const pre = Array.from({length: 16}, (_, i) => row(0, i));
  const post = Array.from({length: 16}, (_, i) => row(1, i));
  pre[10].bypassed = true; post[14].offline = true;
  const r = analyze(trace(pre), trace(post));
  assert.equal(r.callbacks[10].shadowK, null);
  assert.equal(r.callbacks[14].shadowK, null);
});
test('trace parser rejects truncation and requires a clean zero-drop footer', () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-chain-trace-test-'));
  try {
    const r = row(0, 0); r.pcmOffset = 0;
    const header = {type: 'header', schema: 1, pcm: 'f32le', maximumFrames: 8192};
    const write = end => fs.writeFileSync(path.join(root, 'blocks.jsonl'),
      [header, r, ...end].map(x => JSON.stringify(x)).join('\n') + '\n');
    fs.writeFileSync(path.join(root, 'audio.f32'), Buffer.alloc(256));
    write([]); assert.equal(readTrace(root).integrity.complete, false);
    write([{type: 'end', written: 1, callbacks: 1, dropped: 0}]);
    assert.equal(readTrace(root).integrity.complete, true);
    write([{type: 'end', written: 1, callbacks: 2, dropped: 1}]);
    assert.equal(readTrace(root).integrity.complete, false);
    fs.writeFileSync(path.join(root, 'audio.f32'), Buffer.alloc(4));
    assert.throws(() => readTrace(root), /Truncated/);
  } finally { fs.rmSync(root, {recursive: true, force: true}); }
});
