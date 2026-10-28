// Fixture collector: real host evidence requires independent host observation.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { collectSelectedRecords } from './collect_records.mjs';
import { requireCompleteRecordQuality } from './reaper_record_quality.mjs';
import * as releaseContract from '../ls_release/hypha_release_contract.mjs';
import { loadMacShipBundleManifest } from '../ls_release/kirin_hypha_ship_bundles.mjs';
import { initOperationLog, appendOperation, readCompletedOperations, readCaptureBinding, OPERATION_ORDER } from './reaper_operation_log.mjs';
import { checkCapture, checkCaptureRecord, checkCaptureAudio, candidateKeyProof } from './reaper_capture_binding.mjs';
import { readPrivateFile } from './reaper_private_file.mjs';
import { createInterface } from 'node:readline';

const fileHash = file => releaseContract.fileFact(file).sha256;
const requiredOperations = ['start', 'insert', 'record-start', 'record-stop', 'save', 'close', 'reopen', 'export', 'finish'];

function requireDistinctExports(exports) {
  const [reference, processed] = [exports.reference, exports.processed].map(file => fs.statSync(file, { bigint: true }));
  if (!reference.isFile() || !processed.isFile() || (reference.dev === processed.dev && reference.ino === processed.ino)) {
    throw new Error('Two distinct exports are required');
  }
}

function requireFixturePairRecord(source, hmacKey, selectedManifest) {
  if (typeof hmacKey !== 'string' || !hmacKey) throw new Error('Record checksum key required');
  try {
    const root = fs.realpathSync(source);
    if (root !== source) throw new Error();
    const directory = path.join(root, 'record_sessions');
    if (fs.realpathSync(directory) !== directory) throw new Error();
    const manifests = fs.readdirSync(directory).filter(name => name.endsWith('.json'));
    if (!selectedManifest && manifests.length !== 1) throw new Error();
    const manifestFile = selectedManifest || path.join(directory, manifests[0]);
    if (path.dirname(manifestFile) !== directory || !manifestFile.endsWith('.json')
        || fs.realpathSync(manifestFile) !== manifestFile || !fs.lstatSync(manifestFile).isFile()) throw new Error();
    const manifestBytes = fs.readFileSync(manifestFile);
    const manifest = JSON.parse(manifestBytes.toString('utf8'));
    if (manifest.schema_version !== 'pair_record_session.v1'
        || ['session_id', 'project_hash', 'committed_at'].some(key => typeof manifest[key] !== 'string' || !manifest[key].trim())
        || manifest.pre?.instance_id === manifest.post?.instance_id) throw new Error();
    const selected = [{ path: manifestFile, sha256: crypto.createHash('sha256').update(manifestBytes).digest('hex') }];
    const rates = new Set();
    for (const [key, role] of [['pre', 'PRE'], ['post', 'POST']]) {
      const side = manifest[key], member = side?.path;
      if (side?.role !== role || typeof side.instance_id !== 'string' || !side.instance_id.trim()
          || !/^[a-f0-9]{64}$/.test(side.checksum || '')
          || typeof member !== 'string' || !member.startsWith(root + path.sep)
          || !member.split(path.sep).includes('.pair_committed') || fs.realpathSync(member) !== member
          || !fs.lstatSync(member).isFile()) throw new Error();
      const rawBytes = fs.readFileSync(member);
      const raw = rawBytes.toString('utf8');
      const data = JSON.parse(raw);
      requireCompleteRecordQuality(data);
      const suffix = '"checksum":"' + side.checksum + '"}';
      if (data.role !== role || data.status !== 'closed' || data.checksum !== side.checksum
          || data.instance_id !== side.instance_id || data.record_session_id !== manifest.session_id
          || data.project_hash !== manifest.project_hash || data.commit_status !== 'committed'
          || data.validity !== true || data.integrity_degraded !== false || !Array.isArray(data.frames) || !data.frames.length
          || data.frames.some(frame => !frame || !Number.isSafeInteger(frame.t_ms) || frame.t_ms < 0
            || !['lufs_m', 'true_peak', 'crest'].every(field => Number.isFinite(frame[field])))
          || !Number.isSafeInteger(data.sample_rate) || data.sample_rate <= 0
          || (role === 'PRE' && data.paired_post_instance_id !== manifest.post?.instance_id)
          || (role === 'POST' && data.paired_pre_instance_id !== manifest.pre?.instance_id) || !raw.endsWith(suffix)) throw new Error();
      rates.add(data.sample_rate);
      const unsigned = raw.slice(0, -suffix.length) + '"checksum":""}';
      const actual = crypto.createHmac('sha256', hmacKey).update(unsigned).digest('hex');
      if (!crypto.timingSafeEqual(Buffer.from(actual, 'hex'), Buffer.from(side.checksum, 'hex'))) throw new Error();
      selected.push({ path: member, sha256: crypto.createHash('sha256').update(rawBytes).digest('hex') });
    }
    if (rates.size !== 1) throw new Error();
    return selected;
  } catch { throw new Error('Incomplete record pair'); }
}

