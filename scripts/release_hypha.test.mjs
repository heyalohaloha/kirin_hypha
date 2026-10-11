import assert from 'node:assert/strict';
import childProcess from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { STAGES, executeRelease, parseReleaseArgs, makeState } from './release_hypha.mjs';
import { HOST_TESTS, assertState, hash, digest, fileFact, treeFact, checkFacts,
  validateReport, reportTemplate, safeStatePath, authorization, Checkpoint } from './ls_release/hypha_release_contract.mjs';
import { verifyCi, verifyPackages, artifactPaths } from './ls_release/hypha_release_local.mjs';
import { updateBinding } from './updates/update_key_binding.mjs';

const SOURCE = { commit: 'a'.repeat(40), bNumber: 'B-1234', state: 'clean source', fingerprint: 'b'.repeat(64), juce: null };
const options = { execute: true, until: 'hp' };
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-release-test-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), '[package]\nversion = "1.2.3"\n');
  const statePath = path.join(root, 'release_state/pipeline.json');
  const state = makeState(root, { state: statePath, date: '2026-10-01' }, () => SOURCE);
  const evidence = path.join(root, 'evidence.txt'); fs.writeFileSync(evidence, 'independent retained evidence');
  const calls = [];
  const actions = Object.fromEntries(STAGES.map(stage => [stage, async () => {
    calls.push(stage); return { facts: [fileFact(evidence)] };
  }]));
  const dependencies = { statePath, snapshot: () => SOURCE, clean: () => {}, actions, log: () => {} };
  return { root, state, statePath, evidence, calls, actions, dependencies };
}

test('release CLI is a plan by default; authorization, SDK and execution are separate', () => {
  assert.equal(parseReleaseArgs(['--state', 'release_state/a.json']).execute, false);
  assert.throws(() => parseReleaseArgs(['--state']), /requires a value/);
  assert.throws(() => parseReleaseArgs(['--state', 'x', '--execute', '--dry-run']), /must not execute/);
  assert.throws(() => parseReleaseArgs(['--state', 'x', '--until', 'upload-only']), /Invalid/);
  assert.throws(() => parseReleaseArgs(['--state', 'x', '--skip-windows']), /Unknown/);
  assert.throws(() => authorization({ candidate: { id: 'exact' } }, {}), Checkpoint);
  authorization({ candidate: { id: 'exact' } }, { publishApproved: 'exact' });
});

test('an enabled update key selects a distinct approval identity even at the same source/version', t => {
  const f = fixture(t), options = { state: f.statePath, date: '2026-10-01' };
  const a = makeState(f.root, { ...options, updatePublicKey: `10001,${'f'.repeat(512)}` }, () => SOURCE);
  const b = makeState(f.root, { ...options, updatePublicKey: `10001,${'e'.repeat(511)}f` }, () => SOURCE);
  assert.notEqual(a.candidate.id, b.candidate.id); assert.notEqual(a.candidate.id, f.state.candidate.id);
  assert.throws(() => authorization(b, { publishApproved: a.candidate.id }), Checkpoint);
});

