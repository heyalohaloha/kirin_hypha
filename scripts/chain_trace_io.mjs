import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

export function readTrace(folder) {
  const meta = fs.readFileSync(path.join(folder, 'blocks.jsonl'));
  const audio = fs.readFileSync(path.join(folder, 'audio.f32'));
  const records = meta.toString('utf8').trim().split('\n').map(line => JSON.parse(line));
  const header = records.shift(), end = records.at(-1);
  if (header?.type !== 'header' || header.schema !== 1 || header.pcm !== 'f32le')
    throw new Error('Unsupported trace schema or PCM byte order');
  const rows = records.filter(row => row.type === 'block');
  let expectedOffset = 0, nextSerial = 1, missing = 0;
  for (const r of rows) {
    for (const value of [r.serial, r.localFrames, r.pcmOffset, r.frames, r.channels])
      if (!Number.isSafeInteger(value)) throw new Error('Unsafe integer in trace');
    if (r.serial < nextSerial || r.frames < 1 || r.frames > header.maximumFrames
        || ![1, 2].includes(r.channels) || r.pcmOffset !== expectedOffset)
      throw new Error('Invalid trace sequence, dimensions or offset');
    missing += r.serial - nextSerial; nextSerial = r.serial + 1;
    expectedOffset += r.frames * r.channels * 8;
    if (expectedOffset > audio.length) throw new Error('Truncated PCM');
    r.sample = (side, channel, frame) => audio.readFloatLE(
      r.pcmOffset + ((side * r.channels + channel) * r.frames + frame) * 4);
    r.bits = (side, channel, frame) => audio.readUInt32LE(
      r.pcmOffset + ((side * r.channels + channel) * r.frames + frame) * 4);
  }
  const complete = end?.type === 'end' && end.written === rows.length
    && end.callbacks === rows.length + end.dropped && end.dropped === 0 && missing === 0
    && expectedOffset === audio.length;
  return { rows, integrity: { complete, missing, dropped: end?.dropped ?? null,
    cleanClose: end?.type === 'end', bytes: audio.length },
  hashes: { metadata: crypto.createHash('sha256').update(meta).digest('hex'),
    pcm: crypto.createHash('sha256').update(audio).digest('hex') } };
}

// Exact inverse of the separate diagnostic source, NOT audio-content correlation or product K.
// Every frame is decoded; repeated music/sine/silence is not accepted as an occurrence oracle.
export function identity(row, side, frame) {
  const left = row.sample(side, 0, frame);
  if (row.channels === 1) {
    const id = left * 536870912;
    return Number.isInteger(id) && id > 0 && id <= 4194303 ? id : null;
  }
  const l = left * 4194304 + 32768, r = row.sample(side, 1, frame) * 4194304 + 32768;
  if (![l, r].every(v => Number.isInteger(v) && v >= 0 && v <= 65535)) return null;
  const id = Math.imul(((r << 16) | l) ^ 0x80008000, 0x0e8b2f51) >>> 0;
  return id || null;
}

export function identityIndex(rows) {
  const spans = [];
  for (const row of rows) {
    for (let frame = 0; frame < row.frames; frame++) {
      const id = identity(row, 0, frame);
      if (id === null) continue;
      const tail = spans.at(-1);
      if (tail?.row === row && id === tail.end) tail.end++;
      else spans.push({ first: id, end: id + 1, frame, row });
    }
  }
  spans.sort((a, b) => a.first - b.first);
  for (let i = 1; i < spans.length; i++)
    if (spans[i].first < spans[i - 1].end) throw new Error('Repeated PRE identity: oracle is not unique');
  return id => {
    if (id === null) return null;
    let low = 0, high = spans.length;
    while (low < high) {
      const mid = (low + high) >>> 1;
      if (spans[mid].first <= id) low = mid + 1; else high = mid;
    }
    const span = spans[low - 1];
    return span && id < span.end ? { row: span.row, frame: span.frame + id - span.first } : null;
  };
}

export const nanosMs = (a, b) => Number(BigInt(a) - BigInt(b)) / 1e6;

export function annotate(rows) {
  let previous, lap = 0, epoch = 0, loopSamples = 0;
  for (const r of rows) {
    const b = r.block, p = previous;
    const reasons = [];
    if (p) {
      const step = b.project - p.block.project;
      const sameLoop = b.loopActive && p.block.loopActive && b.loopValid && p.block.loopValid
        && b.loopStart === p.block.loopStart && b.loopEnd === p.block.loopEnd && b.bpm === p.block.bpm;
      const length = (b.loopEnd - b.loopStart) * 60 * r.rate / b.bpm;
      const wrapped = sameLoop && step < 0 && Math.abs(p.frames - step - length) <= 1;
      if (wrapped) { lap++; loopSamples = p.frames - step; }
      else if (b.playing && p.block.playing && step !== p.frames) reasons.push('projectJump');
      if (r.serial !== p.serial + 1) reasons.push('traceGap');
      if (b.playing !== p.block.playing) reasons.push('transport');
      if (r.reset !== p.reset) reasons.push('hostReset');
      if (r.rate !== p.rate || r.channels !== p.channels) reasons.push('format');
      if (r.bypassed !== p.bypassed || r.offline !== p.offline) reasons.push('processingMode');
      for (const key of ['clockValid', 'basis', 'authority', 'outputValid', 'outputLatency'])
        if (b[key] !== p.block[key]) reasons.push(key);
      if (b.clockValid && p.block.clockValid && b.clock !== p.block.clock + p.frames)
        reasons.push('continuousJump');
      if (b.loopActive !== p.block.loopActive || (b.loopActive && !sameLoop)) reasons.push('loopRange');
    }
    if (b.gap) reasons.push('callbackGap');
    // This shadow uses a local counter even when the shipping host clock is invalid. Apply
    // the observed wall-gap fence to that hypothesis; do not inherit VST3's exemption.
    if (r.wallGap) reasons.push('localCounterWallGap');
    if (reasons.length) { epoch++; lap = 0; loopSamples = 0; }
    r.lap = lap; r.epoch = epoch; r.measuredLoop = loopSamples; r.clockEvents = reasons;
    previous = r;
  }
}

export function callbackStats(rows) {
  const gaps = rows.slice(1).map((r, i) => nanosMs(r.entryNanos, rows[i].entryNanos)).sort((a, b) => a - b);
  const quantile = fraction => gaps.length ? gaps[Math.floor((gaps.length - 1) * fraction)] : null;
  return { callbacks: rows.length, wallGapFlags: rows.filter(r => r.wallGap).length,
    threads: [...new Set(rows.map(r => r.block.thread).filter(v => v !== undefined))],
    callbackIntervalMs: { min: quantile(0), median: quantile(0.5), p95: quantile(0.95), max: quantile(1) } };
}