export async function collectReaperRun(run) {
  if (run?.scope !== 'fixture') throw new Error('Real host collection is not implemented');
  const plugin = run.plugin;
  try {
    if (!plugin || plugin.expectedPath !== plugin.loadedPath
        || fs.realpathSync(plugin.expectedPath) !== plugin.expectedPath
        || fileHash(plugin.expectedPath) !== plugin.expectedSha256
        || plugin.loadedSha256 !== plugin.expectedSha256) throw new Error();
  } catch { throw new Error('Loaded plugin path or hash does not match'); }
  if (JSON.stringify(run.operations) !== JSON.stringify(requiredOperations)) throw new Error('Incomplete operation sequence');
  if (run.records?.closed !== true) throw new Error('Record is not closed');
  const selectedRecords = requireFixturePairRecord(path.resolve(run.records.source), run.records.hmacKey, run.records.manifest);
  if (!run.exports?.reference || !run.exports?.processed
      || !fs.existsSync(run.exports.reference) || !fs.existsSync(run.exports.processed)) throw new Error('Missing export');
  requireDistinctExports(run.exports);
  const wavTool = fileURLToPath(new URL('./wav_null.py', import.meta.url));
  const result = spawnSync('python3', [wavTool, run.exports.reference, run.exports.processed], {
    encoding: 'utf8', env: { ...process.env, PYTHONDONTWRITEBYTECODE: '1' },
  });
  if (result.status !== 0) throw new Error('Audio comparison failed');
  const audio = JSON.parse(result.stdout);
  if (audio.passed !== true) throw new Error('Audio comparison failed');
  requireDistinctExports(run.exports);
  const records = collectSelectedRecords(path.resolve(run.records.source), path.resolve(run.output), selectedRecords);
  fs.writeFileSync(path.join(run.output, 'audio.json'), `${JSON.stringify(audio, null, 2)}\n`, { flag: 'wx', mode: 0o600 });
  return { status: 'FIXTURE_PASS', hostResult: 'PENDING', audio, records };
}

export function parseReaperLsof(output, pid) {
  const fields = output.split('\0').map(field => field.replace(/^\n+/, ''));
  const ids = fields.filter(field => field.startsWith('p')).map(field => field.slice(1));
  const names = fields.filter(field => field.startsWith('c')).map(field => field.slice(1));
  if (ids.length !== 1 || ids[0] !== String(pid) || names.length !== 1) throw new Error('REAPER process identity mismatch');
  const mappings = []; let entry = null;
  const flush = () => {
    if (entry && ['txt', 'mem'].includes(entry.descriptor) && entry.path) {
      mappings.push({ path: entry.path, inode: /^\d+$/.test(entry.inode || '') ? entry.inode : null });
    }
  };
  for (const field of fields) {
    if (field.startsWith('f')) { flush(); entry = { descriptor: field.slice(1) }; }
    else if (entry && field.startsWith('i')) entry.inode = field.slice(1);
    else if (entry && field.startsWith('n')) entry.path = field.slice(1);
  }
  flush();
  return { processName: names[0], paths: mappings.map(item => item.path), mappings };
}

function observeReaper(pid) {
  const result = spawnSync('/usr/sbin/lsof', ['-n', '-P', '-F0pcfin', '-p', String(pid)], { encoding: 'utf8' });
  if (result.status !== 0) throw new Error('Unable to inspect REAPER process');
  return parseReaperLsof(result.stdout, pid);
}

