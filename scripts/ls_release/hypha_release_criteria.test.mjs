import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { makeState, parseReleaseArgs, realActions, executeRelease, bindWindowsPromotion, STAGES } from '../release_hypha.mjs';
import { SCHEMA, HOST_TESTS, DISTRIBUTION_TESTS, POST_TESTS, SAFETY_EXPECTED, assertState, digest, fileFact, reportTemplate,
  validateReport, acceptanceCriteria, atomicJson, Checkpoint } from './hypha_release_contract.mjs';
import { FIX_ROWS_SCHEMA, OWNER_DECISION_SCHEMA, OWNER_DEFERRED, CI_ORACLES,
  hostTestIds, bindOwnerDecisions, checkOwnerDecisions, checkFixRows, ciFixRows } from './hypha_release_criteria.mjs';
import { verifyCi } from './hypha_release_local.mjs';
import { updateBinding } from '../updates/update_key_binding.mjs';

const SOURCE = { commit: 'a'.repeat(40), bNumber: 'B-1', fingerprint: 'b'.repeat(64) };
function fixture(t, { delivery = false } = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-criteria-'));
  assert.ok(fs.realpathSync(root).startsWith(`${fs.realpathSync(os.tmpdir())}${path.sep}`));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const write = (name, value) => {
    const file = path.isAbsolute(name) ? name : path.join(root, name);
    assert.ok(file.startsWith(`${root}${path.sep}`)); fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, JSON.stringify(value)); return file;
  };
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
  const row = { id: 'fixed-clock', kind: 'host', hosts: ['macOS/x86_64/AU'], depth: 'deep',
    oracle: 'independent callback capture', expected: { lostFrames: 0 } };
  const spec = { schema: FIX_ROWS_SCHEMA, release: '1.2.3', rows: [row,
    { ...row, id: 'fixed-ci', kind: 'ci', hosts: [], oracle: 'pair-recovery-parity', expected: { passed: true } }] };
  const fixes = write('fixes.json', spec);
  const statePath = path.join(root, 'release_state/profile.json');
  let state = makeState(root, { state: statePath, fixRows: fixes,
    ...(delivery ? { windowsFactoryRepository: 'fixture/factory', windowsFactoryCommit: 'c'.repeat(40) } : {}) }, () => SOURCE);
  if (delivery) {
    const archive = write('delivery.zip', 'fixture archive');
    atomicJson(statePath, state);
    state = bindWindowsPromotion(statePath, { bindWindowsPromotion: '123', windowsArtifactArchive: archive }, () => SOURCE);
  }
  const freeze = () => {
    state.freeze = { inputs: digest(state.inputs), criteriaSha256: digest(acceptanceCriteria(state)), updateCheck: updateBinding('') };
    state.freeze.sha256 = digest(state.freeze);
  };
  freeze();
  const decision = ids => write('decision.json', { schema: OWNER_DECISION_SCHEMA, release: '1.2.3', candidateId: state.candidate.id,
    rows: ids.map(id => ({ id, reason: 'Host test unavailable', resumeCondition: 'Validate with the next candidate',
      releaseNote: 'This host check is deferred for this release.' })) });
  return { root, write, spec, fixes, state, statePath, freeze, decision };
}

test('versioned fix rows bind host and CI expectations without requesting a host PASS for CI', t => {
  const f = fixture(t); assertState(f.state);
  assert.equal(SCHEMA, 'kirin-hypha-release-pipeline-v2');
  assert.ok(hostTestIds(f.state).includes('fixed-clock'));
  assert.ok(!hostTestIds(f.state).includes('fixed-ci'));
  assert.deepEqual(f.state.expected['fixed-clock'], { lostFrames: 0 });
  assert.throws(() => assertState({ ...f.state, schema: 'kirin-hypha-release-pipeline-v1' }));
  assert.throws(() => ciFixRows(f.state));
  f.state.stages.ci = { evidence: { oracles: [{ name: 'pair-recovery-parity', checks: [{ conclusion: 'success' }] }] } };
  assert.equal(ciFixRows(f.state)[0].result, 'PASS');
  f.state.fixRows.rows[1].expected = { lostFrames: 0 };
  assert.throws(() => ciFixRows(f.state), Checkpoint);
  f.state.fixRows.rows[1].expected = { passed: true };
  f.state.stages.ci.evidence.oracles[0].checks[0].conclusion = 'skipped';
  assert.throws(() => ciFixRows(f.state));
  const changed = structuredClone(f.state); changed.fixRows.rows[0].expected.lostFrames = 1;
  assert.throws(() => assertState(changed));
  f.write('fixes.json', { ...f.spec, rows: f.spec.rows.slice(1) });
  assert.throws(() => assertState(f.state));
});