test('unified entry release help works without circular import, SDK, CI or machine access', () => {
  const result = childProcess.spawnSync(process.execPath, ['scripts/build_hypha.mjs', '--release', '--help'],
    { cwd: path.resolve(import.meta.dirname, '..'), encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /One workflow through HP upload/);
});

test('plan is read-only and does not call producers', async t => {
  const f = fixture(t);
  await executeRelease(f.state, { execute: false, until: 'hp' }, f.dependencies);
  assert.deepEqual(f.calls, []); assert.equal(fs.existsSync(f.statePath), false);
});

test('all stages through HP run in order; Released is not Release Complete', async t => {
  const f = fixture(t);
  await executeRelease(f.state, options, f.dependencies);
  assert.deepEqual(f.calls, STAGES.slice(0, -1));
  assert.equal(f.state.releaseState, 'RELEASED');
  assert.equal(f.state.stages.postrelease, undefined);
  await executeRelease(f.state, { ...options, until: 'complete' }, f.dependencies);
  assert.equal(f.calls.length, STAGES.length); assert.equal(f.state.releaseState, 'RELEASE_COMPLETE');
});

test('successful resume verifies retained bytes and never rebuilds/signs/uploads again', async t => {
  const f = fixture(t); await executeRelease(f.state, options, f.dependencies);
  const count = f.calls.length; await executeRelease(f.state, options, f.dependencies);
  assert.equal(f.calls.length, count); assert.equal(fs.existsSync(`${f.statePath}.lock`), false);
});

test('missing human report stops downstream stages and resumes only the missing stage', async t => {
  const f = fixture(t); f.actions.hosts = async () => { throw new Checkpoint('host report missing'); };
  await executeRelease(f.state, options, f.dependencies);
  assert.equal(f.state.stages.hosts.status, 'PENDING'); assert.equal(f.state.stages.packages, undefined);
  f.actions.hosts = async () => ({ facts: [fileFact(f.evidence)] });
  await executeRelease(f.state, options, f.dependencies);
  assert.equal(f.calls.filter(s => s === 'macos-aax').length, 1);
  assert.equal(f.state.releaseState, 'RELEASED');
});

test('failed signing output is never blindly retried or promoted', async t => {
  const f = fixture(t); let attempts = 0;
  f.actions['macos-aax'] = async () => { attempts++; throw new Error('authorization failed'); };
  await executeRelease(f.state, options, f.dependencies);
  assert.equal(f.state.stages['macos-aax'].status, 'FAIL');
  await executeRelease(f.state, options, f.dependencies);
  await executeRelease(f.state, options, f.dependencies);
  assert.equal(attempts, 1); assert.equal(f.state.stages.windows, undefined);
});

test('source changes, altered bytes and locks cannot be hidden by completed receipts', async t => {
  const f = fixture(t); await executeRelease(f.state, { ...options, until: 'packages' }, f.dependencies);
  fs.writeFileSync(f.evidence, 'changed'); await executeRelease(f.state, options, f.dependencies);
  assert.equal(f.state.stages.ls, undefined); assert.match(f.state.blocker.reason, /artifact changed/);
  await assert.rejects(executeRelease(f.state, options, { ...f.dependencies,
    snapshot: () => ({ ...SOURCE, fingerprint: 'c'.repeat(64) }) }), /source changed/);
  fs.writeFileSync(`${f.statePath}.lock`, 'another task');
  await assert.rejects(executeRelease(f.state, options, f.dependencies), /EEXIST/);
  assert.equal(fs.readFileSync(`${f.statePath}.lock`, 'utf8'), 'another task');
});

test('release input paths are root-relative and a changed freeze receipt cannot resume', async t => {
  const f = fixture(t);
  const profile = makeState(f.root, { state: f.statePath, sdk: 'external-sdk', notes: 'release_state/notes.md',
    windowsInstallerDir: 'windows', lsState: 'release_state/ls.json' }, () => SOURCE);
  for (const key of ['sdk', 'notes', 'windowsInstallerDir', 'lsState']) assert.ok(path.isAbsolute(profile.inputs[key]));
  f.actions.freeze = async () => {
    f.state.freeze = { inputs: digest(f.state.inputs), updateCheck: updateBinding(''),
      criteriaSha256: digest({ tests: f.state.requiredHostTests, expected: f.state.expected }) };
    f.state.freeze.sha256 = digest(f.state.freeze);
    return { facts: [fileFact(f.evidence)] };
  };
  await executeRelease(f.state, { ...options, until: 'packages' }, f.dependencies);
  f.state.freeze.unrecordedChange = true;
  await executeRelease(f.state, options, f.dependencies);
  assert.match(f.state.blocker.reason, /Frozen candidate receipt changed/);
  assert.equal(f.state.stages.ls, undefined);
});

test('publication errors enter Incident; A7 pending remains Released, never Rollback Complete', async t => {
  const f = fixture(t);
  f.actions.github = async () => { f.state.publicationStarted = true; throw new Error('asset hash mismatch'); };
  await executeRelease(f.state, options, f.dependencies);
  assert.equal(f.state.releaseState, 'RELEASE_INCIDENT');
  await assert.rejects(executeRelease(f.state, options, f.dependencies), /Incident requires/);
  const g = fixture(t); await executeRelease(g.state, options, g.dependencies);
  g.actions.postrelease = async () => { throw new Checkpoint('real smoke pending'); };
  await executeRelease(g.state, { ...options, until: 'complete' }, g.dependencies);
  assert.equal(g.state.releaseState, 'RELEASED');
  g.state.publicationStarted = true;
  g.actions.postrelease = async () => { throw new Error('A7 confirmed acceptance failure'); };
  await executeRelease(g.state, { ...options, until: 'complete' }, g.dependencies);
  assert.equal(g.state.releaseState, 'RELEASE_INCIDENT');
});

test('reports bind candidate and exact artifact set; SKIP/missing evidence/old candidate fail', t => {
  const f = fixture(t); const file = path.join(f.root, 'report.json');
  const report = reportTemplate(f.state, 'A4', 'd'.repeat(64), ['test']);
  const write = () => fs.writeFileSync(file, JSON.stringify(report));
  write(); assert.throws(() => validateReport(f.state, file, 'A4', 'd'.repeat(64), ['test']), Checkpoint);
  report.tests[0] = { id: 'test', result: 'PASS', expected: { samples: 0 }, measured: { samples: 0 },
    oracle: 'independent audio null-test', evidence: [fileFact(f.evidence)] };
  write(); assert.equal(validateReport(f.state, file, 'A4', 'd'.repeat(64), ['test']).length, 2);
  report.tests[0].measured.samples = 1; write();
  assert.throws(() => validateReport(f.state, file, 'A4', 'd'.repeat(64), ['test']), /measured values fail/);
  report.tests[0].measured.samples = 0;
  report.tests[0].result = 'FAIL'; write();
  assert.throws(() => validateReport(f.state, file, 'A4', 'd'.repeat(64), ['test']), /confirmed acceptance failure/);
  report.tests[0].result = 'SKIP'; write(); assert.throws(() => validateReport(f.state, file, 'A4', 'd'.repeat(64), ['test']), Checkpoint);
  report.tests[0].result = 'PASS'; report.candidate = { ...report.candidate, commit: 'f'.repeat(40) }; write();
  assert.throws(() => validateReport(f.state, file, 'A4', 'd'.repeat(64), ['test']), /mismatch/);
  report.candidate = f.state.candidate; report.tests[0].evidence[0].sha256 = '0'.repeat(64); write();
  assert.throws(() => validateReport(f.state, file, 'A4', 'd'.repeat(64), ['test']), /evidence hash/);
});

test('matrix minimum and private ignored state paths are mandatory', t => {
  const f = fixture(t); assertState(f.state);
  f.state.candidate.commit = 'f'.repeat(40);
  assert.throws(() => assertState(f.state), /source identity/);
  f.state.candidate.commit = SOURCE.commit;
  f.state.requiredHostTests = HOST_TESTS.slice(1); assert.throws(() => assertState(f.state), /cannot be reduced/);
  assert.throws(() => safeStatePath(f.root, 'tracked.json'), /ignored/);
  const dir = path.join(f.root, 'release_state/link'); fs.mkdirSync(path.dirname(dir), { recursive: true });
  fs.symlinkSync(os.tmpdir(), dir, 'junction');
  assert.throws(() => safeStatePath(f.root, `${dir}/state.json`), /symlinks/);
});

test('bundle facts include resources, permissions and symlink targets, not only the executable', t => {
  const f = fixture(t); const dir = path.join(f.root, 'bundle'); fs.mkdirSync(dir);
  fs.writeFileSync(path.join(dir, 'resource'), 'resource'); const fact = treeFact(dir);
  checkFacts([fact]); fs.writeFileSync(path.join(dir, 'resource'), 'changed'); assert.throws(() => checkFacts([fact]), /changed/);
});

test('CI exact commit, complete matrix and workflow must agree; no dispatch route exists', async t => {
  const f = fixture(t); f.state.inputs.ciRun = '123';
  let commit = SOURCE.commit, event = 'push', repository = 'heyalohaloha/kirin_hypha';
  const jobs = ['public history identity', 'release source contract (macos)', 'auval arm64 (AU validation)', 'windows VST3 preflight']
    .map(name => ({ name, status: 'completed', conclusion: 'success' }));
  const run = async (tool, args) => { assert.equal(tool, 'gh'); assert.equal(args[0], 'api');
    return JSON.stringify(args[1].includes('/jobs?') ? { jobs } :
      { head_sha: commit, status: 'completed', conclusion: 'success', path: '.github/workflows/ci.yml', name: 'CI', event, head_repository: { full_name: repository } }); };
  await verifyCi(f.state, run); commit = 'f'.repeat(40);
  await assert.rejects(verifyCi(f.state, run), Checkpoint); commit = SOURCE.commit;
  for (event of ['pull_request', 'pull_request_target', undefined]) await assert.rejects(verifyCi(f.state, run), Checkpoint);
  event = 'push'; repository = 'fixture/fork'; await assert.rejects(verifyCi(f.state, run), Checkpoint);
  repository = 'heyalohaloha/kirin_hypha';
  jobs.push(jobs[0]); await assert.rejects(verifyCi(f.state, run), /not green/); jobs.pop();
  jobs[1].conclusion = 'skipped'; await assert.rejects(verifyCi(f.state, run), /not green/);
});

test('packaging validates actual existing sidecar schemas and all ten upload assets', t => {
  const f = fixture(t); f.state.inputs.windowsInstallerDir = 'windows';
  const files = artifactPaths(f.state);
  const binding = ids => ({ ...updateBinding(''), binaries: ids.map(id => ({ format: id.split('/')[0], role: id.split('/')[1], binarySha256: 'd'.repeat(64), publicKeySha256: '',
    architectures: ['arm64', 'x86_64'].map(architecture => ({ architecture, sha256: 'c'.repeat(64) })) })) });
  const mac = binding(['AU/PRE', 'AU/POST', 'VST3/PRE', 'VST3/POST', 'AAX/PRE', 'AAX/POST']);
  const win = binding(['VST3/PRE', 'VST3/POST', 'AAX/PRE', 'AAX/POST']);
  for (const file of files) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, 'artifact bytes'); }
  fs.writeFileSync(files[2], JSON.stringify({ version: '1.2.3', source: { commit: SOURCE.commit, bNumber: SOURCE.bNumber },
    signed: true, notarized: true, aaxIncluded: true, updateCheck: mac, sha256: hash(fs.readFileSync(files[0])) }));
  fs.writeFileSync(files[5], JSON.stringify({ version: '1.2.3', commit: SOURCE.commit, unsigned_smoke_test: false,
    aax_included: true, git_dirty: '', updateCheck: mac, sha256: hash(fs.readFileSync(files[3])) }));
  fs.writeFileSync(files[8], JSON.stringify({ updateCheck: win, installer: { payload: win.binaries.map(b => ({ role: b.role, format: b.format, binary_sha256: b.binarySha256 })) } }));
  for (const file of [files[0], files[3], files[6]]) fs.writeFileSync(`${file}.sha256`, `${fileFact(file).sha256}  ${path.basename(file)}\n`);
  assert.equal(verifyPackages(f.state).length, 10);
  const zipMetadata = JSON.parse(fs.readFileSync(files[5]));
  zipMetadata.updateCheck.binaries[0].binarySha256 = 'e'.repeat(64);
  fs.writeFileSync(files[5], JSON.stringify(zipMetadata));
  assert.throws(() => verifyPackages(f.state), /PKG and ZIP/);
  zipMetadata.updateCheck.binaries[0].binarySha256 = 'd'.repeat(64);
  zipMetadata.updateCheck.binaries[0].architectures[0].sha256 = 'e'.repeat(64);
  fs.writeFileSync(files[5], JSON.stringify(zipMetadata));
  assert.throws(() => verifyPackages(f.state), /PKG and ZIP/);
  zipMetadata.updateCheck.binaries[0].architectures[0].sha256 = 'c'.repeat(64);
  fs.writeFileSync(files[5], JSON.stringify(zipMetadata));
  fs.appendFileSync(files[0], 'tampered'); assert.throws(() => verifyPackages(f.state), /PKG/);
});