function selectedMappingPaths(observation, plugins) {
  const selected = new Set(plugins.map(plugin => plugin.expectedPath));
  const names = new Set([...selected].map(file => path.basename(file)));
  if (observation.mappings.some(item => typeof item?.path !== 'string')) throw new Error('Invalid plugin mappings');
  const mapped = [...new Set(observation.mappings.filter(item => names.has(path.basename(item.path).replace(/ \(deleted\)$/i, ''))).map(item => item.path))].sort();
  if (mapped.some(file => !selected.has(file))) throw new Error('Ambiguous loaded plugin copy');
  return mapped;
}

// Host observations remain pending until independently reviewed; injected observers are for fixtures.
export async function collectObservedReaperRun(run, { observe = observeReaper } = {}) {
  if (run?.scope !== 'host' || !Number.isSafeInteger(run.pid) || run.pid <= 0) throw new Error('Invalid REAPER process');
  if (!/^[a-zA-Z0-9._-]{1,96}$/.test(run.candidate?.id || '')
      || !/^[a-f0-9]{40}$/.test(run.candidate?.commit || '')
      || !/^[a-f0-9]{64}$/.test(run.bindingSha256 || '')) throw new Error('Frozen candidate binding required');
  if (typeof run.operationsLog !== 'string' || !path.isAbsolute(run.operationsLog)) throw new Error('Private operation log is required');
  const operationFact = releaseContract.fileFact(run.operationsLog);
  const events = readCompletedOperations(run.operationsLog, run.candidate, run.bindingSha256);
  const capture = readCaptureBinding(run.operationsLog, run.candidate, run.bindingSha256);
  if (JSON.stringify(capture) !== JSON.stringify(run.capture)) throw new Error('Operation capture binding changed');
  checkCaptureSource(run);
  if (candidateKeyProof(run.records?.hmacKey, run.candidate) !== run.candidateKeyProof) throw new Error('Candidate key configuration differs');
  if (fileHash(run.operationsLog) !== operationFact.sha256) throw new Error('Operation log changed');
  const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
  const output = path.resolve(run.output || '');
  if (output === repo || output.startsWith(repo + path.sep)) throw new Error('Host evidence must be private');
  const observation = await observe(run.pid);
  if (!observation || !/^reaper$/i.test(observation.processName || '') || !Array.isArray(observation.paths)) {
    throw new Error('Not a REAPER process');
  }
  const plugins = run.plugins || [run.plugin];
  if (!Array.isArray(plugins) || !plugins.length || plugins[0]?.expectedPath !== run.plugin?.expectedPath
      || plugins.some(plugin => typeof plugin?.expectedPath !== 'string' || !path.isAbsolute(plugin.expectedPath))
      || new Set(plugins.map(plugin => plugin?.expectedPath)).size !== plugins.length) throw new Error('Invalid plugin selection');
  if (!Array.isArray(observation.mappings)) throw new Error('Loaded plugin mappings unavailable');
  const mappedPluginPaths = selectedMappingPaths(observation, plugins);
  const loadedPlugins = plugins.map(plugin => {
    const loadedPath = plugin?.expectedPath;
    if (!loadedPath || !observation.paths.includes(loadedPath) || fs.realpathSync(loadedPath) !== loadedPath) throw new Error('Expected plugin is not loaded');
    const inode = fs.statSync(loadedPath, { bigint: true }).ino.toString();
    const mappings = observation.mappings.filter(item => item.path === loadedPath);
    if (!mappings.length || mappings.some(item => item.inode !== inode)) {
      throw new Error('Loaded plugin inode differs');
    }
    const loadedSha256 = fileHash(loadedPath);
    if (loadedSha256 !== plugin.expectedSha256) throw new Error('Loaded plugin hash differs');
    return { loadedPath, loadedSha256, inode };
  });
  const { loadedPath: expectedPath, loadedSha256 } = loadedPlugins[0];
  const captured = await collectReaperRun({ ...run, scope: 'fixture', operations: events.map(event => event.name),
    plugin: { ...run.plugin, loadedPath: expectedPath, loadedSha256 } });
  checkCaptureAudio(capture, captured.audio);
  checkCapturedRecords(output, captured.records, capture, captured.audio, events);
  const finalObservation = await observe(run.pid);
  if (!finalObservation || !/^reaper$/i.test(finalObservation.processName || '') || !Array.isArray(finalObservation.paths)
      || !Array.isArray(finalObservation.mappings)
      || JSON.stringify(selectedMappingPaths(finalObservation, plugins)) !== JSON.stringify(mappedPluginPaths)
      || loadedPlugins.some(plugin => !finalObservation.paths.includes(plugin.loadedPath)
        || !finalObservation.mappings.some(item => item.path === plugin.loadedPath)
        || finalObservation.mappings.some(item => item.path === plugin.loadedPath && item.inode !== plugin.inode)
        || fs.statSync(plugin.loadedPath, { bigint: true }).ino.toString() !== plugin.inode
        || fileHash(plugin.loadedPath) !== plugin.loadedSha256)) throw new Error('Loaded plugin or process changed during collection');
  try {
    requireDistinctExports(run.exports);
    checkCaptureSource(run);
    if (fileHash(run.exports.reference) !== captured.audio.reference.sha256
        || fileHash(run.exports.processed) !== captured.audio.processed.sha256) throw new Error();
  } catch { throw new Error('Export changed during collection'); }
  const copiedOperations = path.join(output, 'operations.json');
  if (fileHash(run.operationsLog) !== operationFact.sha256) throw new Error('Operation log changed');
  fs.copyFileSync(run.operationsLog, copiedOperations, fs.constants.COPYFILE_EXCL);
  if (fileHash(copiedOperations) !== operationFact.sha256 || fileHash(run.operationsLog) !== operationFact.sha256) throw new Error('Operation log changed');
  const evidence = { schema: 'hypha-reaper-observed-run-v1', hostResult: 'PENDING',
    candidate: run.candidate, bindingSha256: run.bindingSha256,
    candidateKeyProof: run.candidateKeyProof,
    loadedPath: expectedPath, loadedSha256, loadedPlugins, mappedPluginPaths, pid: run.pid, operations: events.map(event => event.name),
    operationLogSha256: operationFact.sha256,
    recordManifestSha256: fileHash(path.join(output, 'manifest.json')),
    audioResultSha256: fileHash(path.join(output, 'audio.json')),
    exports: { reference: { path: path.resolve(run.exports.reference), ...captured.audio.reference },
      processed: { path: path.resolve(run.exports.processed), ...captured.audio.processed } } };
  const observationFile = path.join(output, 'host-observation.json');
  fs.writeFileSync(observationFile, `${JSON.stringify(evidence, null, 2)}\n`, { flag: 'wx', mode: 0o600 });
  return { ...captured, ...evidence, status: 'HOST_EVIDENCE_PENDING', observationSha256: fileHash(observationFile) };
}