test('fix rows refuse duplicate or reserved ids, missing kinds and unknown CI oracles', t => {
  const f = fixture(t);
  for (const patch of [{ id: HOST_TESTS[0] }, { kind: undefined }, { kind: 'other' },
    { hosts: [] }, { expected: {} }, { depth: 'shallow' }, { oracle: '' }, { expected: null },
    { kind: 'ci', oracle: 'missing' }]) {
    f.write('fixes.json', { ...f.spec, rows: [{ ...f.spec.rows[0], ...patch }] });
    assert.throws(() => makeState(f.root, { state: f.statePath, fixRows: f.fixes }, () => SOURCE));
  }
  f.write('fixes.json', { ...f.spec, rows: [f.spec.rows[0], f.spec.rows[0]] });
  assert.throws(() => makeState(f.root, { state: f.statePath, fixRows: f.fixes }, () => SOURCE));
  assert.throws(() => parseReleaseArgs(['--state', 'x', '--fix-rows', f.fixes]));
  assert.throws(() => parseReleaseArgs(['--state', 'x', '--owner-decision', 'decision.json']));
  assert.deepEqual(parseReleaseArgs(['--state', 'x', '--execute', '--owner-decision', 'a', '--owner-decision', 'b']).ownerDecision, ['a', 'b']);
});

for (const [gate, ids] of [['distribution', DISTRIBUTION_TESTS], ['postrelease', POST_TESTS]]) {
  test(`fix rows cannot replace reserved ${gate} expectations at initialization or revalidation`, t => {
    const f = fixture(t);
    for (const id of ids) {
      const row = { ...f.spec.rows[0], id, expected: { signatureFailures: 1 } };
      f.write('fixes.json', { ...f.spec, rows: [row] });
      assert.throws(() => makeState(f.root, { state: f.statePath, fixRows: f.fixes }, () => SOURCE));
      const bound = { ...f.state, fixRows: { rows: [row], decision: fileFact(f.fixes) },
        expected: { ...f.state.expected, [id]: row.expected } };
      assert.throws(() => checkFixRows(bound));
    }
  });
}

test('deferred acceptance binds multiple host and fix rows after freeze without changing the freeze', t => {
  const f = fixture(t), ids = [HOST_TESTS[0], 'fixed-clock', 'fixed-ci'];
  const frozen = digest(f.state.freeze), criteria = digest(acceptanceCriteria(f.state));
  const decision = f.decision(ids); bindOwnerDecisions(f.state, [decision]);
  bindOwnerDecisions(f.state, [decision]);
  assert.equal(f.state.ownerDecisions.length, 1);
  assert.equal(digest(f.state.freeze), frozen); assert.equal(digest(acceptanceCriteria(f.state)), criteria);
  assert.equal(ciFixRows(f.state)[0].result, OWNER_DEFERRED);
  const report = reportTemplate(f.state, 'A4', f.state.freeze.sha256, ids.slice(0, 2));
  report.tests.forEach(row => { row.result = OWNER_DEFERRED; });
  const file = f.write('report.json', report);
  assert.ok(validateReport(f.state, file, 'A4', f.state.freeze.sha256, ids.slice(0, 2)).some(fact => fact.path === decision));
  assert.throws(() => validateReport({ ...f.state, ownerDecisions: [] }, file, 'A4', f.state.freeze.sha256, ids.slice(0, 2)), Checkpoint);
  f.write('decision.json', { changed: true });
  assert.throws(() => checkOwnerDecisions(f.state));
});

