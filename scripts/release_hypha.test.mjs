import assert from 'node:assert/strict';
import childProcess from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { STAGES, executeRelease, parseReleaseArgs, makeState, realActions, nativeRunner } from './release_hypha.mjs';
import { HOST_TESTS, CI_ORACLES, assertState, hash, digest, fileFact, treeFact, checkFacts,
  validateReport, reportTemplate, safeStatePath, authorization, Checkpoint, acceptanceCriteria, atomicJson } from './ls_release/hypha_release_contract.mjs';
import { verifyCi, verifyPackages, artifactPaths, ZIP_CLEAN_GIT_STATE } from './ls_release/hypha_release_local.mjs';
import { updateBinding } from './updates/update_key_binding.mjs';

const SOURCE = { commit: 'a'.repeat(40), bNumber: 'B-1234', state: 'clean source', fingerprint: 'b'.repeat(64), juce: null };
const options = { execute: true, until: 'hp' };
test('reviewed factory identity is pinned before manufacturing; delivery requires explicit binding', async t => {
  const f = fixture(t);
  const actions = realActions(f.state, {}, async () => { throw new Error('No command may run'); }, () => {});
  await assert.rejects(actions['provenance-inputs'](), /private profile/);
  const options = parseReleaseArgs(['--state','release_state/factory.json',
    '--windows-factory-repository','fixture/factory','--windows-factory-commit','c'.repeat(40)]);
  const state = makeState(f.root,options,() => SOURCE);
  assert.equal(state.inputs.windowsFactoryRepository,'fixture/factory');
  assert.equal(state.inputs.windowsFactoryCommit,'c'.repeat(40));
  assert.equal(state.inputs.windowsPromotionRun, '');
  assert.throws(() => parseReleaseArgs(['--state', 'x', '--windows-promotion-run', '123', '--windows-artifact-archive', 'artifact.zip']), /Bind Windows delivery/);
});
function fixture(t, { delivery = false } = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-release-test-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), '[package]\nversion = "1.2.3"\n');
  const statePath = path.join(root, 'release_state/pipeline.json');
  let state = makeState(root, { state: statePath, date: '2026-10-01',
    ...(delivery ? { windowsFactoryRepository: 'fixture/factory', windowsFactoryCommit: 'c'.repeat(40) } : {}) }, () => SOURCE);
  if (delivery) {
    const archive = path.join(root, 'delivery.zip'); fs.writeFileSync(archive, 'fixture archive');
    atomicJson(statePath, state);
    state.inputs.windowsPromotionRun = '123'; state.inputs.windowsArtifactArchive = archive;
    atomicJson(statePath, state);
  }
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
  const f = fixture(t, { delivery: true });
  await executeRelease(f.state, options, f.dependencies);
  assert.deepEqual(f.calls, STAGES.slice(0, STAGES.indexOf('hp') + 1));
  assert.equal(f.state.releaseState, 'RELEASED');
  assert.equal(f.state.stages.postrelease, undefined);
  await executeRelease(f.state, { ...options, until: 'complete' }, f.dependencies);
  assert.equal(f.calls.length, STAGES.length); assert.equal(f.state.releaseState, 'RELEASE_COMPLETE');
});

test('successful resume verifies retained bytes and never rebuilds/signs/uploads again', async t => {
  const f = fixture(t, { delivery: true }); await executeRelease(f.state, options, f.dependencies);
  const count = f.calls.length; await executeRelease(f.state, options, f.dependencies);
  assert.equal(f.calls.length, count); assert.equal(fs.existsSync(`${f.statePath}.lock`), false);
});

test('missing human report stops downstream stages and resumes only the missing stage', async t => {
  const f = fixture(t, { delivery: true }); f.actions.hosts = async () => { throw new Checkpoint('host report missing'); };
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
  const f = fixture(t, { delivery: true }); await executeRelease(f.state, { ...options, until: 'packages' }, f.dependencies);
  fs.writeFileSync(f.evidence, 'changed'); await executeRelease(f.state, options, f.dependencies);
  assert.equal(f.state.stages.ls, undefined); assert.match(f.state.blocker.reason, /artifact changed/);
  await assert.rejects(executeRelease(f.state, options, { ...f.dependencies,
    snapshot: () => ({ ...SOURCE, fingerprint: 'c'.repeat(64) }) }), /source changed/);
  fs.writeFileSync(`${f.statePath}.lock`, 'another task');
  await assert.rejects(executeRelease(f.state, options, f.dependencies), /EEXIST/);
  assert.equal(fs.readFileSync(`${f.statePath}.lock`, 'utf8'), 'another task');
});