function checkCaptureSource(run) {
  checkCapture(run.capture);
  if (typeof run.captureSource !== 'string' || !path.isAbsolute(run.captureSource)
      || fs.realpathSync(run.captureSource) !== run.captureSource
      || fileHash(run.captureSource) !== run.capture.sourceSha256) throw new Error('Capture source differs');
}

function checkCapturedRecords(root, collection, capture, audio, events, key) {
  const records = collection.files.map(row => fs.readFileSync(path.join(root, 'files', row.path), 'utf8'));
  const parsed = records.map(raw => JSON.parse(raw));
  const manifest = parsed.find(row => row.schema_version === 'pair_record_session.v1');
  const members = parsed.filter(row => row.role === 'PRE' || row.role === 'POST');
  if (!manifest || manifest.project_hash !== capture.projectHash || manifest.session_id !== capture.sessionId
      || members.length !== 2 || new Set(members.map(row => row.role)).size !== 2) throw new Error('Record capture pair differs');
  const committed = Date.parse(manifest.committed_at), start = events.find(e => e.name === 'record-stop')?.timeMs;
  const end = events.find(e => e.name === 'finish')?.timeMs;
  if (!Number.isFinite(committed) || committed < start || committed > end) throw new Error('Record commit time differs from operation log');
  for (const data of members) {
    checkCaptureRecord(capture, data, audio.processed.sha256);
    const side = manifest[data.role.toLowerCase()];
    if (data.status !== 'closed' || data.commit_status !== 'committed' || data.validity !== true || data.integrity_degraded !== false
        || side?.role !== data.role || side.instance_id !== data.instance_id
        || (data.role === 'PRE' && data.paired_post_instance_id !== capture.instances.POST)
        || (data.role === 'POST' && data.paired_pre_instance_id !== capture.instances.PRE)) throw new Error('Incomplete retained record pair');
    if (key !== undefined) {
      const raw = records[parsed.indexOf(data)], suffix = '"checksum":"' + data.checksum + '"}';
      if (!/^[a-f0-9]{64}$/.test(data.checksum || '') || !raw.endsWith(suffix)
          || crypto.createHmac('sha256', key).update(raw.slice(0, -suffix.length) + '"checksum":""}').digest('hex') !== data.checksum
          || manifest[data.role.toLowerCase()]?.checksum !== data.checksum) throw new Error('Record checksum differs from candidate key');
    }
  }
}