test('owner deferrals refuse safety rows, unknown ids, other candidates and incomplete explanations', t => {
  const f = fixture(t);
  for (const id of [...Object.keys(SAFETY_EXPECTED), 'unknown']) assert.throws(() => bindOwnerDecisions(f.state, [f.decision([id])]));
  const decision = f.decision(['fixed-clock']);
  const original = JSON.parse(fs.readFileSync(decision));
  for (const field of ['reason', 'resumeCondition', 'releaseNote']) {
    f.write('decision.json', { ...original, rows: [{ ...original.rows[0], [field]: ' ' }] });
    assert.throws(() => bindOwnerDecisions(f.state, [decision]));
  }
  for (const patch of [{ release: '1.2.2' }, { candidateId: 'other' }, { rows: [] }, { rows: [original.rows[0], original.rows[0]] }]) {
    f.write('decision.json', { ...original, ...patch }); assert.throws(() => bindOwnerDecisions(f.state, [decision]));
  }
  f.write('decision.json', original);
  assert.throws(() => bindOwnerDecisions({ ...f.state, freeze: undefined }, [decision]));
  bindOwnerDecisions(f.state, [decision]);
  assert.ok(f.state.ownerDecisions.some(bound => bound.rows.some(row => row.id === 'fixed-clock')));
});

test('hosts execution persists the decision before checking the report and resumes a confirmed host failure', async t => {
  const f = fixture(t, { delivery: true }), decision = f.decision(['fixed-clock', 'fixed-ci']);
  const report = reportTemplate(f.state, 'A4', f.state.freeze.sha256, hostTestIds(f.state));
  const evidence = f.write('evidence.json', { independent: true });
  for (const row of report.tests) Object.assign(row, { result: 'PASS', measured: row.expected,
    oracle: 'independent retained host run', evidence: [fileFact(evidence)] });
  report.tests.find(row => row.id === 'fixed-clock').result = 'FAIL';
  const reportPath = f.write(f.state.inputs.hostsReport, report);
  const save = () => f.write('saved.json', f.state);
  const real = realActions(f.state, {}, async () => { throw new Error('No external command'); }, save);
  const actions = Object.fromEntries(STAGES.map(stage => [stage, async () => ({ facts: [fileFact(evidence)] })]));
  actions.hosts = real.hosts;
  const deps = { statePath: f.statePath, snapshot: () => SOURCE, clean: () => {}, actions, log: () => {} };
  bindOwnerDecisions(f.state, [decision]); // The CI-only row has a deliberate deferred decision.
  await executeRelease(f.state, { execute: true, until: 'packages' }, deps);
  assert.equal(f.state.stages.hosts.status, 'FAIL'); assert.equal(f.state.stages.packages, undefined);
  const failedReport = fileFact(reportPath);
  report.tests.find(row => row.id === 'fixed-clock').result = OWNER_DEFERRED;
  f.write(reportPath, report);
  await executeRelease(f.state, { execute: true, until: 'packages', ownerDecision: [decision] }, deps);
  assert.equal(f.state.stages.hosts.status, 'PASS'); assert.equal(f.state.stages.packages.status, 'PASS');
  assert.ok(f.state.stages.hosts.previous?.some(attempt => attempt.status === 'FAIL'
    && attempt.facts.some(fact => fact.sha256 === failedReport.sha256
      && fs.realpathSync(fact.path) === fs.realpathSync(failedReport.path))),
  JSON.stringify({ previous: f.state.stages.hosts.previous, failedReport }));
  assert.equal(JSON.parse(fs.readFileSync(f.statePath)).ownerDecisions[0].decision.sha256, fileFact(decision).sha256);
  assert.equal(fs.existsSync(`${f.statePath}.lock`), false);
});

test('invalid owner decision input checkpoints without recording a host failure and can be corrected', async t => {
  const f = fixture(t, { delivery: true }), decision = f.decision(['fixed-clock', 'fixed-ci']);
  const valid = JSON.parse(fs.readFileSync(decision));
  const evidence = f.write('evidence.json', { captured: true });
  const actions = Object.fromEntries(STAGES.map(stage => [stage, async () => ({ facts: [fileFact(evidence)] })]));
  let hostRuns = 0;
  actions.hosts = async () => { hostRuns++; return { facts: [fileFact(evidence)] }; };
  const deps = { statePath: f.statePath, snapshot: () => SOURCE, clean: () => {}, actions, log: () => {} };
  for (const invalid of [{ ...valid, schema: 'unsupported' }, { ...valid, rows: [{ ...valid.rows[0], reason: '' }] }, null]) {
    if (invalid) f.write('decision.json', invalid); else fs.writeFileSync(decision, '{');
    await executeRelease(f.state, { execute: true, until: 'packages', ownerDecision: [decision] }, deps);
    assert.equal(f.state.blocker.stage, 'hosts');
    assert.notEqual(f.state.stages.hosts?.status, 'FAIL');
    assert.equal(hostRuns, 0); assert.equal(f.state.stages.packages, undefined);
    assert.equal(f.state.ownerDecisions, undefined);
    assert.equal(fs.existsSync(`${f.statePath}.lock`), false);
  }
  f.write('decision.json', valid);
  await executeRelease(f.state, { execute: true, until: 'packages', ownerDecision: [decision] }, deps);
  assert.equal(hostRuns, 1); assert.equal(f.state.stages.packages.status, 'PASS');
});

