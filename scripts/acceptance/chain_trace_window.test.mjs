import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { readTrace } from '../chain_trace_io.mjs';
import { openTraceWindowed } from './chain_trace_window.mjs';

// A small trace written the way ChainTraceWriter does: per block, input then output, each
// channel's frames together (planar), float32 little-endian.
function writeTrace(t, blocks, { end = true, truncate = 0 } = {}) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-trace-window-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const lines = [JSON.stringify({ type: 'header', schema: 1, pcm: 'f32le', maximumFrames: 64 })];
  const pcm = [];
  let offset = 0, local = 0;
  blocks.forEach(({ frames, channels }, index) => {
    lines.push(JSON.stringify({ type: 'block', serial: index + 1, localFrames: local, pcmOffset: offset, frames, channels }));
    for (let side = 0; side < 2; side++) for (let channel = 0; channel < channels; channel++)
      for (let frame = 0; frame < frames; frame++) pcm.push(index * 1000 + side * 100 + channel * 10 + frame / 64);
    offset += frames * channels * 8; local += frames;
  });
  if (end) lines.push(JSON.stringify({ type: 'end', written: blocks.length, callbacks: blocks.length, dropped: 0 }));
  fs.writeFileSync(path.join(dir, 'blocks.jsonl'), `${lines.join('\n')}\n`);
  const bytes = Buffer.alloc(pcm.length * 4);
  pcm.forEach((value, index) => bytes.writeFloatLE(value, index * 4));
  fs.writeFileSync(path.join(dir, 'audio.f32'), bytes.subarray(0, bytes.length - truncate));
  return dir;
}

test('every windowed sample equals the whole-file reader, side and channel planar', t => {
  const dir = writeTrace(t, [{ frames: 32, channels: 2 }, { frames: 64, channels: 2 }, { frames: 16, channels: 1 }, { frames: 8, channels: 2 }]);
  const whole = readTrace(dir), trace = openTraceWindowed(dir);
  t.after(() => trace.close());
  assert.deepEqual(trace.integrity, whole.integrity);
  assert.deepEqual(trace.hashes, whole.hashes);
  for (const [first, last] of [[0, 3], [1, 2], [3, 3]]) {
    const rows = trace.window(first, last);
    rows.forEach((row, offset) => {
      const reference = whole.rows[first + offset];
      for (let side = 0; side < 2; side++) for (let channel = 0; channel < row.channels; channel++)
        for (let frame = 0; frame < row.frames; frame++) {
          assert.equal(row.sample(side, channel, frame), reference.sample(side, channel, frame));
          assert.equal(row.bits(side, channel, frame), reference.bits(side, channel, frame));
        }
    });
  }
  // Planar: the output's second channel of block 1, frame 5.
  assert.equal(trace.window(1)[0].sample(1, 1, 5), Math.fround(1000 + 100 + 10 + 5 / 64));
  assert.equal(trace.blockAtFrame(0), 0); assert.equal(trace.blockAtFrame(31), 0); assert.equal(trace.blockAtFrame(32), 1);
  assert.equal(trace.blockAtFrame(111), 2); assert.equal(trace.blockAtFrame(112), 3); assert.equal(trace.blockAtFrame(120), -1);
});

test('indices outside a block or the trace fail instead of reading neighbouring PCM', t => {
  const trace = openTraceWindowed(writeTrace(t, [{ frames: 4, channels: 1 }, { frames: 4, channels: 2 }]), { hashPcm: false });
  t.after(() => trace.close());
  assert.equal(trace.hashes.pcm, null);
  const [row] = trace.window(0);
  for (const args of [[0, 1, 0], [0, 0, 4], [2, 0, 0], [0, -1, 0]]) assert.throws(() => row.sample(...args), /outside the block/);
  for (const [first, last] of [[-1, 0], [1, 0], [0, 2], [2, 2]]) assert.throws(() => trace.window(first, last), /outside/);
});

test('truncated PCM, broken offsets and an unclosed trace are reported', t => {
  assert.throws(() => openTraceWindowed(writeTrace(t, [{ frames: 4, channels: 2 }], { truncate: 4 })), /Truncated/);
  const open = openTraceWindowed(writeTrace(t, [{ frames: 4, channels: 2 }], { end: false }));
  assert.equal(open.integrity.complete, false); assert.equal(open.integrity.cleanClose, false); open.close();
  const dir = writeTrace(t, [{ frames: 4, channels: 2 }, { frames: 4, channels: 2 }]);
  const meta = path.join(dir, 'blocks.jsonl');
  fs.writeFileSync(meta, fs.readFileSync(meta, 'utf8').replace('"pcmOffset":64', '"pcmOffset":60'));
  assert.throws(() => openTraceWindowed(dir), /offset/);
});