test('release input paths are root-relative and a changed freeze receipt cannot resume', async t => {
  const f = fixture(t, { delivery: true });
  const profile = makeState(f.root, { state: f.statePath, sdk: 'external-sdk', notes: 'release_state/notes.md',
    windowsInstallerDir: 'windows', lsState: 'release_state/ls.json' }, () => SOURCE);
  for (const key of ['sdk', 'notes', 'windowsInstallerDir', 'lsState']) assert.ok(path.isAbsolute(profile.inputs[key]));
  f.actions.freeze = async () => {
    f.state.freeze = { inputs: digest(f.state.inputs), updateCheck: updateBinding(''),
      criteriaSha256: digest(acceptanceCriteria(f.state)) };
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
  const f = fixture(t, { delivery: true });
  f.actions.github = async () => { f.state.publicationStarted = true; throw new Error('asset hash mismatch'); };
  await executeRelease(f.state, options, f.dependencies);
  assert.equal(f.state.releaseState, 'RELEASE_INCIDENT');
  await assert.rejects(executeRelease(f.state, options, f.dependencies), /Incident requires/);
  const g = fixture(t, { delivery: true }); await executeRelease(g.state, options, g.dependencies);
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
  f.state.freeze = { criteriaSha256: digest(acceptanceCriteria(f.state)) };
  const report = reportTemplate(f.state, 'A4', 'd'.repeat(64), ['test']);
  const write = () => fs.writeFileSync(file, JSON.stringify(report));
  write(); assert.throws(() => validateReport(f.state, file, 'A4', 'd'.repeat(64), ['test']), Checkpoint);
  report.tests[0] = { id: 'test', result: 'PASS', expected: { samples: 0 }, measured: { samples: 0 },
    oracle: 'independent audio null-test', evidence: [fileFact(f.evidence)] };
  write(); assert.equal(validateReport(f.state, file, 'A4', 'd'.repeat(64), ['test']).length, 2);
  const frozenCriteria = f.state.freeze.criteriaSha256; f.state.freeze.criteriaSha256 = 'f'.repeat(64);
  assert.throws(() => validateReport(f.state, file, 'A4', 'd'.repeat(64), ['test']), /frozen criteria changed/);
  f.state.freeze.criteriaSha256 = frozenCriteria;
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

const OWNER_DECISION = { schema: 'hypha-owner-acceptance-override-v1', release: '1.2.3', candidateId: null,
  excludedRequiredTest: 'Exact-4-S-samples', excludedResultMustBe: 'NOT_REQUIRED_OWNER_OVERRIDE' };

test('an owner decision lifts only its named row, bound to this candidate and frozen with the criteria', t => {
  const f = fixture(t); const decisionFile = path.join(f.root, 'owner-decision.json');
  fs.writeFileSync(decisionFile, JSON.stringify(OWNER_DECISION));
  const state = makeState(f.root, { state: f.statePath, date: '2026-10-01', ownerOverride: decisionFile }, () => SOURCE);
  state.freeze = { criteriaSha256: digest(acceptanceCriteria(state)) };
  assertState(state);
  assert.deepEqual(state.requiredHostTests, HOST_TESTS);
  assert.deepEqual(state.ownerOverrides.map(o => [o.testId, o.candidateId]), [['Exact-4-S-samples', state.candidate.id]]);
  assert.notEqual(digest(acceptanceCriteria(state)), digest(acceptanceCriteria(f.state)));
  assert.equal(digest(acceptanceCriteria(f.state)), digest({ tests: f.state.requiredHostTests, expected: f.state.expected,
    evidenceMethods: Object.fromEntries(f.state.requiredHostTests.map(id => [id, 'manual'])) }));
  const file = path.join(f.root, 'override-report.json'), ids = ['Exact-4-S-samples', 'pair/combinations'];
  const report = reportTemplate(state, 'A4', 'd'.repeat(64), ids);
  report.tests[0].result = 'NOT_REQUIRED_OWNER_OVERRIDE';
  report.tests[1] = { id: 'pair/combinations', result: 'PASS', expected: { passed: true }, measured: { passed: true },
    oracle: 'retained pair combinations report', evidence: [fileFact(f.evidence)] };
  const write = () => fs.writeFileSync(file, JSON.stringify(report));
  write(); assert.equal(validateReport(state, file, 'A4', 'd'.repeat(64), ids).length, 3);
  assert.throws(() => validateReport(f.state, file, 'A4', 'd'.repeat(64), ids), Checkpoint);
  report.tests[1].result = 'NOT_REQUIRED_OWNER_OVERRIDE'; write();
  assert.throws(() => validateReport(state, file, 'A4', 'd'.repeat(64), ids), /has no owner override/);
  report.tests[1].result = 'PASS'; write();
  assert.throws(() => assertState({ ...state, ownerOverrides: [{ ...state.ownerOverrides[0], candidateId: 'other' }] }),
    /not bound to this candidate/);
  const notes = path.join(f.root, 'notes.md'); fs.writeFileSync(notes, 'not an owner decision');
  const handWritten = { ...state, ownerOverrides: [{ testId: 'pair/combinations', candidateId: state.candidate.id,
    decision: fileFact(notes) }, { testId: 'Exact-4-S-samples', candidateId: state.candidate.id, decision: fileFact(notes) }] };
  assert.throws(() => assertState(handWritten));
  for (const row of report.tests) row.result = 'NOT_REQUIRED_OWNER_OVERRIDE';
  write(); assert.throws(() => validateReport(handWritten, file, 'A4', 'd'.repeat(64), ids));
  const otherRow = path.join(f.root, 'other-row.json');
  fs.writeFileSync(otherRow, JSON.stringify({ ...OWNER_DECISION, excludedRequiredTest: 'Exact-4-S-samples' }));
  handWritten.ownerOverrides = [{ testId: 'pair/combinations', candidateId: state.candidate.id, decision: fileFact(otherRow) }];
  assert.throws(() => validateReport(handWritten, file, 'A4', 'd'.repeat(64), ['pair/combinations']), /decision changed/);
  fs.writeFileSync(decisionFile, JSON.stringify({ ...OWNER_DECISION, ownerAnswer: 'changed' }));
  assert.throws(() => validateReport(state, file, 'A4', 'd'.repeat(64), ids), /decision changed/);
});

test('an owner override added after the freeze stops the release', async t => {
  const f = fixture(t, { delivery: true }); const decisionFile = path.join(f.root, 'owner-decision.json');
  fs.writeFileSync(decisionFile, JSON.stringify(OWNER_DECISION));
  f.actions.freeze = async () => {
    f.state.freeze = { inputs: digest(f.state.inputs), updateCheck: updateBinding(''),
      criteriaSha256: digest(acceptanceCriteria(f.state)) };
    f.state.freeze.sha256 = digest(f.state.freeze);
    return { facts: [fileFact(f.evidence)] };
  };
  await executeRelease(f.state, { ...options, until: 'packages' }, f.dependencies);
  f.state.ownerOverrides = makeState(f.root, { state: f.statePath, date: '2026-10-01', ownerOverride: decisionFile },
    () => SOURCE).ownerOverrides;
  await executeRelease(f.state, options, f.dependencies);
  assert.match(f.state.blocker.reason, /Frozen acceptance criteria changed/);
  assert.equal(f.state.stages.ls, undefined);
});

test('an owner decision for another release, candidate or row is refused, and binds only at --init', t => {
  const f = fixture(t); const file = path.join(f.root, 'owner-decision.json');
  for (const change of [{ release: '1.2.2' }, { candidateId: 'B-1-1.2.3-other' }, { excludedRequiredTest: 'Exact-4-S' },
    { excludedResultMustBe: 'SKIP' }, { schema: 'other' }]) {
    fs.writeFileSync(file, JSON.stringify({ ...OWNER_DECISION, ...change }));
    assert.throws(() => makeState(f.root, { state: f.statePath, date: '2026-10-01', ownerOverride: file }, () => SOURCE),
      /Owner override must name/);
  }
  assert.throws(() => parseReleaseArgs(['--state', 'x', '--owner-override', file]), /only at --init/);
  assert.deepEqual(parseReleaseArgs(['--state', 'x', '--init', '--owner-override', file]).ownerOverride, [file]);
  fs.writeFileSync(file, JSON.stringify(OWNER_DECISION));
  const second = path.join(f.root, 'owner-decision-stress.json');
  fs.writeFileSync(second, JSON.stringify({ ...OWNER_DECISION, excludedRequiredTest: 'pair/combinations' }));
  const parsed = parseReleaseArgs(['--state', f.statePath, '--init', '--owner-override', file, '--owner-override', second]);
  const state = makeState(f.root, { ...parsed, date: '2026-10-01' }, () => SOURCE);
  assert.deepEqual(state.ownerOverrides.map(o => o.testId), ['Exact-4-S-samples', 'pair/combinations']);
  assert.deepEqual(state.requiredHostTests, HOST_TESTS);
  assert.throws(() => makeState(f.root, { state: f.statePath, date: '2026-10-01', ownerOverride: [file, file] }, () => SOURCE),
    /names a row twice/);
});

test('matrix minimum and private ignored state paths are mandatory', t => {
  const f = fixture(t); assertState(f.state);
  for (const format of ['AU', 'VST3', 'AAX']) {
    assert.ok(f.state.requiredHostTests.includes(`macOS/x86_64/${format}`));
    assert.ok(!f.state.requiredHostTests.includes(`macOS/arm64/${format}`));
  }
  assert.ok(f.state.requiredHostTests.includes('Windows/x64/VST3'));
  assert.ok(f.state.requiredHostTests.includes('Windows/x64/AAX'));
  f.state.candidate.commit = 'f'.repeat(40);
  assert.throws(() => assertState(f.state), /source identity/);
  f.state.candidate.commit = SOURCE.commit;
  f.state.requiredHostTests = HOST_TESTS.slice(1); assert.throws(() => assertState(f.state), /cannot be reduced/);
  assert.throws(() => safeStatePath(f.root, 'tracked.json'), /ignored/);
  const dir = path.join(f.root, 'release_state/link'); fs.mkdirSync(path.dirname(dir), { recursive: true });
  fs.symlinkSync(os.tmpdir(), dir, 'junction');
  assert.throws(() => safeStatePath(f.root, `${dir}/state.json`), /symlinks/);
});

test('an added host remains a real evidence checkpoint while the default minimum stays intact', t => {
  const f = fixture(t), optional = 'macOS/arm64/AAX';
  f.state.requiredHostTests.push(optional);
  f.state.expected[optional] = { passed: true };
  assertState(f.state);
  const file = path.join(f.root, 'additional-host.json');
  const report = reportTemplate(f.state, 'A4', 'd'.repeat(64), [optional]);
  fs.writeFileSync(file, JSON.stringify(report));
  assert.throws(() => validateReport(f.state, file, 'A4', 'd'.repeat(64), [optional]), Checkpoint);
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
    .map(name => ({ name, status: 'completed', conclusion: 'success', steps: [] }));
  for (const check of Object.values(CI_ORACLES).flatMap(oracle => oracle.checks)) {
    let job = jobs.find(job => job.name === check.job);
    if (!job) { job = { name: check.job, status: 'completed', conclusion: 'success', steps: [] }; jobs.push(job); }
    if (!job.steps.some(step => step.name === check.step)) job.steps.push({ name: check.step, status: 'completed', conclusion: 'success' });
  }
  for (const [index, job] of jobs.entries()) Object.assign(job, { id: index + 1, run_id: 123, head_sha: SOURCE.commit, run_attempt: 1 });
  const run = async (tool, args) => { assert.equal(tool, 'gh'); assert.equal(args[0], 'api');
    return JSON.stringify(args[1].includes('/jobs?') ? { total_count: jobs.length, jobs } :
      { head_sha: commit, run_attempt: 1, status: 'completed', conclusion: 'success', path: '.github/workflows/ci.yml', name: 'CI', event, head_repository: { full_name: repository } }); };
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
    aax_included: true, git_dirty: 'false', updateCheck: mac, sha256: hash(fs.readFileSync(files[3])) }));
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

test('ZIP clean-source state uses the producer schema; dirty, unknown, missing or retyped values stop', t => {
  const f = fixture(t); f.state.inputs.windowsInstallerDir = 'windows';
  const files = artifactPaths(f.state);
  // The producer writes this exact string (xtask release_gate GIT_CLEAN_STATE); bind both sides.
  const producer = fs.readFileSync(path.join(import.meta.dirname, '..', 'xtask/src/release_gate.rs'), 'utf8');
  assert.equal(producer.match(/pub const GIT_CLEAN_STATE: &str = "([^"]*)";/)?.[1], ZIP_CLEAN_GIT_STATE);
  assert.match(producer, /Ok\(entries\) if entries\.is_empty\(\) => GIT_CLEAN_STATE\.to_string\(\)/);
  const binding = ids => ({ ...updateBinding(''), binaries: ids.map(id => ({ format: id.split('/')[0], role: id.split('/')[1], binarySha256: 'd'.repeat(64), publicKeySha256: '',
    architectures: ['arm64', 'x86_64'].map(architecture => ({ architecture, sha256: 'c'.repeat(64) })) })) });
  const mac = binding(['AU/PRE', 'AU/POST', 'VST3/PRE', 'VST3/POST', 'AAX/PRE', 'AAX/POST']);
  const win = binding(['VST3/PRE', 'VST3/POST', 'AAX/PRE', 'AAX/POST']);
  for (const file of files) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, 'artifact bytes'); }
  fs.writeFileSync(files[2], JSON.stringify({ version: '1.2.3', source: { commit: SOURCE.commit, bNumber: SOURCE.bNumber },
    signed: true, notarized: true, aaxIncluded: true, updateCheck: mac, sha256: hash(fs.readFileSync(files[0])) }));
  fs.writeFileSync(files[8], JSON.stringify({ updateCheck: win, installer: { payload: win.binaries.map(b => ({ role: b.role, format: b.format, binary_sha256: b.binarySha256 })) } }));
  for (const file of [files[0], files[3], files[6]]) fs.writeFileSync(`${file}.sha256`, `${fileFact(file).sha256}  ${path.basename(file)}\n`);
  // Same shape and string typing as release_package_metadata::manifest_json.
  const zip = { updateCheck: mac, product: 'Kirin Hypha', version: '1.2.3', commit: SOURCE.commit,
    package: 'Kirin-Hypha-1.2.3-macOS-Universal.zip', sha256: hash(fs.readFileSync(files[3])),
    unsigned_smoke_test: false, git_dirty: 'false', ship_set: 'juce-common-shell+aax', aax_included: true, bundles: [] };
  const write = value => fs.writeFileSync(files[5], `${JSON.stringify(value, null, 2)}\n`);
  write(zip); assert.equal(verifyPackages(f.state).length, 10);
  for (const gitDirty of ['true: M crates/fixture.rs', 'unknown: git status failed', '', false, 0, null, 'False', ' false']) {
    write({ ...zip, git_dirty: gitDirty });
    assert.throws(() => verifyPackages(f.state), /ZIP release identity/, JSON.stringify(gitDirty));
  }
  const { git_dirty: _omitted, ...missing } = zip; write(missing);
  assert.throws(() => verifyPackages(f.state), /ZIP release identity/);
  write({ ...zip, commit: 'f'.repeat(40) });
  assert.throws(() => verifyPackages(f.state), /ZIP release identity/);
});

test('native runner writes binary stdout to a new owned file and never overwrites it', async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-runner-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const file = path.join(root, 'asset.bin');
  const run = nativeRunner(root);
  const script = 'process.stdout.write(Buffer.from([0, 255, 1, 10, 13]))';
  assert.equal(await run(process.execPath, ['-e', script], { stdoutFile: file }), '');
  assert.deepEqual(fs.readFileSync(file), Buffer.from([0, 255, 1, 10, 13]));
  assert.equal(fs.statSync(file).mode & 0o777, 0o600);
  await assert.rejects(run(process.execPath, ['-e', script], { stdoutFile: file }), /EEXIST/);
  await assert.rejects(run(process.execPath, ['-e', 'process.exit(3)'], { stdoutFile: path.join(root, 'failed.bin') }), /exit 3/);
});
