import assert from 'node:assert/strict';
import test from 'node:test';
import { checkCapture, checkCaptureRecord, checkCaptureAudio, candidateKeyProof } from './reaper_capture_binding.mjs';

const capture = () => ({ projectHash: 'project', sessionId: 'session', selection: { startFrame: 10, endFrame: 12 },
  sourceSha256: 'a'.repeat(64), sampleRate: 48000, frames: 2, instances: { PRE: 'pre', POST: 'post' } });
const record = () => ({ role: 'PRE', project_hash: 'project', record_session_id: 'session', instance_id: 'pre', sample_rate: 48000,
  expected_wav: { expected_sample_rate: 48000, expected_duration_samples: 2, wav_hash: 'b'.repeat(64) },
  bounce_take: { sample_rate: 48000, wav_start_sample: 0, wav_end_sample: 2, duration_samples: 2 } });
const audio = () => ({ sampleRate: 48000, frames: 2, reference: { sha256: 'a'.repeat(64) }, processed: { sha256: 'b'.repeat(64) } });

test('run capture binds selection, source, rate, pair and Record to exports', () => {
  const c = capture(); assert.doesNotThrow(() => checkCapture(c));
  assert.doesNotThrow(() => checkCaptureAudio(c, audio()));
  assert.doesNotThrow(() => checkCaptureRecord(c, record(), audio().processed.sha256));
  for (const mutate of [x => { x.project_hash = 'old'; }, x => { x.record_session_id = 'old'; },
    x => { x.instance_id = 'other'; }, x => { x.sample_rate = 96000; },
    x => { x.expected_wav.wav_hash = 'c'.repeat(64); }, x => { x.bounce_take.duration_samples = 3; }]) {
    const r = record(); mutate(r); assert.throws(() => checkCaptureRecord(c, r, audio().processed.sha256), /capture|record/i);
  }
  for (const mutate of [x => { x.sourceSha256 = 'c'.repeat(64); }, x => { x.sampleRate = 96000; },
    x => { x.frames = 1; }, x => { x.selection.endFrame = 11; }, x => { x.instances.POST = x.instances.PRE; }]) {
    const wrong = capture(); mutate(wrong); assert.throws(() => { checkCapture(wrong); checkCaptureAudio(wrong, audio()); }, /capture|audio/i);
  }
});

test('candidate key proof changes with key and candidate and discloses neither key', () => {
  const c = { id: 'candidate', commit: 'd'.repeat(40) };
  const proof = candidateKeyProof('private-key', c);
  assert.match(proof, /^[a-f0-9]{64}$/); assert.ok(!proof.includes('private-key'));
  assert.notEqual(proof, candidateKeyProof('other-key', c));
  assert.notEqual(proof, candidateKeyProof('private-key', { ...c, id: 'other' }));
});

test('operation log fixes capture before start and detects changes before reopen', async t => {
  const fs = await import('node:fs'), os = await import('node:os'), path = await import('node:path');
  const { initOperationLog, appendOperation, readCaptureBinding } = await import('./reaper_operation_log.mjs');
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-capture-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const file = path.join(root, 'operations.json'), candidate = { id: 'candidate', commit: 'd'.repeat(40) }, binding = 'e'.repeat(64), c = capture();
  initOperationLog(file, candidate, binding, c);
  appendOperation(file, candidate, binding, 'start', () => 1, c);
  const wrong = capture(); wrong.projectHash = 'other';
  assert.throws(() => appendOperation(file, candidate, binding, 'insert', () => 2, wrong), /capture/i);
  assert.deepEqual(readCaptureBinding(file, candidate, binding), c);
});
