import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import test from 'node:test';
import { initOperationLog, appendOperation, OPERATION_ORDER } from './reaper_operation_log.mjs';
import { candidateKeyProof } from './reaper_capture_binding.mjs';
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const completedLog = (file, candidate, binding, capture) => { initOperationLog(file, candidate, binding, capture); OPERATION_ORDER.forEach((name, i) => appendOperation(file, candidate, binding, name, () => i + 1000)); return file; };
const observedFiles = paths => ({ processName: 'REAPER', paths, mappings: paths.map(file => ({ path: file, inode: fs.statSync(file, { bigint: true }).ino.toString() })) });
function wav(sample) {
  const b = Buffer.alloc(46); b.write('RIFF', 0); b.writeUInt32LE(38, 4); b.write('WAVEfmt ', 8);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(48000, 24);
  b.writeUInt32LE(96000, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34);
  b.write('data', 36); b.writeUInt32LE(2, 40); b.writeInt16LE(sample, 44); return b;
}
function fixture(t) {
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-reaper-fixture-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const plugin = path.join(root, 'plugin'), other = path.join(root, 'other');
  fs.writeFileSync(plugin, 'plugin'); fs.writeFileSync(other, 'other');
  const reference = path.join(root, 'reference.wav'), processed = path.join(root, 'processed.wav');
  fs.writeFileSync(reference, wav(100)); fs.writeFileSync(processed, wav(100));
  const records = path.join(root, 'records'); fs.mkdirSync(records);
  const side = role => {
    const member = path.join(records, role, '.pair_committed', 'member.json');
    fs.mkdirSync(path.dirname(member), { recursive: true });
    const data = { role, status: 'closed', instance_id: role, project_hash: 'fixture', record_session_id: 'fixture',
      paired_pre_instance_id: role === 'POST' ? 'PRE' : null, paired_post_instance_id: role === 'PRE' ? 'POST' : null,
      sample_rate: 48000, frames: [{ t_ms: 0, lufs_m: -20, true_peak: -1, crest: 5 }], commit_status: 'committed', validity: true, integrity_degraded: false, note: 'original',
      record_quality: { status: 'complete', complete: true, usable: true, expected_wav_ready: true, sample_count_ready: true, trace_slots_complete: true, expected_frame_count: 1, measured_frame_count: 1, missing_trace_slots: 0 },
      trace_diagnostics: { expected_frame_count: 1, measured_frame_count: 1, missing_slots: 0 }, expected_wav: { expected_sample_rate: 48000, expected_duration_samples: 1, wav_hash: hash(wav(100)) }, bounce_take: { sample_rate: 48000, wav_start_sample: 0, wav_end_sample: 1, duration_samples: 1 }, checksum: '' };
    const checksum = crypto.createHmac('sha256', 'fixture-key').update(JSON.stringify(data)).digest('hex');
    data.checksum = checksum; fs.writeFileSync(member, JSON.stringify(data));
    return { role, path: member, checksum, instance_id: role };
  };
  const manifestDir = path.join(records, 'record_sessions'); fs.mkdirSync(manifestDir);
  fs.writeFileSync(path.join(manifestDir, 'pair.json'), JSON.stringify({
    schema_version: 'pair_record_session.v1', session_id: 'fixture', project_hash: 'fixture',
    committed_at: new Date(1003).toISOString(), pre: side('PRE'), post: side('POST'),
  }));
  const candidate = { id: 'candidate-fixture', commit: 'a'.repeat(40) }, bindingSha256 = 'b'.repeat(64);
  const capture = { projectHash: 'fixture', sessionId: 'fixture', selection: { startFrame: 0, endFrame: 1 }, sourceSha256: hash(wav(100)), sampleRate: 48000, frames: 1, instances: { PRE: 'PRE', POST: 'POST' } };
  const operationsLog = path.join(root, 'operations.json');
  fs.writeFileSync(operationsLog, JSON.stringify({ schema: 'hypha-reaper-operations-v1', candidate, bindingSha256,
    capture, events: OPERATION_ORDER.map((name, i) => ({ name, timeMs: i + 1000, ...(['record-start','record-stop','save','close','reopen','export'].includes(name) ? { captureSha256: hash(JSON.stringify(capture)) } : {}) })) }), { mode: 0o600 });
  return { scope: 'fixture', candidate, bindingSha256, capture, captureSource: reference, candidateKeyProof: candidateKeyProof('fixture-key', candidate), operationsLog, plugin: { expectedPath: plugin, expectedSha256: hash('plugin'), loadedPath: plugin, loadedSha256: hash('plugin') },
    operations: ['start', 'insert', 'record-start', 'record-stop', 'save', 'close', 'reopen', 'export', 'finish'],
    records: { source: records, closed: true, hmacKey: 'fixture-key' }, exports: { reference, processed }, output: path.join(root, 'evidence') };
}
const validate = async run => (await import('./reaper_run.mjs')).collectReaperRun(run);
test('fixture results cannot become a real host PASS', async t => {
  const result = await validate(fixture(t)); assert.equal(result.status, 'FIXTURE_PASS'); assert.notEqual(result.hostResult, 'PASS');
});
test('wrong loaded plugin, interrupted operation and incomplete record are failures', async t => {
  for (const patch of [f => { f.plugin.loadedPath = path.join(path.dirname(f.plugin.loadedPath), 'other'); },
    f => { f.operations.pop(); }, f => { f.records.closed = false; }]) {
    const f = fixture(t); patch(f); await assert.rejects(() => validate(f), /plugin|operation|record/i);
  }
});
test('missing export and different audio are failures', async t => {
  for (const patch of [f => fs.unlinkSync(f.exports.processed), f => fs.writeFileSync(f.exports.processed, wav(101))]) {
    const f = fixture(t); patch(f); await assert.rejects(() => validate(f), /export|audio/i);
  }
});
test('one export cannot stand in for both reference and processed audio', async t => {
  const f = fixture(t); f.exports.processed = f.exports.reference;
  await assert.rejects(() => validate(f), /export/i);
  const linked = fixture(t); fs.unlinkSync(linked.exports.processed); fs.linkSync(linked.exports.reference, linked.exports.processed);
  await assert.rejects(() => validate(linked), /export/i);
});
test('fixture collector refuses to claim a real host result', async t => {
  const f = fixture(t); f.scope = 'host';
  await assert.rejects(() => validate(f), /Real host collection is not implemented/);
  assert.equal(fs.existsSync(f.output), false);
});

