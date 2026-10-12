#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { readTrace, identity, identityIndex, annotate, nanosMs, callbackStats } from './chain_trace_io.mjs';

// Shadow hypothesis only. This selects no product audio and does not infer a physical delay
// bound from a ring size or elapsed wall time. maxAge is an explicit experiment input.
export function loopCandidate(base, ageAtBase, length, frames, maxAge) {
  if (!(length > 0 && maxAge >= frames)) return { k: null, candidates: null };
  const first = Math.ceil((frames - ageAtBase) / length);
  const last = Math.floor((maxAge - ageAtBase) / length);
  const candidates = Math.max(0, last - first + 1);
  return { k: candidates === 1 ? base + first * length : null, candidates };
}

// A stop (including its coincident reset/M1), mode change or trace hole ends the
// observation. A later playback run cannot retrospectively detect this change.
function detectionWindow(rows, detections, first) {
  const start = rows[first];
  const playing = !!start?.block.playing && !start.offline && !start.bypassed;
  if (!playing) return { playing, detection: null, censor: start };
  const censor = rows.slice(first).find(r => !r.block.playing || r.offline || r.bypassed
    || r.clockEvents.includes('traceGap') || r.clockEvents.includes('format'));
  const detection = detections.find(d => d.serial >= start.serial
    && (!censor || d.serial < censor.serial) && !d.reasons.includes('transport'));
  return { playing, detection, censor };
}

