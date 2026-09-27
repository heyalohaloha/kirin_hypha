import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const directory = path.dirname(fileURLToPath(import.meta.url));
const invoke = (script, ...args) => spawnSync(process.execPath,
  [path.join(directory, script), ...args], { encoding: 'utf8' });
const header = 'index,project,auxiliary,rate,frames,channels,flags,input_latency,output_latency,aux_source,presentation_source,first_left,last_left,first_right,last_right';

test('trace oracle distinguishes cycle wraps, missing latency, and malformed data', () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-clock-oracle-'));
  try {
    const fixture = path.join(temporary, 'fixture.wav');
    const trace = path.join(temporary, 'trace.csv');
    assert.equal(invoke('make-fixture.mjs', fixture).status, 0);
    assert.notEqual(invoke('make-fixture.mjs', fixture).status, 0, 'fixture must not overwrite');
    const wav = fs.readFileSync(fixture);
    const row = (index, project, auxiliary, flags) => {
      const first = 56 + project * 8;
      const last = first + 527 * 8;
      return [index, project, auxiliary, 48000, 528, 2, flags, 0, 0, 1, 1,
        wav.readUInt32LE(first), wav.readUInt32LE(last),
        wav.readUInt32LE(first + 4), wav.readUInt32LE(last + 4)].join(',');
    };
    const valid = [header, row(0, 0, 0, 77), row(1, 528, 528, 77),
      row(2, 0, 1056, 111), row(3, 528, 1584, 76)].join('\n') + '\n';
    fs.writeFileSync(trace, valid);
    const result = invoke('analyze-trace.mjs', fixture, trace);
    assert.equal(result.status, 0, result.stderr);
    const parsed = JSON.parse(result.stdout);
    assert.equal(parsed.fixture.frames, 1152000);
    assert.equal(parsed.traces[0].callbacks, 4);
    assert.equal(parsed.traces[0].lastCallback.playing, false);
    const run = parsed.traces[0].runs[0];
    assert.equal(run.matchedFirst, 3);
    assert.equal(run.matchedLast, 3);
    assert.deepEqual(run.firstContentMinusProject, { 0: 3 });
    assert.deepEqual(run.outputLatency, { 0: 1, missing: 2 });
    assert.equal(run.projectDiscontinuities.length, 1);
    assert.equal(run.auxiliaryDiscontinuities.length, 0);
    for (const malformed of [valid.replace('48000', ''), valid.replace('48000', 'NaN'),
      valid.replace('48000', '44100'), valid.replace('0,0,0,48000', '7,0,0,48000'),
      valid.replace('first_left', 'wrong_schema')]) {
      fs.writeFileSync(trace, malformed);
      assert.notEqual(invoke('analyze-trace.mjs', fixture, trace).status, 0);
    }
    fs.writeFileSync(trace, valid);
    const truncated = path.join(temporary, 'truncated.wav');
    fs.writeFileSync(truncated, wav.subarray(0, 100));
    assert.notEqual(invoke('analyze-trace.mjs', truncated, trace).status, 0);
  } finally {
    // Only this test's freshly created, exclusive temporary directory is removed.
    fs.rmSync(temporary, { recursive: true });
  }
});