test('a missing committed pair member is an incomplete Record', async t => {
  const f = fixture(t);
  fs.unlinkSync(path.join(f.records.source, 'POST', '.pair_committed', 'member.json'));
  await assert.rejects(() => validate(f), /record/i);
  assert.equal(fs.existsSync(f.output), false);
});
test('record pair metadata must name matching members within the selected root', async t => {
  for (const patch of [
    manifest => { manifest.pre.role = 'POST'; },
    manifest => { manifest.pre.path = path.join(path.dirname(manifest.pre.path), '..', '..', '..', '..', 'plugin'); },
    manifest => { manifest.post.checksum = '0'.repeat(64); },
  ]) {
    const f = fixture(t), file = path.join(f.records.source, 'record_sessions', 'pair.json');
    const manifest = JSON.parse(fs.readFileSync(file, 'utf8')); patch(manifest);
    fs.writeFileSync(file, JSON.stringify(manifest));
    await assert.rejects(() => validate(f), /record/i);
  }
});
test('a committed record without a project identity is incomplete', async t => {
  const f = fixture(t), manifestPath = path.join(f.records.source, 'record_sessions', 'pair.json');
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8')); delete manifest.project_hash;
  for (const side of [manifest.pre, manifest.post]) {
    const data = JSON.parse(fs.readFileSync(side.path, 'utf8')); delete data.project_hash; data.checksum = '';
    data.checksum = crypto.createHmac('sha256', 'fixture-key').update(JSON.stringify(data)).digest('hex');
    fs.writeFileSync(side.path, JSON.stringify(data)); side.checksum = data.checksum;
  }
  fs.writeFileSync(manifestPath, JSON.stringify(manifest));
  await assert.rejects(() => validate(f), /record/i);
});
test('a committed record needs string identities for PRE and POST', async t => {
  for (const identity of [42, 'same-instance']) {
    const f = fixture(t), manifestPath = path.join(f.records.source, 'record_sessions', 'pair.json');
    const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    for (const side of [manifest.pre, manifest.post]) {
      const data = JSON.parse(fs.readFileSync(side.path, 'utf8')); data.instance_id = identity;
      if (data.role === 'PRE') data.paired_post_instance_id = identity; else data.paired_pre_instance_id = identity;
      data.checksum = ''; data.checksum = crypto.createHmac('sha256', 'fixture-key').update(JSON.stringify(data)).digest('hex');
      fs.writeFileSync(side.path, JSON.stringify(data)); side.checksum = data.checksum; side.instance_id = identity;
    }
    fs.writeFileSync(manifestPath, JSON.stringify(manifest));
    await assert.rejects(() => validate(f), /record/i);
  }
});
test('a changed committed member with the original checksum is rejected', async t => {
  const f = fixture(t), member = path.join(f.records.source, 'PRE', '.pair_committed', 'member.json');
  const data = JSON.parse(fs.readFileSync(member, 'utf8')); data.note = 'modified';
  fs.writeFileSync(member, JSON.stringify(data));
  await assert.rejects(() => validate(f), /record/i);
  assert.equal(fs.existsSync(f.output), false);
});