test('CI oracle names describe their evidence and every job and step exists in the workflow', () => {
  assert.ok(CI_ORACLES['pair-recovery-parity']);
  assert.ok(CI_ORACLES['editor-surface-render']);
  const workflow = fs.readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8');
  const verifyWorkflow = source => {
    const jobs = new Map();
    for (const block of source.split(/(?=^  [\w-]+:\s*$)/m)) {
      const name = block.match(/^    name: (.+)$/m)?.[1];
      if (!name) continue;
      const phases = [...block.matchAll(/^\s+- phase: (\w+)$/gm)].map(match => match[1]);
      const names = name.includes('${{ matrix.phase }}')
        ? phases.map(phase => name.replace('${{ matrix.phase }}', phase)) : [name];
      const steps = [...block.matchAll(/^      - name: (.+)$/gm)].map(match => match[1]);
      for (const expanded of names) jobs.set(expanded, steps);
    }
    for (const oracle of Object.values(CI_ORACLES)) {
      for (const check of oracle.checks) assert.ok(jobs.get(check.job)?.includes(check.step), `${check.job}/${check.step}`);
    }
  };
  verifyWorkflow(workflow);
  assert.throws(() => verifyWorkflow(workflow.replace('name: Test shipping source contract', 'name: Renamed source step')));
  assert.throws(() => verifyWorkflow(workflow.replace('- phase: native3', '- phase: other')));
  const native = fs.readFileSync(new URL('../../juce_shell/cmake/LocalBlind.cmake', import.meta.url), 'utf8')
    + fs.readFileSync(new URL('../../juce_shell/CMakeLists.txt', import.meta.url), 'utf8');
  for (const id of ['editor-surface-render', 'editor-reachability']) {
    for (const name of CI_ORACLES[id].tests) assert.ok(native.includes(`add_test(NAME ${name} `), name);
  }
});

function greenJobs() {
  const jobs = ['public history identity', 'release source contract (macos)', 'auval arm64 (AU validation)', 'windows VST3 preflight']
    .map((name, index) => ({ id: index + 1, name, status: 'completed', conclusion: 'success', steps: [] }));
  for (const check of Object.values(CI_ORACLES).flatMap(oracle => oracle.checks)) {
    let job = jobs.find(job => job.name === check.job);
    if (!job) { job = { id: jobs.length + 1, name: check.job, status: 'completed', conclusion: 'success', steps: [] }; jobs.push(job); }
    if (!job.steps.some(step => step.name === check.step)) job.steps.push({ name: check.step, status: 'completed', conclusion: 'success' });
  }
  return jobs.map(job => ({ ...job, run_id: 123, head_sha: SOURCE.commit, run_attempt: 1 }));
}

test('CI oracles require the named successful jobs and steps, including all native shards', async t => {
  const f = fixture(t); f.state.inputs.ciRun = '123';
  const jobs = greenJobs();
  const run = async (_tool, args) => JSON.stringify(args[1].includes('/jobs?') ? { total_count: jobs.length, jobs } : {
    head_sha: SOURCE.commit, run_attempt: 1, status: 'completed', conclusion: 'success', path: '.github/workflows/ci.yml',
    name: 'CI', event: 'push', head_repository: { full_name: 'heyalohaloha/kirin_hypha' } });
  const result = await verifyCi(f.state, run);
  assert.deepEqual(new Set(result.oracles.map(oracle => oracle.name)), new Set(Object.keys(CI_ORACLES)));
  const shard = jobs.find(job => job.name === 'release source native3 (macos)');
  shard.steps[0].conclusion = 'skipped'; await assert.rejects(verifyCi(f.state, run), Checkpoint);
  shard.steps[0].conclusion = 'success'; shard.steps = []; await assert.rejects(verifyCi(f.state, run), Checkpoint);
});