// Recheck retained bytes against a separately frozen candidate and observation hash.
export function verifyReaperEvidence(directory, expected) {
  const root = path.resolve(directory);
  if (fs.realpathSync(root) !== root) throw new Error('Evidence path traverses a symlink');
  const facts = [];
  const readEvidence = (name, expectedHash) => {
    const file = path.join(root, name);
    if (fs.realpathSync(file) !== file || !fs.lstatSync(file).isFile()) throw new Error('Evidence hash changed');
    const bytes = fs.readFileSync(file), actual = crypto.createHash('sha256').update(bytes).digest('hex');
    const fact = releaseContract.fileFact(file);
    if (actual !== expectedHash || fact.sha256 !== expectedHash) throw new Error('Evidence hash changed');
    facts.push(fact);
    return JSON.parse(bytes.toString('utf8'));
  };
  const observation = readEvidence('host-observation.json', expected.observationSha256);
  if (observation.schema !== 'hypha-reaper-observed-run-v1' || observation.hostResult !== 'PENDING'
      || observation.candidate?.id !== expected.candidate?.id || observation.candidate?.commit !== expected.candidate?.commit
      || observation.bindingSha256 !== expected.bindingSha256) throw new Error('Candidate binding differs');
  if (candidateKeyProof(expected.recordKey, expected.candidate) !== observation.candidateKeyProof) throw new Error('Candidate key configuration differs');
  const hashes = observation.loadedPlugins?.map(plugin => plugin.loadedSha256).sort();
  if (!Array.isArray(expected.pluginHashes) || !hashes?.length || hashes.length !== expected.pluginHashes.length
      || JSON.stringify(hashes) !== JSON.stringify([...expected.pluginHashes].sort())) throw new Error('Loaded plugin hashes differ');
  if (!Array.isArray(observation.mappedPluginPaths)
      || JSON.stringify(observation.mappedPluginPaths) !== JSON.stringify(observation.loadedPlugins.map(plugin => plugin.loadedPath).sort())) {
    throw new Error('Loaded plugin mappings are ambiguous');
  }
  const operationLog = readEvidence('operations.json', observation.operationLogSha256);
  const operationEvents = readCompletedOperations(path.join(root, 'operations.json'), observation.candidate, observation.bindingSha256);
  if (JSON.stringify(operationLog.events) !== JSON.stringify(operationEvents)
      || JSON.stringify(operationEvents.map(event => event.name)) !== JSON.stringify(observation.operations)
      || fileHash(path.join(root, 'operations.json')) !== observation.operationLogSha256) throw new Error('Operation log changed');
  const manifest = readEvidence('manifest.json', observation.recordManifestSha256);
  if (manifest.schema !== 'hypha-record-collection-v1' || !Array.isArray(manifest.files) || manifest.files.length !== 3
      || new Set(manifest.files.map(row => row.path)).size !== manifest.files.length) throw new Error('Invalid record manifest');
  const recordRoles = [];
  for (const row of manifest.files) {
    if (typeof row.path !== 'string' || path.isAbsolute(row.path) || row.path.split(path.sep).some(part => !part || part === '.' || part === '..')) throw new Error('Invalid record path');
    const file = path.join(root, 'files', row.path);
    if (fs.realpathSync(file) !== file || !fs.lstatSync(file).isFile() || fs.statSync(file).size !== row.bytes
        || fileHash(file) !== row.sha256) throw new Error('Retained record changed');
    const fact = releaseContract.fileFact(file);
    if (fact.sha256 !== row.sha256 || fact.bytes !== row.bytes) throw new Error('Retained record changed');
    const record = JSON.parse(fs.readFileSync(file, 'utf8'));
    if (record.role === 'PRE' || record.role === 'POST') { requireCompleteRecordQuality(record); recordRoles.push(record.role); }
    facts.push(fact);
  }
  if (recordRoles.sort().join(',') !== 'POST,PRE') throw new Error('Incomplete retained record pair');
  const audio = readEvidence('audio.json', observation.audioResultSha256);
  if (audio.schema !== 'hypha-wav-null-v1' || audio.passed !== true || audio.mismatchedSamples !== 0
      || audio.nonFiniteSamples !== 0 || audio.latencySamples !== 0) throw new Error('Invalid audio result');
  const capture = readCaptureBinding(path.join(root, 'operations.json'), observation.candidate, observation.bindingSha256);
  checkCaptureAudio(capture, audio);
  checkCapturedRecords(root, manifest, capture, audio, operationEvents, expected.recordKey);
  for (const name of ['reference', 'processed']) {
    const fact = observation.exports?.[name], actual = audio[name];
    if (!fact || !actual || fact.sha256 !== actual.sha256 || fact.bytes !== actual.bytes
        || !path.isAbsolute(fact.path) || fs.statSync(fact.path).size !== fact.bytes || fileHash(fact.path) !== fact.sha256) {
      throw new Error('Retained export changed');
    }
    const retained = releaseContract.fileFact(fact.path);
    if (retained.sha256 !== fact.sha256 || retained.bytes !== fact.bytes) throw new Error('Retained export changed');
    facts.push(retained);
  }
  requireDistinctExports({ reference: observation.exports.reference.path, processed: observation.exports.processed.path });
  releaseContract.checkFacts(facts);
  return { hostResult: 'PENDING', observationSha256: expected.observationSha256, observation, facts };
}