test('the wrong record checksum key is rejected', async t => {
  const f = fixture(t); f.records.hmacKey = 'wrong-key';
  await assert.rejects(() => validate(f), /record/i);
});
test('host evidence derives loaded path from the REAPER process and remains pending', async t => {
  const f = fixture(t); f.scope = 'host'; f.pid = 123;
  const { collectObservedReaperRun } = await import('./reaper_run.mjs');
  const result = await collectObservedReaperRun(f, { observe: () => observedFiles([f.plugin.expectedPath]) });
  assert.equal(result.status, 'HOST_EVIDENCE_PENDING'); assert.equal(result.hostResult, 'PENDING');
  assert.equal(result.loadedPath, f.plugin.expectedPath); assert.equal(result.loadedSha256, f.plugin.expectedSha256);
  assert.equal(result.observationSha256, hash(fs.readFileSync(path.join(f.output, 'host-observation.json'))));
});
test('host evidence rejects a different process or different loaded binary', async t => {
  const { collectObservedReaperRun } = await import('./reaper_run.mjs');
  for (const observe of [() => ({ processName: 'other', paths: [] }), () => ({ processName: 'REAPER', paths: [] })]) {
    const f = fixture(t); f.scope = 'host'; f.pid = 123;
    await assert.rejects(() => collectObservedReaperRun(f, { observe }), /process|plugin/i);
  }
});
test('a second loaded copy with the same plugin name is ambiguous', async t => {
  const { collectObservedReaperRun } = await import('./reaper_run.mjs');
  const f = fixture(t); f.scope = 'host'; f.pid = 123;
  const otherDir = path.join(path.dirname(f.plugin.expectedPath), 'other-copy'); fs.mkdirSync(otherDir);
  const other = path.join(otherDir, path.basename(f.plugin.expectedPath)); fs.writeFileSync(other, 'other version');
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => observedFiles([f.plugin.expectedPath, other]) }), /plugin|ambiguous/i);
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => {
    const observation = observedFiles([f.plugin.expectedPath]);
    observation.mappings.push({ path: `${other} (deleted)`, inode: '1' }); return observation;
  } }), /plugin|ambiguous/i);
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => {
    const observation = observedFiles([f.plugin.expectedPath]);
    observation.mappings.push({ path: `${other} (deleted)`, inode: null }); return observation;
  } }), /plugin|ambiguous/i);
  let observations = 0;
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => observedFiles(observations++ ? [f.plugin.expectedPath, other] : [f.plugin.expectedPath]) }), /plugin|ambiguous/i);
  assert.equal(fs.existsSync(path.join(f.output, 'host-observation.json')), false);
});
test('two mapped inodes for one plugin path are ambiguous', async t => {
  const { collectObservedReaperRun } = await import('./reaper_run.mjs');
  const f = fixture(t); f.scope = 'host'; f.pid = 123;
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => {
    const observation = observedFiles([f.plugin.expectedPath]);
    observation.mappings.push({ path: f.plugin.expectedPath, inode: '0' }); return observation;
  } }), /plugin|inode|ambiguous/i);
  let calls = 0;
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => {
    const observation = observedFiles([f.plugin.expectedPath]);
    if (calls++) observation.mappings.push({ path: f.plugin.expectedPath, inode: '0' }); return observation;
  } }), /plugin|inode|ambiguous|process/i);
  assert.equal(fs.existsSync(path.join(f.output, 'host-observation.json')), false);
});
test('private host config is read only from outside the repository', async t => {
  const f = fixture(t), root = path.dirname(f.plugin.expectedPath);
  const repo = path.join(root, 'repo'); fs.mkdirSync(repo);
  const good = path.join(root, 'private.json'), bad = path.join(repo, 'private.json');
  fs.writeFileSync(good, JSON.stringify(f), { mode: 0o600 }); fs.writeFileSync(bad, JSON.stringify(f), { mode: 0o600 });
  const { loadPrivateReaperConfig } = await import('./reaper_run.mjs');
  assert.equal(loadPrivateReaperConfig(good, repo).scope, 'fixture');
  assert.throws(() => loadPrivateReaperConfig(bad, repo), /private/i);
});
test('malformed private config does not expose its contents in the error', async t => {
  const f = fixture(t), root = path.dirname(f.plugin.expectedPath), repo = path.join(root, 'repo'); fs.mkdirSync(repo);
  const file = path.join(root, 'private.json'); fs.writeFileSync(file, '{"key":"secret-value",', { mode: 0o600 });
  const { loadPrivateReaperConfig } = await import('./reaper_run.mjs');
  assert.throws(() => loadPrivateReaperConfig(file, repo), error => !error.message.includes('secret-value'));
});
test('host evidence rejects a process that stops loading the candidate during collection', async t => {
  const f = fixture(t); f.scope = 'host'; f.pid = 123;
  const { collectObservedReaperRun } = await import('./reaper_run.mjs'); let observations = 0;
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => observedFiles(observations++ === 0 ? [f.plugin.expectedPath] : []) }), /plugin|process/i);
  assert.equal(fs.existsSync(path.join(f.output, 'host-observation.json')), false);
});
test('lsof process evidence uses NUL-delimited names and verifies the requested PID', async () => {
  const { parseReaperLsof } = await import('./reaper_run.mjs');
  const sample = 'p123\0\ncREAPER\0\nftxt\0\ni42\0\nntmp/path with spaces\0\n';
  assert.deepEqual(parseReaperLsof(sample, 123), { processName: 'REAPER', paths: ['tmp/path with spaces'], mappings: [{ path: 'tmp/path with spaces', inode: '42' }] });
  const withoutInode = 'p123\0\ncREAPER\0\nfmem\0\nn/tmp/plugin (deleted)\0\n';
  assert.deepEqual(parseReaperLsof(withoutInode, 123).mappings, [{ path: '/tmp/plugin (deleted)', inode: null }]);
  assert.throws(() => parseReaperLsof(sample, 124), /process/i);
});
test('a selected Record manifest can be collected when the project has multiple sessions', async t => {
  const f = fixture(t), directory = path.join(f.records.source, 'record_sessions');
  fs.writeFileSync(path.join(directory, 'other.json'), '{}');
  f.records.manifest = path.join(directory, 'pair.json');
  const result = await validate(f); assert.equal(result.status, 'FIXTURE_PASS');
});
test('Record manifest selection cannot escape its session directory', async t => {
  const f = fixture(t); f.records.manifest = f.plugin.expectedPath;
  await assert.rejects(() => validate(f), /record/i);
});
test('host evidence verifies every selected plugin binary', async t => {
  const f = fixture(t); f.scope = 'host'; f.pid = 123;
  const second = path.join(path.dirname(f.plugin.expectedPath), 'other');
  f.plugins = [f.plugin, { expectedPath: second, expectedSha256: hash('other') }];
  const { collectObservedReaperRun } = await import('./reaper_run.mjs');
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => observedFiles([f.plugin.expectedPath]) }), /plugin/i);
  assert.equal(fs.existsSync(f.output), false);
  const result = await collectObservedReaperRun(f, { observe: () => observedFiles([f.plugin.expectedPath, second]) });
  assert.equal(result.loadedPlugins.length, 2);
});
test('committed members must belong to the selected session', async t => {
  const f = fixture(t), file = path.join(f.records.source, 'record_sessions', 'pair.json');
  const manifest = JSON.parse(fs.readFileSync(file, 'utf8')); manifest.session_id = 'different-session';
  fs.writeFileSync(file, JSON.stringify(manifest));
  await assert.rejects(() => validate(f), /record/i);
});
test('collection retains only the selected pair evidence, not unrelated sessions', async t => {
  const f = fixture(t), directory = path.join(f.records.source, 'record_sessions');
  fs.writeFileSync(path.join(directory, 'other.json'), 'unrelated');
  fs.writeFileSync(path.join(f.records.source, 'unrelated-audio'), 'unrelated');
  f.records.manifest = path.join(directory, 'pair.json');
  const result = await validate(f);
  assert.equal(result.records.files.length, 3);
  assert.ok(result.records.files.every(row => !row.path.includes('other.json') && !row.path.includes('unrelated-audio')));
});
test('host evidence rejects an export that changes during collection', async t => {
  const f = fixture(t); f.scope = 'host'; f.pid = 123; let observations = 0;
  const { collectObservedReaperRun } = await import('./reaper_run.mjs');
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => {
    if (observations++ > 0) fs.writeFileSync(f.exports.processed, wav(101));
    return observedFiles([f.plugin.expectedPath]);
  } }), /export|audio/i);
  assert.equal(fs.existsSync(path.join(f.output, 'host-observation.json')), false);
});
test('host observation binds evidence to an explicit frozen candidate', async t => {
  const f = fixture(t); f.scope = 'host'; f.pid = 123; delete f.candidate; delete f.bindingSha256;
  const { collectObservedReaperRun } = await import('./reaper_run.mjs');
  const observe = () => observedFiles([f.plugin.expectedPath]);
  await assert.rejects(() => collectObservedReaperRun(f, { observe }), /candidate|binding/i);
  assert.equal(fs.existsSync(f.output), false);
  f.candidate = { id: 'candidate-fixture', commit: 'a'.repeat(40) }; f.bindingSha256 = 'b'.repeat(64);
  const result = await collectObservedReaperRun(f, { observe });
  assert.deepEqual(result.candidate, f.candidate); assert.equal(result.bindingSha256, f.bindingSha256);
});
test('retained host evidence is bound to candidate, binary and immutable record and WAV facts', async t => {
  const f = fixture(t); f.scope = 'host'; f.pid = 123;
  const { collectObservedReaperRun, verifyReaperEvidence } = await import('./reaper_run.mjs');
  await collectObservedReaperRun(f, { observe: () => observedFiles([f.plugin.expectedPath]) });
  const expected = { candidate: f.candidate, bindingSha256: f.bindingSha256, pluginHashes: [f.plugin.expectedSha256],
    observationSha256: hash(fs.readFileSync(path.join(f.output, 'host-observation.json'))), recordKey: 'fixture-key' };
  assert.doesNotThrow(() => verifyReaperEvidence(f.output, expected));
  assert.throws(() => verifyReaperEvidence(f.output, { ...expected, bindingSha256: 'c'.repeat(64) }), /binding/i);
  assert.throws(() => verifyReaperEvidence(f.output, { ...expected, pluginHashes: ['c'.repeat(64)] }), /plugin/i);
  const operations = path.join(f.output, 'operations.json'), savedOperations = fs.readFileSync(operations);
  const originalStat = fs.statSync; let changedDuringVerify = false;
  fs.statSync = (file, ...args) => {
    if (file === f.exports.processed && !changedDuringVerify) { changedDuringVerify = true; fs.appendFileSync(operations, ' '); }
    return originalStat(file, ...args);
  };
  try { assert.throws(() => verifyReaperEvidence(f.output, expected), /changed|evidence|retained/i); }
  finally { fs.statSync = originalStat; fs.writeFileSync(operations, savedOperations); }
  fs.unlinkSync(f.exports.processed); fs.linkSync(f.exports.reference, f.exports.processed);
  assert.throws(() => verifyReaperEvidence(f.output, expected), /export/i);
  fs.unlinkSync(f.exports.processed); fs.writeFileSync(f.exports.processed, wav(100));
  fs.writeFileSync(f.exports.processed, wav(101));
  assert.throws(() => verifyReaperEvidence(f.output, expected), /export/i);
  fs.writeFileSync(f.exports.processed, wav(100));
  const manifest = JSON.parse(fs.readFileSync(path.join(f.output, 'manifest.json'), 'utf8'));
  fs.writeFileSync(path.join(f.output, 'files', manifest.files[0].path), 'altered');
  assert.throws(() => verifyReaperEvidence(f.output, expected), /record/i);
});
test('host evidence is checked against the frozen candidate bundle binaries', async t => {
  const { makeState } = await import('../release_hypha.mjs');
  const { treeFact, fileFact, acceptanceCriteria, digest, reportTemplate, validateReport } = await import('../ls_release/hypha_release_contract.mjs');
  const { loadMacShipBundleManifest } = await import('../ls_release/kirin_hypha_ship_bundles.mjs');
  const { collectObservedReaperRun, verifyReaperEvidenceForState } = await import('./reaper_run.mjs');
  const f = fixture(t), root = path.dirname(f.plugin.expectedPath);
  fs.mkdirSync(path.join(root, 'config')); fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.copyFileSync(new URL('../../config/hypha_macos_ship_bundles.json', import.meta.url), path.join(root, 'config/hypha_macos_ship_bundles.json'));
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
  const source = { commit: 'a'.repeat(40), bNumber: 'B-1', fingerprint: 'b'.repeat(64), state: 'clean source' };
  const state = makeState(root, { state: path.join(root, 'release_state/profile.json') }, () => source);
  state.evidenceMethods = { 'macOS/x86_64/VST3': 'reaper' };
  const privateRoot = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-key-config-')); t.after(() => fs.rmSync(privateRoot, { recursive: true, force: true }));
  const keyConfig = path.join(privateRoot, 'key.json'), producer = path.join(privateRoot, 'producer.json'), proof = candidateKeyProof('fixture-key', state.candidate);
  fs.writeFileSync(keyConfig, JSON.stringify({ candidate: state.candidate, records: { hmacKey: 'fixture-key' } }), { mode: 0o600 }); fs.writeFileSync(producer, JSON.stringify({ schema: 'hypha-reaper-key-provenance-v1', candidate: state.candidate, keyProof: proof, review: 'fixture only' }), { mode: 0o600 });
  state.reaperKeyBindings = { 'macOS/x86_64/VST3': { keyConfig, proof, producerEvidence: fileFact(producer) } };
  const bundles = loadMacShipBundleManifest({ root }).bundles.filter(b => b.kind === 'vst3');
  for (const b of bundles) {
    const binary = path.join(b.sourcePath, 'Contents/MacOS', b.executable_name); fs.mkdirSync(path.dirname(binary), { recursive: true });
    fs.writeFileSync(binary, b.role === 'PRE' ? 'plugin' : 'other');
  }
  state.freeze = { source: state.source, inputs: digest(state.inputs), criteriaSha256: digest(acceptanceCriteria(state)),
    payloads: bundles.map(b => treeFact(b.sourcePath)) };
  state.freeze.sha256 = digest(state.freeze);
  f.scope = 'host'; f.pid = 123; f.candidate = state.candidate; f.bindingSha256 = state.freeze.sha256;
  f.candidateKeyProof = candidateKeyProof('fixture-key', f.candidate);
  f.operationsLog = completedLog(path.join(root, 'operations-frozen.json'), f.candidate, f.bindingSha256, f.capture);
  const installed = bundles.map(b => {
    const binary = path.join(root, 'installed', path.basename(b.install_relative), 'Contents/MacOS', b.executable_name);
    fs.mkdirSync(path.dirname(binary), { recursive: true }); fs.writeFileSync(binary, b.role === 'PRE' ? 'plugin' : 'other'); return binary;
  });
  f.plugin = { expectedPath: installed[0], expectedSha256: hash('plugin') };
  const other = installed[1]; f.plugins = [f.plugin, { expectedPath: other, expectedSha256: hash('other') }];
  await collectObservedReaperRun(f, { observe: () => observedFiles([f.plugin.expectedPath, other]) });
  const observationHash = hash(fs.readFileSync(path.join(f.output, 'host-observation.json')));
  assert.equal(verifyReaperEvidenceForState(state, f.output, observationHash, 'vst3').hostResult, 'PENDING');
  const observationFile = path.join(f.output, 'host-observation.json');
  const originalObservation = fs.readFileSync(observationFile);
  const ambiguous = JSON.parse(originalObservation); ambiguous.mappedPluginPaths.push(path.join(root, 'unexpected-copy'));
  fs.writeFileSync(observationFile, JSON.stringify(ambiguous));
  assert.throws(() => verifyReaperEvidenceForState(state, f.output, hash(fs.readFileSync(observationFile)), 'vst3'), /mapping/i);
  fs.writeFileSync(observationFile, originalObservation);
  const wrongPath = JSON.parse(originalObservation); wrongPath.loadedPlugins[0].loadedPath = path.join(root, 'wrong-plugin');
  wrongPath.mappedPluginPaths = wrongPath.loadedPlugins.map(plugin => plugin.loadedPath).sort();
  fs.writeFileSync(observationFile, JSON.stringify(wrongPath));
  assert.throws(() => verifyReaperEvidenceForState(state, f.output, hash(fs.readFileSync(observationFile)), 'vst3'), /plugin.*path/i);
  fs.writeFileSync(observationFile, originalObservation);
  const id = 'macOS/x86_64/VST3', report = reportTemplate(state, 'A4', state.freeze.sha256, [id]);
  report.tests[0] = { ...report.tests[0], result: 'PASS', measured: { passed: true }, oracle: 'independent review',
    evidence: [{ kind: 'reaper', format: 'vst3', path: path.join(f.output, 'host-observation.json'), sha256: observationHash }] };
  const reportPath = path.join(root, 'a4-report.json');
  report.tests[0].result = 'PENDING'; fs.writeFileSync(reportPath, JSON.stringify(report));
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /not PASS/i);
  report.tests[0].result = 'PASS'; fs.writeFileSync(reportPath, JSON.stringify(report));
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /identity/i);
  const identityPath = path.join(root, 'host-identity.json');
  fs.writeFileSync(identityPath, JSON.stringify({ schema: 'hypha-host-identity-v1', candidate: state.candidate,
    bindingSha256: state.freeze.sha256, processId: f.pid, os: 'macOS', osVersion: 'fixture', locale: 'en', architecture: 'x86_64', review: 'independent fixture review' }));
  const originalIdentity = fs.readFileSync(identityPath);
  report.tests[0].evidence.push({ kind: 'host-identity', path: identityPath, sha256: hash(originalIdentity) });
  const wrongArchitecture = JSON.parse(originalIdentity); wrongArchitecture.architecture = 'arm64';
  fs.writeFileSync(identityPath, JSON.stringify(wrongArchitecture));
  report.tests[0].evidence[1].sha256 = hash(fs.readFileSync(identityPath)); fs.writeFileSync(reportPath, JSON.stringify(report));
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /identity/i);
  const missingLocale = JSON.parse(originalIdentity); missingLocale.locale = '';
  fs.writeFileSync(identityPath, JSON.stringify(missingLocale));
  report.tests[0].evidence[1].sha256 = hash(fs.readFileSync(identityPath)); fs.writeFileSync(reportPath, JSON.stringify(report));
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /identity/i);
  fs.writeFileSync(identityPath, originalIdentity); report.tests[0].evidence[1].sha256 = hash(originalIdentity);
  fs.writeFileSync(reportPath, JSON.stringify(report));
  const savedKey = fs.readFileSync(keyConfig); fs.writeFileSync(keyConfig, JSON.stringify({ candidate: state.candidate, records: { hmacKey: 'wrong' } }));
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /key/i); fs.writeFileSync(keyConfig, savedKey);
  const savedProducer = fs.readFileSync(producer); fs.appendFileSync(producer, ' ');
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /changed/i); fs.writeFileSync(producer, savedProducer);
  state.reaperKeyBindings[id].proof = 'f'.repeat(64);
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /criteria/i); state.reaperKeyBindings[id].proof = proof;
  const facts = validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]);
  assert.ok(facts.some(fact => fact.path === path.join(f.output, 'audio.json')));
  assert.ok(facts.some(fact => fact.path === path.join(f.output, 'operations.json')));
  const operationFile = path.join(f.output, 'operations.json'), originalOperations = fs.readFileSync(operationFile);
  fs.appendFileSync(operationFile, ' ');
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /hash/i);
  fs.writeFileSync(operationFile, originalOperations);
  report.tests[0].evidence[0].format = 'au'; fs.writeFileSync(reportPath, JSON.stringify(report));
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /format/i);
  report.tests[0].evidence[0].format = 'vst3'; fs.writeFileSync(reportPath, JSON.stringify(report));
  fs.writeFileSync(f.exports.processed, wav(101));
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /export/i);
  fs.writeFileSync(f.exports.processed, wav(100));
  fs.writeFileSync(path.join(bundles[0].sourcePath, 'Contents/MacOS', bundles[0].executable_name), 'changed');
  assert.throws(() => verifyReaperEvidenceForState(state, f.output, observationHash, 'vst3'), /changed|hash/i);
  assert.throws(() => validateReport(state, reportPath, 'A4', state.freeze.sha256, [id]), /changed|hash/i);
});
test('an opened but unmapped plugin file is not treated as a loaded binary', async () => {
  const { parseReaperLsof } = await import('./reaper_run.mjs');
  const sample = 'p123\0\ncREAPER\0\nf3\0\ni12\0\nn/not-loaded\0\nftxt\0\ni42\0\nn/mapped\0\n';
  assert.deepEqual(parseReaperLsof(sample, 123).paths, ['/mapped']);
});
test('mapped binary identity includes its file inode', async () => {
  const { parseReaperLsof } = await import('./reaper_run.mjs');
  const sample = 'p123\0\ncREAPER\0\nftxt\0\ni42\0\nn/mapped\0\n';
  assert.deepEqual(parseReaperLsof(sample, 123).mappings, [{ path: '/mapped', inode: '42' }]);
});
test('a mapped inode different from the candidate file is rejected', async t => {
  const f = fixture(t); f.scope = 'host'; f.pid = 123;
  const { collectObservedReaperRun } = await import('./reaper_run.mjs');
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => ({ processName: 'REAPER',
    paths: [f.plugin.expectedPath], mappings: [{ path: f.plugin.expectedPath, inode: '0' }] }) }), /plugin/i);
  assert.equal(fs.existsSync(f.output), false);
});
test('host collection refuses an interrupted operation log even when a full list is supplied', async t => {
  const f = fixture(t); f.scope = 'host'; f.pid = 123;
  const { initOperationLog, appendOperation } = await import('./reaper_operation_log.mjs');
  f.operationsLog = path.join(path.dirname(f.output), 'partial-operations.json');
  initOperationLog(f.operationsLog, f.candidate, f.bindingSha256);
  appendOperation(f.operationsLog, f.candidate, f.bindingSha256, 'start', () => 1);
  const { collectObservedReaperRun } = await import('./reaper_run.mjs');
  await assert.rejects(() => collectObservedReaperRun(f, { observe: () => observedFiles([f.plugin.expectedPath]) }), /incomplete/i);
  assert.equal(fs.existsSync(f.output), false);
});
test('private config cannot be world-readable or a symlink', async t => {
  const f = fixture(t), root = path.dirname(f.plugin.expectedPath), repo = path.join(root, 'repo'); fs.mkdirSync(repo);
  const file = path.join(root, 'private.json'); fs.writeFileSync(file, JSON.stringify(f), { mode: 0o600 });
  const link = path.join(root, 'config-link'); fs.symlinkSync(file, link);
  const { loadPrivateReaperConfig } = await import('./reaper_run.mjs');
  assert.throws(() => loadPrivateReaperConfig(link, repo), /private/i);
  if (typeof process.getuid === 'function') {
    const originalLstat = fs.lstatSync;
    fs.lstatSync = (item, ...args) => {
      const stat = originalLstat(item, ...args);
      return item === file ? new Proxy(stat, { get(target, key) { return key === 'uid' ? process.getuid() + 1 : Reflect.get(target, key); } }) : stat;
    };
    try { assert.throws(() => loadPrivateReaperConfig(file, repo), /private/i); }
    finally { fs.lstatSync = originalLstat; }
  }
  fs.chmodSync(file, 0o644); assert.throws(() => loadPrivateReaperConfig(file, repo), /private/i);
});
test('a committed member with no measured frames is an incomplete Record', async t => {
  const f = fixture(t), manifestPath = path.join(f.records.source, 'record_sessions', 'pair.json');
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  const data = JSON.parse(fs.readFileSync(manifest.pre.path, 'utf8')); data.frames = []; data.checksum = '';
  data.checksum = crypto.createHmac('sha256', 'fixture-key').update(JSON.stringify(data)).digest('hex');
  fs.writeFileSync(manifest.pre.path, JSON.stringify(data)); manifest.pre.checksum = data.checksum;
  fs.writeFileSync(manifestPath, JSON.stringify(manifest));
  await assert.rejects(() => validate(f), /record/i);
});
test('a committed member with an empty measurement frame is incomplete', async t => {
  const f = fixture(t), manifestPath = path.join(f.records.source, 'record_sessions', 'pair.json');
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  const data = JSON.parse(fs.readFileSync(manifest.pre.path, 'utf8')); data.frames = [{}]; data.checksum = '';
  data.checksum = crypto.createHmac('sha256', 'fixture-key').update(JSON.stringify(data)).digest('hex');
  fs.writeFileSync(manifest.pre.path, JSON.stringify(data)); manifest.pre.checksum = data.checksum;
  fs.writeFileSync(manifestPath, JSON.stringify(manifest));
  await assert.rejects(() => validate(f), /record/i);
});
test('a committed Record requires a matching peer and valid common sample rate', async t => {
  for (const patch of [data => { data.paired_post_instance_id = 'wrong'; },
    data => { data.sample_rate = 0; }, data => { data.sample_rate = 96000; }]) {
    const f = fixture(t), manifestPath = path.join(f.records.source, 'record_sessions', 'pair.json');
    const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    const data = JSON.parse(fs.readFileSync(manifest.pre.path, 'utf8')); patch(data); data.checksum = '';
    data.checksum = crypto.createHmac('sha256', 'fixture-key').update(JSON.stringify(data)).digest('hex');
    fs.writeFileSync(manifest.pre.path, JSON.stringify(data)); manifest.pre.checksum = data.checksum;
    fs.writeFileSync(manifestPath, JSON.stringify(manifest));
    await assert.rejects(() => validate(f), /record/i);
  }
});
test('frozen REAPER evidence policy rejects missing, unknown and downgraded kinds', async t => {
  const { acceptanceCriteria, digest, reportTemplate, validateReport } = await import('../ls_release/hypha_release_contract.mjs');
  const f = fixture(t), id = 'macOS/x86_64/VST3', root = path.dirname(f.output);
  const state = { root, candidate: f.candidate, inputs: {}, requiredHostTests: [id], expected: {}, evidenceMethods: { [id]: 'reaper' } };
  state.freeze = { criteriaSha256: digest(acceptanceCriteria(state)) }; state.freeze.sha256 = digest(state.freeze);
  const observation = path.join(root, 'observation.json');
  fs.writeFileSync(observation, JSON.stringify({ schema: 'hypha-reaper-observed-run-v1' }));
  const report = reportTemplate(state, 'A4', state.freeze.sha256, [id]);
  const file = path.join(root, 'report.json');
  for (const kind of [undefined, 'reapre', 'attachment']) {
    report.tests[0] = { ...report.tests[0], result: 'PASS', measured: { passed: true }, oracle: 'review', evidence: [{ ...(kind ? { kind } : {}), path: observation, sha256: hash(fs.readFileSync(observation)) }] };
    fs.writeFileSync(file, JSON.stringify(report));
    assert.throws(() => validateReport(state, file, 'A4', state.freeze.sha256, [id]), /kind|REAPER|method/i);
  }
  state.evidenceMethods[id] = 'manual';
  assert.throws(() => validateReport(state, file, 'A4', state.freeze.sha256, [id]), /frozen|criteria/i);
});
test('host capture rejects stale session, stale commit, wrong key and changed reopen context', async t => {
  const { collectObservedReaperRun } = await import('./reaper_run.mjs');
  for (const mutation of ['session', 'commit', 'key', 'reopen']) {
    const f = fixture(t); f.scope = 'host'; f.pid = 123;
    const file = path.join(f.records.source, 'record_sessions', 'pair.json'), manifest = JSON.parse(fs.readFileSync(file));
    if (mutation === 'session') {
      manifest.session_id = 'old';
      for (const side of [manifest.pre, manifest.post]) {
        const data = JSON.parse(fs.readFileSync(side.path)); data.record_session_id = 'old'; data.checksum = '';
        data.checksum = crypto.createHmac('sha256', 'fixture-key').update(JSON.stringify(data)).digest('hex');
        fs.writeFileSync(side.path, JSON.stringify(data)); side.checksum = data.checksum;
      }
      fs.writeFileSync(file, JSON.stringify(manifest));
    } else if (mutation === 'commit') { manifest.committed_at = new Date(1).toISOString(); fs.writeFileSync(file, JSON.stringify(manifest)); }
    else if (mutation === 'key') f.candidateKeyProof = 'c'.repeat(64);
    else { const log = JSON.parse(fs.readFileSync(f.operationsLog)); log.events.find(e => e.name === 'reopen').captureSha256 = 'f'.repeat(64); fs.writeFileSync(f.operationsLog, JSON.stringify(log)); }
    await assert.rejects(() => collectObservedReaperRun(f, { observe: () => observedFiles([f.plugin.expectedPath]) }), /capture|record|key/i);
  }
});
