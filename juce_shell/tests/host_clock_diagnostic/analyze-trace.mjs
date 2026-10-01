// Read-only protocol analysis: exact fixture sample bits, never waveform correlation.
import fs from 'node:fs';
import crypto from 'node:crypto';
import path from 'node:path';

const [fixturePath, ...tracePaths] = process.argv.slice(2);
if (!fixturePath || tracePaths.length === 0) throw new Error('Expected fixture.wav trace.csv [...]');
const wav = fs.readFileSync(fixturePath);
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
if (wav.toString('ascii', 0, 4) !== 'RIFF' || wav.toString('ascii', 8, 12) !== 'WAVE')
  throw new Error('Expected RIFF WAVE');
let data, format;
for (let at = 12; at + 8 <= wav.length;) {
  const size = wav.readUInt32LE(at + 4);
  if (at + 8 + size > wav.length) throw new Error('Truncated WAV chunk');
  const chunk = wav.subarray(at + 8, at + 8 + size);
  const name = wav.toString('ascii', at, at + 4);
  if (name === 'fmt ') format = chunk;
  if (name === 'data') data = chunk;
  at += 8 + size + (size & 1);
}
if (!format || format.length < 16 || format.readUInt16LE(0) !== 3
    || format.readUInt16LE(2) !== 2 || format.readUInt16LE(14) !== 32
    || !data || data.length % 8 !== 0) throw new Error('Expected stereo float32 fixture');
const rate = format.readUInt32LE(4);
const frames = data.length / 8;
const bitsAt = sample => [data.readUInt32LE(sample * 8), data.readUInt32LE(sample * 8 + 4)];
const key = (left, right) => `${left},${right}`;
const positions = new Map();
for (let i = 0; i < frames; i++) {
  const k = key(...bitsAt(i));
  positions.set(k, positions.has(k) ? null : i); // ambiguous boundaries are not matches
}
const header = 'index,project,auxiliary,rate,frames,channels,flags,input_latency,output_latency,aux_source,presentation_source,first_left,last_left,first_right,last_right';
const extendedHeader = header + ',host_ns,ppq,bpm,loop_start,loop_end';
const todHeader = extendedHeader + ',tod_samples';
const fullHeader = todHeader + ',add_clock_samples,identity_first,identity_last,identity_frames,silent_prefix,identity_errors';
const count = (map, value) => { map[value] = (map[value] || 0) + 1; };
const results = tracePaths.map(file => {
  const bytes = fs.readFileSync(file);
  const lines = bytes.toString('utf8').trimEnd().split(/\r?\n/);
  const observedHeader = lines.shift();
  if (![header, extendedHeader, todHeader, fullHeader].includes(observedHeader))
    throw new Error(`Unexpected trace schema: ${file}`);
  const fields = observedHeader.split(',');
  const rows = lines.map((line, index) => {
    const cells = line.split(',');
    if (cells.some((cell, i) => !(i >= 16 && i <= 19 ? /^-?\d+(\.\d+)?$/ : /^-?\d+$/).test(cell)))
      throw new Error(`Invalid number: ${index}`);
    const values = cells.map(Number);
    if (values.length !== fields.length || values.some((v, i) =>
      i >= 16 && i <= 19 ? !Number.isFinite(v) : !Number.isSafeInteger(v)))
      throw new Error(`Invalid numeric row: ${index}`);
    const row = Object.fromEntries(fields.map((f, i) => [f, values[i]]));
    if (row.index !== index || row.rate !== rate || row.channels !== 2 || row.frames <= 0)
      throw new Error(`Unexpected callback identity: ${index}`);
    return row;
  });
  const runs = [];
  let run, previous;
  for (const row of rows) {
    if (!(row.flags & 1)) { run = undefined; previous = undefined; continue; }
    if (!run) {
      run = { firstIndex: row.index, callbacks: 0, flags: {}, frames: {}, inputLatency: {},
        outputLatency: {}, matchedFirst: 0, matchedLast: 0, unmatchedFirst: 0,
        firstContentMinusProject: {}, lastContentMinusProjectEnd: {}, auxiliaryMinusProject: {},
        projectDiscontinuities: [], auxiliaryDiscontinuities: [] };
      runs.push(run);
    }
    run.callbacks++;
    count(run.flags, row.flags); count(run.frames, row.frames);
    count(run.inputLatency, row.flags & 16 ? row.input_latency : 'missing');
    count(run.outputLatency, row.flags & 32 ? row.output_latency : 'missing');
    if (row.flags & 8) count(run.auxiliaryMinusProject, row.auxiliary - row.project);
    const first = positions.get(key(row.first_left, row.first_right));
    const last = positions.get(key(row.last_left, row.last_right));
    if (Number.isInteger(first)) {
      run.matchedFirst++;
      count(run.firstContentMinusProject, first - row.project);
    } else run.unmatchedFirst++;
    if (Number.isInteger(last)) {
      run.matchedLast++;
      count(run.lastContentMinusProjectEnd, last - (row.project + row.frames - 1));
    }
    if (previous) {
      const evidence = { index: row.index, previousProject: previous.project,
        project: row.project, previousAuxiliary: previous.auxiliary,
        auxiliary: row.auxiliary, previousFrames: previous.frames };
      if (row.project !== previous.project + previous.frames)
        run.projectDiscontinuities.push(evidence);
      if ((row.flags & previous.flags & 8)
          && row.auxiliary !== previous.auxiliary + previous.frames)
        run.auxiliaryDiscontinuities.push(evidence);
    }
    previous = row;
  }
  const last = rows.at(-1);
  return { file: path.basename(file), sha256: sha256(bytes), callbacks: rows.length,
    capacityReached: rows.length === 65536,
    lastCallback: last && { playing: Boolean(last.flags & 1), project: last.project,
      auxiliary: last.auxiliary, flags: last.flags }, runs };
});
console.log(JSON.stringify({ fixture: { sha256: sha256(wav), rate, frames }, traces: results }, null, 2));
