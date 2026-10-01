// Diagnostic only. Decode emission identities independently of host clocks / Consumer / K.
// New traces audit every stereo frame in the non-shipping observer. Old traces remain
// boundary-only evidence and are never promoted to full-frame/host qualification.
import fs from 'node:fs';
import crypto from 'node:crypto';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const header = 'index,project,auxiliary,rate,frames,channels,flags,input_latency,output_latency,aux_source,presentation_source,first_left,last_left,first_right,last_right,host_ns,ppq,bpm,loop_start,loop_end';
const todHeader = header + ',tod_samples';
const fullHeader = todHeader + ',add_clock_samples,identity_first,identity_last,identity_frames,silent_prefix,identity_errors';
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const count = (map, key) => { map[key] = (map[key] ?? 0) + 1; };

export function readTrace(file) {
  const bytes = fs.readFileSync(file);
  const lines = bytes.toString('utf8').trimEnd().split(/\r?\n/);
  const observedHeader = lines.shift();
  if (![header, todHeader, fullHeader].includes(observedHeader) || lines.length === 0 || lines.length > 65536)
    throw new Error('Invalid identity trace schema / count');
  const fields = observedHeader.split(',');
  const rows = lines.map((line, index) => {
    const cells = line.split(',');
    if (cells.length !== fields.length || cells.some((v, i) =>
      !(i >= 16 && i <= 19 ? /^-?\d+(\.\d+)?$/ : /^-?\d+$/).test(v)))
      throw new Error(`Invalid numeric syntax at row ${index}`);
    const values = cells.map(Number);
    if (values.some((v, i) => i >= 16 && i <= 19 ? !Number.isFinite(v) : !Number.isSafeInteger(v)))
      throw new Error(`Invalid number at row ${index}`);
    const row = Object.fromEntries(fields.map((f, i) => [f, values[i]]));
    if (row.index !== index || row.rate <= 0 || row.frames <= 0 || row.channels !== 2
      || ((row.flags & 4096) && !Number.isSafeInteger(row.tod_samples))
      || ((row.flags & 8192) && !Number.isSafeInteger(row.add_clock_samples))
      || ((row.flags & 16384) && (observedHeader !== fullHeader
        || ['identity_first', 'identity_last', 'identity_frames', 'silent_prefix', 'identity_errors']
          .some(f => row[f] < 0 || row[f] > 0xffffffff)
        || row.identity_frames + row.silent_prefix > row.frames
        || row.identity_errors > row.frames))
      || values.slice(11, 15).some(v => v < 0 || v > 0xffffffff))
      throw new Error(`Invalid callback at row ${index}`);
    return row;
  });
  if (rows.some(r => r.rate !== rows[0].rate)) throw new Error('Sample rate changed within trace');
  return { file: path.basename(file), sha256: hash(bytes), rows };
}

export function decodeIdentity(leftBits, rightBits) {
  const value = bits => {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32LE(bits);
    const f = bytes.readFloatLE();
    const integer = f * 4194304 + 32768;
    if (!Number.isInteger(integer) || integer < 0 || integer > 65535
      || f !== (integer - 32768) / 4194304) return null;
    return integer;
  };
  const l = value(leftBits), r = value(rightBits);
  if (l === null || r === null) return null;
  const packed = (((r << 16) | l) ^ 0x80008000) >>> 0;
  const id = Math.imul(packed, 0x0e8b2f51) >>> 0;
  return id === 0 ? null : id; // silence is not an emission
}