test('latest per-job attempts can differ within one exact-commit run and remain recorded', async t => {
  const f = fixture(t); f.state.inputs.ciRun = '123';
  const jobs = greenJobs();
  jobs.find(job => job.name === 'release source ffi (macos)').run_attempt = 2;
  const record = { head_sha: SOURCE.commit, run_attempt: 2, status: 'completed', conclusion: 'success',
    path: '.github/workflows/ci.yml', name: 'CI', event: 'push', head_repository: { full_name: 'heyalohaloha/kirin_hypha' } };
  const requests = [];
  const run = async (_tool, args) => {
    requests.push(args[1]);
    if (!args[1].includes('/jobs?')) return JSON.stringify(record);
    const page = new URLSearchParams(args[1].split('?')[1]);
    assert.equal(page.get('filter'), 'latest');
    return JSON.stringify({ total_count: jobs.length, jobs: page.get('page') === '1' ? jobs.slice(0, 4) : jobs.slice(4) });
  };
  const evidence = await verifyCi(f.state, run);
  assert.equal(evidence.jobs.find(job => job.name === 'release source ffi (macos)').attempt, 2);
  assert.equal(evidence.jobs.find(job => job.name === 'windows VST3 preflight').attempt, 1);
  assert.equal(evidence.attempt, 2);
  assert.ok(requests.some(url => url.includes('page=2')));
  const changed = jobs.find(job => job.name === 'release source ffi (macos)');
  for (const patch of [{ head_sha: 'f'.repeat(40) }, { run_id: 124 }, { run_attempt: 3 },
    { run_attempt: 0 }, { conclusion: 'failure' }]) {
    const old = { ...changed }; Object.assign(changed, patch);
    await assert.rejects(verifyCi(f.state, run), Checkpoint);
    Object.assign(changed, old);
  }
});

test('a changed bound fix file stops the next stage before packaging', async t => {
  const f = fixture(t), evidence = f.write('evidence.json', { captured: true });
  const actions = Object.fromEntries(STAGES.map(stage => [stage, async () => ({ facts: [fileFact(evidence)] })]));
  let packaged = false;
  actions.packages = async () => { packaged = true; return { facts: [fileFact(evidence)] }; };
  actions.hosts = async () => {
    f.write('fixes.json', { ...f.spec, release: '1.2.2' });
    return { facts: [fileFact(evidence)] };
  };
  await executeRelease(f.state, { execute: true, until: 'packages' }, {
    statePath: f.statePath, snapshot: () => SOURCE, clean: () => {}, actions, log: () => {} });
  assert.equal(packaged, false);
  assert.ok(f.state.blocker);
  assert.equal(fs.existsSync(`${f.statePath}.lock`), false);
});

test('missing job attempts checkpoint without consulting inferred attempt catalogs', async t => {
  const f = fixture(t); f.state.inputs.ciRun = '123';
  const jobs = greenJobs();
  const record = { head_sha: SOURCE.commit, run_attempt: 2, status: 'completed', conclusion: 'success',
    path: '.github/workflows/ci.yml', name: 'CI', event: 'push', head_repository: { full_name: 'heyalohaloha/kirin_hypha' } };
  const rerun = jobs.find(job => job.name === 'release source ffi (macos)');
  const requests = [];
  const run = async (_tool, args) => {
    const endpoint = args[1];
    requests.push(endpoint);
    if (!endpoint.includes('/jobs?')) return JSON.stringify(record);
    const selected = endpoint.includes('/attempts/2/') ? [rerun] : endpoint.includes('/attempts/1/')
      ? jobs.filter(job => job.id !== rerun.id) : jobs;
    return JSON.stringify({ total_count: selected.length, jobs: selected });
  };
  await verifyCi(f.state, run); // Positive control: the same run has usable explicit metadata.
  for (const job of jobs) {
    requests.length = 0;
    const attempt = job.run_attempt; delete job.run_attempt;
    await assert.rejects(verifyCi(f.state, run), Checkpoint);
    assert.ok(requests.every(endpoint => !endpoint.includes('/attempts/')));
    job.run_attempt = attempt;
  }
});