// Verify evidence against frozen candidate binaries; this never upgrades a host result to PASS.
export function verifyReaperEvidenceForState(state, directory, observationSha256, format) {
  releaseContract.assertState(state);
  const { sha256, ...frozen } = state.freeze || {};
  if (!sha256 || sha256 !== releaseContract.digest(frozen)
      || frozen.inputs !== releaseContract.digest(state.inputs)
      || frozen.criteriaSha256 !== releaseContract.digest(releaseContract.acceptanceCriteria(state))
      || releaseContract.digest(frozen.source) !== releaseContract.digest(state.source)) throw new Error('Frozen candidate binding changed');
  if (!['vst3', 'au'].includes(format)) throw new Error('Unsupported REAPER plugin format');
  const bundles = loadMacShipBundleManifest({ root: state.root }).bundles.filter(bundle => bundle.kind === format);
  if (bundles.length !== 2 || new Set(bundles.map(bundle => bundle.role)).size !== 2) throw new Error('Frozen plugin pair missing');
  const facts = bundles.map(bundle => {
    const matches = (frozen.payloads || []).filter(fact => fact.path === bundle.sourcePath && fact.treeSha256);
    if (matches.length !== 1) throw new Error('Frozen plugin payload missing');
    return matches[0];
  });
  releaseContract.checkFacts(facts);
  const pluginHashes = bundles.map(bundle => {
    const binary = path.join(bundle.sourcePath, 'Contents/MacOS', bundle.executable_name);
    if (fs.realpathSync(binary) !== binary || !fs.lstatSync(binary).isFile()) throw new Error('Frozen plugin binary invalid');
    return fileHash(binary);
  });
  const id = `macOS/x86_64/${format.toUpperCase()}`, bound = state.reaperKeyBindings?.[id];
  if (!bound || typeof bound.keyConfig !== 'string' || !path.isAbsolute(bound.keyConfig)
      || !/^[a-f0-9]{64}$/.test(bound.proof || '') || !bound.producerEvidence) throw new Error('Frozen candidate key configuration required');
  releaseContract.checkFacts([bound.producerEvidence]);
  const producer = loadPrivateReaperConfig(bound.producerEvidence.path, state.root);
  if (producer.schema !== 'hypha-reaper-key-provenance-v1' || producer.candidate?.id !== state.candidate.id
      || producer.candidate?.commit !== state.candidate.commit || producer.keyProof !== bound.proof
      || typeof producer.review !== 'string' || !producer.review.trim()) throw new Error('Candidate key producer evidence differs');
  const configFact = releaseContract.fileFact(bound.keyConfig), config = loadPrivateReaperConfig(bound.keyConfig, state.root);
  if (config.candidate?.id !== state.candidate.id || config.candidate?.commit !== state.candidate.commit
      || candidateKeyProof(config.records?.hmacKey, state.candidate) !== bound.proof) throw new Error('Candidate key configuration differs');
  const verified = verifyReaperEvidence(directory, { candidate: state.candidate, bindingSha256: sha256, pluginHashes, observationSha256, recordKey: config.records.hmacKey });
  const { observation } = verified;
  for (const [index, bundle] of bundles.entries()) {
    const suffix = path.join(path.basename(bundle.install_relative), 'Contents', 'MacOS', bundle.executable_name);
    if (!observation.loadedPlugins.some(plugin => typeof plugin.loadedPath === 'string'
        && path.isAbsolute(plugin.loadedPath) && plugin.loadedPath.endsWith(path.sep + suffix)
        && plugin.loadedSha256 === pluginHashes[index])) throw new Error('Loaded plugin path does not match its frozen role');
  }
  releaseContract.checkFacts(facts);
  releaseContract.checkFacts([configFact, bound.producerEvidence]);
  loadPrivateReaperConfig(bound.keyConfig, state.root);
  loadPrivateReaperConfig(bound.producerEvidence.path, state.root);
  return { ...verified, facts: [...verified.facts, ...facts, bound.producerEvidence] };
}

