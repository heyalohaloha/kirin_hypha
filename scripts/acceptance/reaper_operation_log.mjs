import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { checkCapture } from './reaper_capture_binding.mjs';
import { readPrivateFile, writeNewPrivateFile } from './reaper_private_file.mjs';

export const OPERATION_ORDER = Object.freeze(['start', 'insert', 'record-start', 'record-stop', 'save', 'close', 'reopen', 'export', 'finish']);
const REPOSITORY = fs.realpathSync(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..'));
const captureHash = capture => crypto.createHash('sha256').update(JSON.stringify(capture)).digest('hex');
const captureEvents = new Set(['record-start', 'record-stop', 'save', 'close', 'reopen', 'export']);

function privatePath(file) {
  const absolute = path.resolve(file), parent = path.dirname(absolute);
  if (fs.realpathSync(parent) !== parent || absolute === REPOSITORY || absolute.startsWith(REPOSITORY + path.sep)) {
    throw new Error('Operation log must be private and outside the repository');
  }
  return absolute;
}
function validateBinding(candidate, binding) {
  if (!/^[a-zA-Z0-9._-]{1,96}$/.test(candidate?.id || '') || !/^[a-f0-9]{40}$/.test(candidate?.commit || '')
      || !/^[a-f0-9]{64}$/.test(binding || '')) throw new Error('Invalid candidate binding');
}
function readLog(file, candidate, binding) {
  validateBinding(candidate, binding);
  file = privatePath(file);
  const contents = readPrivateFile(file);
  let log;
  try { log = JSON.parse(contents); } catch { throw new Error('Invalid operation log'); }
  if (log.schema !== 'hypha-reaper-operations-v1' || log.candidate?.id !== candidate.id
      || log.candidate?.commit !== candidate.commit || log.bindingSha256 !== binding || !Array.isArray(log.events)) {
    throw new Error('Operation log candidate binding differs');
  }
  let previous = 0;
  for (const [index, event] of log.events.entries()) {
    if (event.name !== OPERATION_ORDER[index] || !Number.isSafeInteger(event.timeMs) || event.timeMs < previous || event.timeMs <= 0) {
      throw new Error('Operation log order or time is invalid');
    }
    if (log.capture && captureEvents.has(event.name) && event.captureSha256 !== captureHash(log.capture)) throw new Error('Operation capture binding changed');
    previous = event.timeMs;
  }
  return log;
}
export function initOperationLog(file, candidate, binding, capture) {
  validateBinding(candidate, binding); file = privatePath(file);
  if (capture !== undefined) checkCapture(capture);
  writeNewPrivateFile(file, `${JSON.stringify({ schema: 'hypha-reaper-operations-v1', candidate: { id: candidate.id, commit: candidate.commit }, bindingSha256: binding, ...(capture ? { capture } : {}), events: [] }, null, 2)}\n`);
}
export function appendOperation(file, candidate, binding, name, clock = Date.now, capture) {
  file = privatePath(file);
  const lock = `${file}.lock`, fd = fs.openSync(lock, 'wx', 0o600);
  let temporary;
  try {
    const log = readLog(file, candidate, binding);
    if (capture !== undefined && JSON.stringify(capture) !== JSON.stringify(log.capture)) throw new Error('Operation capture binding changed');
    if (name !== OPERATION_ORDER[log.events.length]) throw new Error('Operation order is invalid');
    const timeMs = clock();
    if (!Number.isSafeInteger(timeMs) || timeMs <= 0 || timeMs < (log.events.at(-1)?.timeMs || 0)) throw new Error('Operation time is invalid');
    log.events.push({ name, timeMs, ...(log.capture && captureEvents.has(name) ? { captureSha256: captureHash(log.capture) } : {}) });
    temporary = `${file}.${crypto.randomBytes(8).toString('hex')}.tmp`;
    writeNewPrivateFile(temporary, `${JSON.stringify(log, null, 2)}\n`);
    fs.renameSync(temporary, file); temporary = null;
  } finally {
    if (temporary) fs.rmSync(temporary, { force: true });
    fs.closeSync(fd); fs.unlinkSync(lock);
  }
}
export function readCaptureBinding(file, candidate, binding) {
  const capture = readLog(file, candidate, binding).capture;
  checkCapture(capture);
  return capture;
}
export function readCompletedOperations(file, candidate, binding) {
  const log = readLog(file, candidate, binding);
  if (log.events.length !== OPERATION_ORDER.length) throw new Error('Operation log is incomplete');
  return log.events;
}