export function analyzeIdentity(source, destination) {
  if (source.rows[0].rate !== destination.rows[0].rate) throw new Error('Source/destination rate mismatch');
  const spans = [];
  let unverified = 0;
  const identity = row => row.flags & 16384
    ? { first: row.identity_first || null, last: row.identity_last || null,
      frames: row.identity_frames, prefix: row.silent_prefix, errors: row.identity_errors,
      full: row.identity_errors === 0 && row.identity_frames + row.silent_prefix === row.frames }
    : { first: decodeIdentity(row.first_left, row.first_right),
      last: decodeIdentity(row.last_left, row.last_right), frames: row.frames, prefix: 0, errors: 0, full: false };
  for (const row of source.rows) {
    if (!(row.flags & 1) || !(row.flags & 64)) continue;
    const audit = identity(row);
    const { first, last } = audit;
    // Only exact silence is an unarmed/finished source. Corrupt/non-grid samples
    // must not disappear from the evidence just because both fail to decode.
    if (row.first_left === 0 && row.first_right === 0
      && row.last_left === 0 && row.last_right === 0 && (!(row.flags & 16384) || audit.prefix === row.frames)) continue;
    if (first === null || audit.errors || last !== first + audit.frames - 1
      || (spans.length && first !== spans.at(-1).last + 1)) throw new Error('Source identities overlap / are malformed / have gaps');
    const unverifiedBefore = unverified;
    if (!audit.full) ++unverified;
    spans.push({ first, last, row, audit, unverifiedBefore, unverifiedThrough: unverified });
  }
  if (!spans.length) throw new Error('No source emission evidence');
  const locate = id => {
    let lo = 0, hi = spans.length;
    while (lo < hi) {
      const mid = (lo + hi) >>> 1;
      if (spans[mid].last < id) lo = mid + 1; else hi = mid;
    }
    return lo < spans.length && spans[lo].first <= id ? spans[lo] : undefined;
  };
  const result = { source: { file: source.file, sha256: source.sha256, emittedBlocks: spans.length },
    destination: { file: destination.file, sha256: destination.sha256 },
    rate: source.rows[0].rate, callbacks: 0, loopingCallbacks: 0, firstPlayingLooping: null,
    bothBoundariesLocated: 0, invalidOrSilent: 0, sourceNotObserved: 0,
    identitySpanMismatch: 0, clockMissing: 0, kDisagreementsWithinBlock: 0,
    fullFrameVerifiedBlocks: 0, fullFrameVerifiedFrames: 0, interiorErrors: 0, fullFrameUnavailable: 0,
    contentClockOffsets: {}, todContentOffsets: {}, todMissing: 0, todWithinBlockDisagreements: 0,
    addClockContentOffsets: {}, addClockMissing: 0, addClockWithinBlockDisagreements: 0,
    clockSources: {}, frames: {}, outputLatency: {}, runs: [] };
  let previous, run;
  for (const row of destination.rows) {
    if (!(row.flags & 1)) { previous = run = undefined; continue; }
    if (!run) {
      run = { firstIndex: row.index, initiallyLooping: !!(row.flags & 2), callbacks: 0,
        projectWraps: 0, auxiliaryJumps: 0, todJumps: 0, addClockJumps: 0,
        offsets: {}, todOffsets: {}, addClockOffsets: {} };
      result.runs.push(run);
    }
    ++result.callbacks; ++run.callbacks;
    if (result.firstPlayingLooping === null) result.firstPlayingLooping = !!(row.flags & 2);
    if (row.flags & 2) ++result.loopingCallbacks;
    count(result.clockSources, row.aux_source); count(result.frames, row.frames);
    count(result.outputLatency, row.flags & 32 ? row.output_latency : 'missing');
    if (previous) {
      if (row.project < previous.project) ++run.projectWraps;
      if ((row.flags & previous.flags & 8) && row.auxiliary !== previous.auxiliary + previous.frames)
        ++run.auxiliaryJumps;
      if ((row.flags & previous.flags & 4096) && row.tod_samples !== previous.tod_samples + previous.frames)
        ++run.todJumps;
      if ((row.flags & previous.flags & 8192) && row.add_clock_samples !== previous.add_clock_samples + previous.frames)
        ++run.addClockJumps;
    }
    previous = row;
    const audit = identity(row);
    const { first, last } = audit;
    if (audit.errors) { ++result.interiorErrors; continue; }
    if (!(row.flags & 64) || first === null || last === null) { ++result.invalidOrSilent; continue; }
    if (last !== first + audit.frames - 1) { ++result.identitySpanMismatch; continue; }
    const a = locate(first), b = locate(last);
    if (!a || !b) { ++result.sourceNotObserved; continue; }
    ++result.bothBoundariesLocated;
    // A destination block can span several source callbacks. Audited endpoints
    // cannot promote an unobserved interior source callback to full-frame evidence.
    if (audit.full && b.unverifiedThrough === a.unverifiedBefore) {
      ++result.fullFrameVerifiedBlocks;
      result.fullFrameVerifiedFrames += audit.frames;
    } else ++result.fullFrameUnavailable;
    if (row.flags & a.row.flags & b.row.flags & 8192) {
      const start = a.row.add_clock_samples + a.audit.prefix + first - a.first;
      const end = b.row.add_clock_samples + b.audit.prefix + last - b.first;
      const offset = row.add_clock_samples + audit.prefix - start;
      if (row.add_clock_samples + row.frames - 1 - end !== offset) ++result.addClockWithinBlockDisagreements;
      else { count(result.addClockContentOffsets, offset); count(run.addClockOffsets, offset); }
    } else ++result.addClockMissing;
    if (row.flags & a.row.flags & b.row.flags & 4096) {
      const firstTod = a.row.tod_samples + a.audit.prefix + first - a.first;
      const lastTod = b.row.tod_samples + b.audit.prefix + last - b.first;
      const offset = row.tod_samples + audit.prefix - firstTod;
      if (row.tod_samples + row.frames - 1 - lastTod !== offset) ++result.todWithinBlockDisagreements;
      else { count(result.todContentOffsets, offset); count(run.todOffsets, offset); }
    } else ++result.todMissing;
    if (!(row.flags & a.row.flags & b.row.flags & 8)) { ++result.clockMissing; continue; }
    const firstClock = a.row.auxiliary + a.audit.prefix + first - a.first;
    const lastClock = b.row.auxiliary + b.audit.prefix + last - b.first;
    const k = row.auxiliary + audit.prefix - firstClock;
    if (row.auxiliary + row.frames - 1 - lastClock !== k) {
      ++result.kDisagreementsWithinBlock; continue;
    }
    count(result.contentClockOffsets, k); count(run.offsets, k);
  }
  return result;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const [source, destination, ...extra] = process.argv.slice(2);
  if (!source || !destination || extra.length) throw new Error('Expected source.csv destination.csv');
  console.log(JSON.stringify(analyzeIdentity(readTrace(source), readTrace(destination)), null, 2));
}