export function loadPrivateReaperConfig(file, repository = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..')) {
  const absolute = path.resolve(file);
  const real = fs.realpathSync(absolute), root = fs.realpathSync(repository);
  if (real !== absolute || real === root || real.startsWith(root + path.sep)) {
    throw new Error('Private config must be outside the repository, without symlinks and accessible only to its owner');
  }
  const contents = readPrivateFile(absolute);
  try { return JSON.parse(contents); }
  catch { throw new Error('Invalid private config'); }
}

async function recordOperationsGuided(config) {
  if (config.scope === 'host') checkCaptureSource(config);
  initOperationLog(config.operationsLog, config.candidate, config.bindingSha256, config.capture);
  const input = createInterface({ input: process.stdin, terminal: false });
  let index = 0;
  process.stdout.write(`After ${OPERATION_ORDER[index]}, press Enter.\n`);
  try {
    for await (const ignored of input) {
      void ignored; appendOperation(config.operationsLog, config.candidate, config.bindingSha256, OPERATION_ORDER[index], Date.now, config.capture);
      index += 1;
      if (index === OPERATION_ORDER.length) break;
      process.stdout.write(`After ${OPERATION_ORDER[index]}, press Enter.\n`);
    }
  } finally { input.close(); }
  if (index !== OPERATION_ORDER.length) throw new Error('Operation log is incomplete');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const args = process.argv.slice(2);
    if (args[0] === '--record-operations' && args.length === 2) {
      const config = loadPrivateReaperConfig(args[1]);
      await recordOperationsGuided(config);
      process.stdout.write('Operation log complete\n');
    } else if (args[0] === '--init-operations' && args.length === 2) {
      const config = loadPrivateReaperConfig(args[1]);
      if (config.scope === 'host') checkCaptureSource(config);
      initOperationLog(config.operationsLog, config.candidate, config.bindingSha256, config.capture);
      process.stdout.write('Operation log initialized\n');
    } else if (args[0] === '--event' && args.length === 3) {
      const config = loadPrivateReaperConfig(args[1]);
      appendOperation(config.operationsLog, config.candidate, config.bindingSha256, args[2], Date.now, config.capture);
      process.stdout.write('Operation recorded\n');
    } else if (args.length === 1) {
      const config = loadPrivateReaperConfig(args[0]);
      const result = await collectObservedReaperRun(config);
      process.stdout.write(`${JSON.stringify({ status: result.status, hostResult: result.hostResult, observationSha256: result.observationSha256 })}\n`);
    } else throw new Error('Usage: node reaper_run.mjs [--record-operations|--init-operations|--event] PRIVATE_CONFIG [EVENT]');
  } catch (error) {
    process.stderr.write(`REAPER evidence collection failed: ${error.message}\n`);
    process.exitCode = 1;
  }
}
