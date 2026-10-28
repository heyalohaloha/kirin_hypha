import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { collectObservedReaperRun, loadPrivateReaperConfig } from './reaper_run.mjs';
import { OPERATION_ORDER } from './reaper_operation_log.mjs';
import { candidateKeyProof } from './reaper_capture_binding.mjs';
import { acceptanceCriteria, digest, reportTemplate, validateReport } from '../ls_release/hypha_release_contract.mjs';

const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function wav() {
  const b = Buffer.alloc(48); b.write('RIFF'); b.writeUInt32LE(40, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(48000, 24); b.writeUInt32LE(96000, 28);
  b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(4, 40); b.writeInt16LE(100, 44); b.writeInt16LE(-100, 46); return b;
}
function fixture(t) {
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-audit-r8-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const plugin = path.join(root, 'plugin'), reference = path.join(root, 'reference.wav'), processed = path.join(root, 'processed.wav');
  fs.writeFileSync(plugin, 'plugin'); fs.writeFileSync(reference, wav()); fs.copyFileSync(reference, processed);
  const candidate = { id: 'candidate', commit: 'a'.repeat(40) }, bindingSha256 = 'b'.repeat(64);
  const capture = { projectHash: 'new-project', sessionId: 'new-session', selection: { startFrame: 0, endFrame: 2 },
    sourceSha256: hash(wav()), sampleRate: 48000, frames: 2, instances: { PRE: 'PRE', POST: 'POST' } };
  const operationsLog = path.join(root, 'operations.json'), base = Date.now();
  const captureSha256 = hash(JSON.stringify(capture));
  fs.writeFileSync(operationsLog, JSON.stringify({ schema: 'hypha-reaper-operations-v1', candidate, bindingSha256, capture,
    events: OPERATION_ORDER.map((name, i) => ({ name, timeMs: base + i,
      ...(['record-start', 'record-stop', 'save', 'close', 'reopen', 'export'].includes(name) ? { captureSha256 } : {}) })) }), { mode: 0o600 });
  const records = path.join(root, 'records'); fs.mkdirSync(path.join(records, 'record_sessions'), { recursive: true });
  const side = role => {
    const file = path.join(records, role, '.pair_committed', 'member.json'); fs.mkdirSync(path.dirname(file), { recursive: true });
    const data = { role, status: 'closed', instance_id: role, project_hash: 'old-project', record_session_id: 'old-session',
      paired_pre_instance_id: role === 'POST' ? 'PRE' : null, paired_post_instance_id: role === 'PRE' ? 'POST' : null,
      sample_rate: 96000, frames: [{ t_ms: 0, lufs_m: -20, true_peak: -1, crest: 5 }], commit_status: 'committed', validity: true, integrity_degraded: false,
      record_quality: { status: 'complete', complete: true, usable: true, expected_wav_ready: true, sample_count_ready: true, trace_slots_complete: true, expected_frame_count: 1, measured_frame_count: 1, missing_trace_slots: 0 },
      trace_diagnostics: { expected_frame_count: 1, measured_frame_count: 1, missing_slots: 0 }, checksum: '' };
    data.checksum = crypto.createHmac('sha256', 'audit-key').update(JSON.stringify(data)).digest('hex');
    fs.writeFileSync(file, JSON.stringify(data)); return { role, path: file, checksum: data.checksum, instance_id: role };
  };
  fs.writeFileSync(path.join(records, 'record_sessions', 'pair.json'), JSON.stringify({ schema_version: 'pair_record_session.v1',
    session_id: 'old-session', project_hash: 'old-project', committed_at: '2000-01-01T00:00:00Z', pre: side('PRE'), post: side('POST') }));
  return { root, scope: 'host', pid: 123, candidate, bindingSha256, operationsLog, capture, captureSource: reference,
    candidateKeyProof: candidateKeyProof('audit-key', candidate), plugin: { expectedPath: plugin, expectedSha256: hash('plugin') },
    records: { source: records, closed: true, hmacKey: 'audit-key' }, exports: { reference, processed }, output: path.join(root, 'evidence') };
}
const observe = f => () => ({ processName: 'REAPER', paths: [f.plugin.expectedPath], mappings: [{ path: f.plugin.expectedPath,
  inode: fs.statSync(f.plugin.expectedPath, { bigint: true }).ino.toString() }] });

test('rejects the independently reproduced stale project, session, time and WAV mismatch', async t => {
  const f = fixture(t);
  await assert.rejects(() => collectObservedReaperRun(f, { observe: observe(f) }), /record|capture/i);
});
test('rejects the independently reproduced 0600 file with read ACL', t => {
  if (process.platform !== 'darwin') return t.skip('macOS ACL');
  const f = fixture(t), file = path.join(f.root, 'private.json'); fs.writeFileSync(file, '{}', { mode: 0o600 });
  const result = spawnSync('/bin/chmod', ['+a', 'everyone allow read', file], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr); assert.equal(fs.statSync(file).mode & 0o777, 0o600);
  assert.throws(() => loadPrivateReaperConfig(file), /private|ACL/i);
  assert.match(spawnSync('/bin/ls', ['-lde', file], { encoding: 'utf8' }).stdout, /allow read/);
});
test('rejects the independently reproduced valid-HMAC incomplete quality', async t => {
  const f = fixture(t), file = path.join(f.records.source, 'record_sessions', 'pair.json'), manifest = JSON.parse(fs.readFileSync(file));
  for (const side of [manifest.pre, manifest.post]) {
    const data = JSON.parse(fs.readFileSync(side.path));
    data.record_quality = { status: 'usable_fallback', complete: false, usable: true, trace_slots_complete: false, expected_frame_count: 10, measured_frame_count: 1, missing_trace_slots: 9 };
    data.trace_diagnostics = { expected_frame_count: 10, measured_frame_count: 1, missing_slots: 9 };
    data.checksum = ''; data.checksum = crypto.createHmac('sha256', 'audit-key').update(JSON.stringify(data)).digest('hex');
    fs.writeFileSync(side.path, JSON.stringify(data)); side.checksum = data.checksum;
  }
  fs.writeFileSync(file, JSON.stringify(manifest));
  await assert.rejects(() => collectObservedReaperRun(f, { observe: observe(f) }), /incomplete record/i);
});
test('rejects the independently reproduced untyped observation and missing identity', t => {
  const f = fixture(t), id = 'macOS/x86_64/VST3';
  const state = { root: f.root, candidate: f.candidate, inputs: {}, requiredHostTests: [id], expected: {}, evidenceMethods: { [id]: 'reaper' } };
  state.freeze = { criteriaSha256: digest(acceptanceCriteria(state)) }; state.freeze.sha256 = digest(state.freeze);
  const observation = path.join(f.root, 'host-observation.json'); fs.writeFileSync(observation, JSON.stringify({ schema: 'hypha-reaper-observed-run-v1' }));
  const report = reportTemplate(state, 'A4', state.freeze.sha256, [id]);
  report.tests[0] = { ...report.tests[0], result: 'PASS', measured: { passed: true }, oracle: 'review', evidence: [{ path: observation, sha256: hash(fs.readFileSync(observation)) }] };
  const file = path.join(f.root, 'report.json'); fs.writeFileSync(file, JSON.stringify(report));
  assert.throws(() => validateReport(state, file, 'A4', state.freeze.sha256, [id]), /REAPER|kind/i);
});

test('manual evidence stays available but a known observation cannot be downgraded', t => {
  const f = fixture(t), id = 'macOS/x86_64/VST3';
  const state = { root: f.root, candidate: f.candidate, inputs: {}, requiredHostTests: [id], expected: {} };
  state.freeze = { criteriaSha256: digest(acceptanceCriteria(state)) }; state.freeze.sha256 = digest(state.freeze);
  const evidence = path.join(f.root, 'evidence.json'), file = path.join(f.root, 'report.json');
  const report = reportTemplate(state, 'A4', state.freeze.sha256, [id]);
  const write = value => { fs.writeFileSync(evidence, JSON.stringify(value));
    report.tests[0] = { ...report.tests[0], result: 'PASS', measured: { passed: true }, oracle: 'manual review', evidence: [{ path: evidence, sha256: hash(fs.readFileSync(evidence)) }] };
    fs.writeFileSync(file, JSON.stringify(report)); };
  write({ manual: 'reviewed' }); assert.doesNotThrow(() => validateReport(state, file, 'A4', state.freeze.sha256, [id]));
  write({ schema: 'hypha-reaper-observed-run-v1', padding: 'x'.repeat(1024 * 1024) });
  assert.throws(() => validateReport(state, file, 'A4', state.freeze.sha256, [id]), /REAPER|kind/i);
});