export function analyze(pre, post, delay = null, maxAge = null) {
  if (!pre.rows.length || !post.rows.length || pre.rows.some(r => r.role !== 0)
      || post.rows.some(r => r.role !== 1)) throw new Error('Choose one PRE and one POST instance');
  const rate = post.rows[0].rate, channels = post.rows[0].channels;
  if (!(rate > 0) || [...pre.rows, ...post.rows].some(r => r.rate !== rate || r.channels !== channels))
    throw new Error('Split runs at rate/layout changes; do not join unlike formats');
  annotate(pre.rows); annotate(post.rows);
  const lookup = identityIndex(pre.rows);
  const callbacks = [], events = [];
  let cursor = 0, latest, previous, k = null, streak = 0, lastCandidate = null;
  let oracleFrames = 0, unknownFrames = 0, changedBlocks = 0, actualPreBlocks = 0;
  let unclassifiedRenderBlocks = 0, unclassifiedRenderFrames = 0;
  let wrongPreFrames = 0, wrongPreBlocks = 0, unknownPreFrames = 0, shadowWrongFrames = 0, shadowFrames = 0;
  let maxObservedAge = null, maxResidenceMs = null, lastOracleK = null;
  let unknownSince = null;
  for (const row of post.rows) {
    while (cursor < pre.rows.length && BigInt(pre.rows[cursor].exitNanos) <= BigInt(row.entryNanos))
      latest = pre.rows[cursor++];
    const reasons = [...row.clockEvents];
    if (latest && previous && latest.epoch !== previous.preEpoch) reasons.push('preEpoch');
    if (row.decision?.m1) reasons.push('productM1');
    if (row.decision?.timelineChanged) reasons.push('productTimeline');
    if (row.contentHeld && !previous?.contentHeld) reasons.push('contentHold');
    if (reasons.length) { k = null; streak = 0; lastCandidate = null; }
    const base = latest ? (row.localFrames - row.block.project)
      - (latest.localFrames - latest.block.project) : null;
    let candidates = null;
    const eligible = latest && row.block.playing && latest.block.playing
      && row.block.projectValid && latest.block.projectValid && !row.offline && !row.bypassed
      && !latest.offline && !latest.bypassed;
    if (!eligible) { k = null; streak = 0; lastCandidate = null; }
    if (eligible && !row.block.loopActive && !latest.block.loopActive) {
      if (k !== null && k !== base) { reasons.push('shadowM1'); k = null; streak = 0; }
      streak = base === lastCandidate ? streak + 1 : 1; lastCandidate = base;
      const age = latest.localFrames + latest.frames - (row.localFrames - base);
      if (streak >= 8 && age >= row.frames && (maxAge === null || age <= maxAge)) k = base;
    } else if (eligible && row.block.loopActive && k === null && maxAge !== null
        && row.measuredLoop === latest.measuredLoop) {
      const candidate = loopCandidate(base,
        latest.localFrames + latest.frames - (row.localFrames - base), row.measuredLoop, row.frames, maxAge);
      candidates = candidate.candidates; k = candidate.k;
    }
    if (k !== null && latest && maxAge !== null) {
      const age = latest.localFrames + latest.frames - (row.localFrames - k);
      if (age < row.frames || age > maxAge) { reasons.push('ageBound'); k = null; }
    }
    let changed = false, matched = 0, unresolved = 0, wrong = 0;
    const wrongBefore = wrongPreFrames;
    const oracleKs = new Set(), preSerials = new Set(), preLaps = new Set();
    const actualPre = row.decision?.seen && row.render?.preAudible;
    if (actualPre) actualPreBlocks++;
    // preAudible describes the end of the block. A fade-out can end at zero after
    // playing PRE earlier; keep every non-stable render outside a zero-error claim.
    if (row.decision?.seen && !row.render?.stable) {
      unclassifiedRenderBlocks++; unclassifiedRenderFrames += row.frames;
    }
    for (let f = 0; f < row.frames; f++) {
      for (let c = 0; c < channels; c++) if (row.bits(0, c, f) !== row.bits(1, c, f)) changed = true;
      const id = identity(row, 0, f), hit = lookup(id);
      if (!hit || BigInt(hit.row.exitNanos) > BigInt(row.entryNanos)) {
        if (unknownSince === null) {
          unknownSince = { serial: row.serial, localFrame: row.localFrames + f, entryNanos: row.entryNanos };
          events.push({ type: 'oracleUnknownStart', ...unknownSince });
        }
        unknownFrames++; unresolved++; if (actualPre) unknownPreFrames++; continue;
      }
      oracleFrames++; matched++;
      const preFrame = hit.row.localFrames + hit.frame;
      const oracleK = row.localFrames + f - preFrame;
      oracleKs.add(oracleK); preSerials.add(hit.row.serial); preLaps.add(hit.row.lap);
      if (lastOracleK !== null && oracleK !== lastOracleK)
        events.push({ type: 'audioMappingChange', serial: row.serial, frame: f,
          localFrame: row.localFrames + f, entryNanos: row.entryNanos,
          onsetLowerFrame: unknownSince?.localFrame ?? row.localFrames + f,
          onsetLowerSerial: unknownSince?.serial ?? row.serial,
          oldK: lastOracleK, newK: oracleK });
      if (unknownSince !== null) events.push({ type: 'oracleUnknownEnd', serial: row.serial,
        localFrame: row.localFrames + f, entryNanos: row.entryNanos });
      unknownSince = null;
      lastOracleK = oracleK;
      const age = latest.localFrames + latest.frames - preFrame;
      maxObservedAge = Math.max(maxObservedAge ?? age, age);
      const residence = nanosMs(row.entryNanos, hit.row.exitNanos);
      maxResidenceMs = Math.max(maxResidenceMs ?? residence, residence);
      if (k !== null) { shadowFrames++; if (oracleK !== k) { shadowWrongFrames++; wrong++; } }
      if (actualPre) {
        if (!row.render.stable || !row.render.gainSettled || row.preGain !== 1) unknownPreFrames++;
        else { const playedId = identity(row, 1, f);
          if (playedId === null) unknownPreFrames++; else if (playedId !== id) wrongPreFrames++; }
      }
    }
    if (changed) changedBlocks++;
    if (wrongPreFrames !== wrongBefore) wrongPreBlocks++;
    if (reasons.length) events.push({ type: 'detection', serial: row.serial, frame: 0,
      localFrame: row.localFrames, entryNanos: row.entryNanos, reasons });
    callbacks.push({ serial: row.serial, localFrames: row.localFrames, frames: row.frames,
      entryNanos: row.entryNanos, lap: row.lap, preEpoch: latest?.epoch,
      latestPre: latest?.serial ?? null, preSerials: [...preSerials], preLaps: [...preLaps],
      contentHeld: row.contentHeld, oracleK: [...oracleKs], matched, unresolved,
      shadowK: k, candidates, shadowWrongFrames: wrong, outputChanged: changed, reasons });
    previous = callbacks.at(-1);
  }
  const detections = events.filter(e => e.type === 'detection');
  const changes = events.filter(e => e.type === 'audioMappingChange').map(e => {
    const before = detections.filter(d => d.serial <= e.serial).at(-1);
    const { detection: next, censor } = detectionWindow(post.rows, detections,
      post.rows.findIndex(r => r.serial >= e.onsetLowerSerial));
    return { ...e, detectionSerial: next?.serial ?? null,
      detectionAudioMs: next ? Math.max(0, next.localFrame - e.localFrame) * 1000 / rate : null,
      detectionAudioMsUpper: next ? Math.max(0, next.localFrame - e.onsetLowerFrame) * 1000 / rate : null,
      onsetUncertain: e.onsetLowerFrame !== e.localFrame,
      detectionWallMs: next ? nanosMs(next.entryNanos, e.entryNanos) : null,
      lastPriorDetectionSerial: before?.serial ?? null,
      // null means right-censored, not zero; an earlier notification is separately retained.
      censored: !next, censorSerial: !next ? censor?.serial ?? null : null };
  });
  const fixtureChanges = [];
  if (delay) for (let i = 1; i < delay.rows.length; i++) {
    const r = delay.rows[i], p = delay.rows[i - 1];
    if (r.fixtureGeneration === p.fixtureGeneration) continue;
    let first = post.rows.findIndex(p => BigInt(p.entryNanos) >= BigInt(r.entryNanos));
    if (first < 0) first = post.rows.length;
    const preceding = post.rows[Math.max(0, first - 1)];
    const { detection, playing, censor } = detectionWindow(post.rows, detections,
      preceding?.block.playing ? first : Math.max(0, first - 1));
    fixtureChanges.push({ serial: r.serial, entryNanos: r.entryNanos,
      from: p.fixtureDelay, to: r.fixtureDelay, reported: r.fixtureReported,
      playing, censored: !detection, censorSerial: !detection ? censor?.serial ?? null : null,
      detectionSerial: detection?.serial ?? null,
      detectionWallMs: detection ? nanosMs(detection.entryNanos, r.entryNanos) : null });
  }
  return { schema: 1, status: 'MEASUREMENT_ONLY_NOT_HOST_CERTIFICATION', rate, channels,
    integrity: { pre: pre.integrity, post: post.integrity, delay: delay?.integrity ?? null },
    hashes: { pre: pre.hashes, post: post.hashes, delay: delay?.hashes ?? null },
    scheduling: { pre: callbackStats(pre.rows), post: callbackStats(post.rows) },
    summary: { preBlocks: pre.rows.length, postBlocks: post.rows.length, oracleFrames, unknownFrames,
      changedBlocks, actualPreBlocks, wrongPreFrames: actualPreBlocks ? wrongPreFrames : null,
      wrongPreBlocks: actualPreBlocks ? wrongPreBlocks : null,
      unknownPreFrames, unclassifiedRenderBlocks, unclassifiedRenderFrames,
      shadowFrames, shadowWrongFrames, declaredMaximumAge: maxAge,
      shadowWrongBlocks: callbacks.filter(c => c.shadowWrongFrames > 0).length,
      maxObservedAge, maxResidenceMs, changes: changes.length,
      censoredChanges: changes.filter(c => c.censored).length },
    changes, fixtureChanges, events, callbacks };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try {
    const args = process.argv.slice(2), options = {};
    while (args.length) {
      const key = args.shift(), value = args.shift();
      if (!['--pre', '--post', '--delay', '--max-age', '--out'].includes(key) || !value)
        throw new Error('Usage: --pre RUN_DIR --post RUN_DIR [--delay RUN_DIR] [--max-age FRAMES] --out FILE');
      options[key] = value;
    }
    if (!options['--pre'] || !options['--post'] || !options['--out']) throw new Error('PRE, POST and output required');
    const bound = options['--max-age'] === undefined ? null : Number(options['--max-age']);
    if (bound !== null && (!Number.isSafeInteger(bound) || bound < 1)) throw new Error('Invalid age bound');
    const result = analyze(readTrace(options['--pre']), readTrace(options['--post']),
      options['--delay'] ? readTrace(options['--delay']) : null, bound);
    fs.writeFileSync(options['--out'], `${JSON.stringify(result, null, 2)}\n`, { flag: 'wx' });
    console.log(JSON.stringify(result.summary, null, 2));
    if (!result.integrity.pre.complete || !result.integrity.post.complete
        || (result.integrity.delay && !result.integrity.delay.complete)) process.exitCode = 2;
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
