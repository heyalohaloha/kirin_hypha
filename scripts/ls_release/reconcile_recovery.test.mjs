import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import * as release from '../release_hypha.mjs';
import { assertState, atomicJson, checkFacts, fileFact } from './hypha_release_contract.mjs';
import { updateBinding } from '../updates/update_key_binding.mjs';

function fixture(t) {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-recovery-')));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
  const source = { commit: 'a'.repeat(40), bNumber: 'B-1', state: 'clean source', fingerprint: 'b'.repeat(64) };
  const snapshot = () => source, statePath = path.join(root, 'release_state/state.json');
  const state = release.makeState(root, { state: statePath }, snapshot);
  const signed = path.join(root, 'signed.bin'); fs.writeFileSync(signed, 'signed fixture');
  for (const stage of ['provenance-inputs', 'ci', 'source-delivery', 'macos-au-vst3', 'macos-aax'])
    state.stages[stage] = { status: 'PASS', facts: [fileFact(signed)] };
  state.stages.windows = { status: 'FAIL', reason: 'interrupted' };
  state.blocker = { stage: 'windows', reason: 'interrupted' }; atomicJson(statePath, state);
  return { root, statePath, state, signed, snapshot };
}

test('inspect, plan and explicit apply retain the failed attempt and signed output without signing', async t => {
  const f = fixture(t); let signatures = 0;
  const verify = async (state, stage) => { assert.equal(stage, 'windows'); return { facts: [fileFact(f.signed)] }; };
  const inspect = await release.inspectRelease(f.statePath, { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify });
  const plan = await release.planReleaseReconciliation(f.statePath, inspect, { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify });
  assert.equal(signatures, 0); assert.deepEqual(JSON.parse(fs.readFileSync(f.statePath)), f.state);
  const backup = `${f.statePath}.reconcile-${plan.inspection.profileSha256}.json`;
  fs.copyFileSync(f.statePath, backup, fs.constants.COPYFILE_EXCL); // a previous apply stopped after preserving the profile
  await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify });
  const after = JSON.parse(fs.readFileSync(f.statePath));
  assert.equal(after.stages.windows.status, 'PASS');
  assert.equal(after.stages.windows.previous.at(-1).status, 'FAIL');
  assert.deepEqual(JSON.parse(fs.readFileSync(backup)), f.state);
  assert.deepEqual(after.stages['macos-aax'], f.state.stages['macos-aax']);
  assert.equal(fs.readFileSync(f.signed, 'utf8'), 'signed fixture');
  const actions = Object.fromEntries(release.STAGES.map(stage => [stage, async () => {
    if (stage === 'macos-aax') { signatures++; throw new Error('must not sign again'); }
    return { facts: [fileFact(f.signed)] };
  }]));
  await release.executeRelease(after, { execute: true, until: 'packages' },
    { statePath: f.statePath, snapshot: f.snapshot, clean: () => {}, actions, log: () => {} });
  assert.equal(signatures, 0); assert.equal(after.stages['macos-aax'].status, 'PASS');
});

test('apply rejects stale profile, a changed plan and failed reinspection', async t => {
  const f = fixture(t), verify = async () => ({ facts: [fileFact(f.signed)] });
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify };
  const inspect = await release.inspectRelease(f.statePath, deps);
  const plan = await release.planReleaseReconciliation(f.statePath, inspect, deps);
  await assert.rejects(release.applyReleaseReconciliation(f.statePath, plan, '0'.repeat(64), deps), /hash/i);
  const tampered = structuredClone(plan); tampered.result.facts[0].sha256 = '0'.repeat(64);
  await assert.rejects(release.applyReleaseReconciliation(f.statePath, tampered, plan.sha256, deps), /hash/i);
  fs.writeFileSync(f.signed, 'changed');
  await assert.rejects(release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps), /changed|evidence/i);
  assert.deepEqual(JSON.parse(fs.readFileSync(f.statePath)), f.state);
});

test('unsupported failure, missing prerequisite, incident and changed profile cannot be reconciled', async t => {
  const f = fixture(t), verify = async () => ({ facts: [fileFact(f.signed)] });
  for (const mutate of [
    state => { state.blocker.stage = 'macos-au-vst3'; state.stages['macos-au-vst3'].status = 'FAIL'; },
    state => { delete state.stages.ci; },
    state => { state.releaseState = 'RELEASE_INCIDENT'; },
  ]) {
    const state = structuredClone(f.state); mutate(state); atomicJson(f.statePath, state);
    const inspection = await release.inspectRelease(f.statePath, { snapshot: f.snapshot });
    await assert.rejects(release.planReleaseReconciliation(f.statePath, inspection, { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify }));
    assert.deepEqual(JSON.parse(fs.readFileSync(f.statePath)), state);
  }
  atomicJson(f.statePath, f.state);
  const inspection = await release.inspectRelease(f.statePath, { snapshot: f.snapshot });
  const plan = await release.planReleaseReconciliation(f.statePath, inspection, { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify });
  const altered = { ...f.state, blocker: { ...f.state.blocker, reason: 'new information' } };
  atomicJson(f.statePath, altered);
  await assert.rejects(release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify }), /changed/i);
  assert.deepEqual(JSON.parse(fs.readFileSync(f.statePath)), altered);
});

test('reconciliation CLI requires separate operations and an explicit plan hash', () => {
  assert.equal(release.parseReleaseArgs(['--state', 'x', '--inspect']).inspect, true);
  assert.equal(release.parseReleaseArgs(['--state', 'x', '--reconcile-plan']).reconcilePlan, true);
  assert.throws(() => release.parseReleaseArgs(['--state', 'x', '--reconcile-apply', 'plan.json']), /sha256/);
  assert.throws(() => release.parseReleaseArgs(['--state', 'x', '--inspect', '--execute']), /separate/);
  assert.throws(() => release.parseReleaseArgs(['--state', 'x', '--reconcile-plan', '--inspect']), /separate/);
});

test('publication incident is cleared only after the failed HP stage is reverified', async t => {
  const f = fixture(t), state = structuredClone(f.state);
  for (const stage of ['windows', 'freeze', 'hosts', 'packages', 'provenance', 'distribution', 'ls', 'github'])
    state.stages[stage] = { status: 'PASS', facts: [fileFact(f.signed)] };
  state.freeze = { source: state.source, publicationNotes: fileFact(f.signed), updateCheck: updateBinding(''), inputs: (await import('./hypha_release_contract.mjs')).digest(state.inputs),
    criteriaSha256: (await import('./hypha_release_contract.mjs')).digest((await import('./hypha_release_contract.mjs')).acceptanceCriteria(state)),
    payloads: [fileFact(f.signed)] };
  state.freeze.sha256 = (await import('./hypha_release_contract.mjs')).digest(state.freeze);
  state.hpCommit = 'c'.repeat(40); state.releaseState = 'RELEASE_INCIDENT'; state.publicationStarted = true;
  state.stages.hp = { status: 'FAIL', reason: 'public readback mismatch' };
  state.blocker = { stage: 'hp', reason: 'public readback mismatch' }; atomicJson(f.statePath, state);
  let reads = 0;
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify: async (_state, stage) => {
    assert.equal(stage, 'hp'); reads++; return { facts: [fileFact(f.signed)], evidence: { hpCommit: state.hpCommit } };
  } };
  const inspection = await release.inspectRelease(f.statePath, deps);
  const plan = await release.planReleaseReconciliation(f.statePath, inspection, deps);
  const result = await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps);
  assert.equal(reads, 2); assert.equal(result.releaseState, 'RELEASED');
  assert.equal(result.stages.hp.previous.at(-1).status, 'FAIL');
  assert.deepEqual(result.stages['macos-aax'], state.stages['macos-aax']);
});


test('retained AAX verification restores only that failed stage and never invokes its producer', async t => {
  const f = fixture(t), state = structuredClone(f.state);
  state.stages['macos-aax'] = { status: 'RUNNING', startedAt: 'interrupted' };
  delete state.stages.windows; state.blocker = { stage: 'macos-aax', reason: 'interrupted' };
  atomicJson(f.statePath, state); let inspections = 0;
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify: async (_state, stage) => {
    assert.equal(stage, 'macos-aax'); inspections++; return { facts: [fileFact(f.signed)] };
  } };
  const inspection = await release.inspectRelease(f.statePath, deps);
  const plan = await release.planReleaseReconciliation(f.statePath, inspection, deps);
  const result = await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps);
  assert.equal(inspections, 2); assert.equal(result.stages['macos-aax'].status, 'PASS');
  assert.equal(result.stages['macos-aax'].previous[0].status, 'RUNNING');
  assert.equal(fs.readFileSync(f.signed, 'utf8'), 'signed fixture');
});

test('LS incident applies only after retained downloads and two successful API readbacks', async t => {
  const { digest, acceptanceCriteria, reportTemplate } = await import('./hypha_release_contract.mjs');
  const f = fixture(t), state = structuredClone(f.state);
  for (const stage of ['windows', 'freeze', 'hosts', 'packages', 'provenance', 'distribution'])
    state.stages[stage] = { status: 'PASS', facts: [fileFact(f.signed)] };
  state.freeze = { source: state.source, publicationNotes: fileFact(f.signed), updateCheck: updateBinding(''), inputs: digest(state.inputs), criteriaSha256: digest(acceptanceCriteria(state)), payloads: [fileFact(f.signed)] };
  state.freeze.sha256 = digest(state.freeze);
  state.inputs.lsState = path.join(f.root, 'targets.json');
  // Freeze the final input values before constructing the fixture reports.
  state.freeze.inputs = digest(state.inputs); delete state.freeze.sha256; state.freeze.sha256 = digest(state.freeze);
  fs.writeFileSync(state.inputs.lsState, JSON.stringify({ lemonSqueezy: { products: [{ productId: 1 }] } }));
  const report = reportTemplate(state, 'A6-LS', digest(state.stages.packages.facts), ['LS/1']);
  report.tests[0] = { id: 'LS/1', result: 'PASS', expected: { sha256: fileFact(f.signed).sha256 },
    measured: { sha256: fileFact(f.signed).sha256 }, oracle: 'retained download', evidence: [fileFact(f.signed)] };
  fs.mkdirSync(path.dirname(path.join(f.root, state.inputs.lsReport)), { recursive: true });
  fs.writeFileSync(path.join(f.root, state.inputs.lsReport), JSON.stringify(report));
  state.releaseState = 'RELEASE_INCIDENT'; state.publicationStarted = true;
  const oldApi = path.join(f.root, 'failed-api.json'); fs.writeFileSync(oldApi, 'old failure evidence');
  state.stages.ls = { status: 'FAIL', reason: 'readback mismatch', evidence: { apiChecks: { ...fileFact(oldApi), label: 'old readback' } } }; state.blocker = { stage: 'ls', reason: 'readback mismatch' };
  atomicJson(f.statePath, state); let calls = 0;
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null, run: async (tool, args) => {
    calls++; assert.equal(tool, 'node'); assert.ok(args.includes('--with-ls-api'));
    return JSON.stringify({ ok: true, currentArtifacts: [{ sha256: fileFact(f.signed).sha256 }],
      checks: [{ id: 'LS/1/product/id', ok: true }, { id: 'LS/1/variant/id', ok: true }] });
  } };
  const plan = await release.planReleaseReconciliation(f.statePath, await release.inspectRelease(f.statePath, deps), deps);
  const result = await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps);
  assert.equal(calls, 2); assert.equal(result.releaseState, 'RELEASE_READY');
  assert.equal(result.stages.ls.previous[0].status, 'FAIL');
  assert.equal(JSON.parse(fs.readFileSync(result.stages.ls.evidence.apiChecks.path)).ok, true);
  assert.deepEqual(result.stages.ls.reconciliation.preserved[0], { original: fileFact(oldApi), retained: fileFact(oldApi) });
  const incomplete = structuredClone(result); incomplete.stages.ls.reconciliation.preserved = [];
  assert.throws(() => assertState(incomplete), /missing from reconciliation/i);
  fs.writeFileSync(oldApi, 'changed failure evidence'); assert.throws(() => assertState(result), /changed/i);
});

test('host failure is restored only when every frozen report row has valid retained evidence', async t => {
  const { digest, acceptanceCriteria, reportTemplate, HOST_TESTS } = await import('./hypha_release_contract.mjs');
  const f = fixture(t), state = structuredClone(f.state);
  state.stages.windows = { status: 'PASS', facts: [fileFact(f.signed)] };
  state.stages.freeze = { status: 'PASS', facts: [fileFact(f.signed)] };
  state.freeze = { source: state.source, publicationNotes: fileFact(f.signed), updateCheck: updateBinding(''), inputs: digest(state.inputs),
    criteriaSha256: digest(acceptanceCriteria(state)), payloads: [fileFact(f.signed)] };
  state.freeze.sha256 = digest(state.freeze);
  const report = reportTemplate(state, 'A4', state.freeze.sha256, HOST_TESTS);
  report.tests = report.tests.map(row => ({ ...row, result: 'PASS', measured: row.expected, oracle: 'retained report', evidence: [fileFact(f.signed)] }));
  const reportPath = path.join(f.root, state.inputs.hostsReport); fs.mkdirSync(path.dirname(reportPath), { recursive: true });
  fs.writeFileSync(reportPath, JSON.stringify(report));
  state.stages.hosts = { status: 'FAIL', reason: 'earlier report failed' };
  state.blocker = { stage: 'hosts', reason: 'earlier report failed' }; atomicJson(f.statePath, state);
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null };
  const inspection = await release.inspectRelease(f.statePath, deps);
  report.tests[0].result = 'FAIL'; fs.writeFileSync(reportPath, JSON.stringify(report));
  await assert.rejects(release.planReleaseReconciliation(f.statePath, inspection, deps), /failure/i);
  report.tests[0].result = 'PASS'; fs.writeFileSync(reportPath, JSON.stringify(report));
  const plan = await release.planReleaseReconciliation(f.statePath, inspection, deps);
  const result = await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps);
  assert.equal(result.releaseState, 'HOST_ACCEPTED'); assert.equal(result.stages.hosts.previous[0].status, 'FAIL');
});


test('a changed failed-attempt evidence file blocks reconciliation and preserves the profile', async t => {
  const f = fixture(t); f.state.stages.windows.facts = [fileFact(f.signed)]; atomicJson(f.statePath, f.state);
  const inspection = await release.inspectRelease(f.statePath, { snapshot: f.snapshot });
  fs.writeFileSync(f.signed, 'changed failed evidence');
  await assert.rejects(release.planReleaseReconciliation(f.statePath, inspection,
    { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify: async () => ({ facts: [fileFact(f.signed)] }) }), /changed/i);
  assert.deepEqual(JSON.parse(fs.readFileSync(f.statePath)), f.state);
});

test('a replaced failed report can be recovered only with a matching preserved original', async t => {
  const f = fixture(t), original = path.join(f.root, 'failed-report'), copy = path.join(f.root, 'retained-report');
  fs.writeFileSync(original, 'original failure'); fs.copyFileSync(original, copy);
  f.state.stages.windows.facts = [fileFact(original)]; atomicJson(f.statePath, f.state);
  fs.writeFileSync(original, 'corrected report');
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify: async () => ({ facts: [fileFact(f.signed)] }) };
  const inspection = await release.inspectRelease(f.statePath, deps);
  await assert.rejects(release.planReleaseReconciliation(f.statePath, inspection, deps), /retained original/i);
  const retained = [{ originalPath: original, retainedPath: copy }];
  const plan = await release.planReleaseReconciliation(f.statePath, inspection, { ...deps, retained });
  fs.writeFileSync(copy, 'wrong');
  await assert.rejects(release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps), /differs/i);
  fs.writeFileSync(copy, 'original failure');
  const result = await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps);
  assert.equal(result.stages.windows.previous[0].status, 'FAIL');
  assert.equal(result.stages.windows.reconciliation.preserved[0].retained.sha256, fileFact(copy).sha256);
  assert.equal(fs.readFileSync(original, 'utf8'), 'corrected report');
  fs.writeFileSync(copy, 'changed after apply');
  assert.throws(() => assertState(result), /changed/i);
});

test('plan refuses a concurrent profile change during verification', async t => {
  const f = fixture(t), inspection = await release.inspectRelease(f.statePath, { snapshot: f.snapshot });
  const verify = async () => {
    atomicJson(f.statePath, { ...f.state, note: 'concurrent change' });
    return { facts: [fileFact(f.signed)] };
  };
  await assert.rejects(release.planReleaseReconciliation(f.statePath, inspection, { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify }), /changed/i);
  assert.equal(JSON.parse(fs.readFileSync(f.statePath)).note, 'concurrent change');
});

test('postrelease reconciliation requires the complete A7 report and public readback before completion', async t => {
  const { digest, acceptanceCriteria, reportTemplate, POST_TESTS } = await import('./hypha_release_contract.mjs');
  const { makeHpRecord, renderHpPage, installerFilename } = await import('./hypha_release_hp_data.mjs');
  const f = fixture(t), state = structuredClone(f.state);
  const payloads = ['macos', 'windows'].map(target => {
    const file = path.join(f.root, installerFilename(state.candidate.version, target));
    fs.writeFileSync(file, `signed fixture ${target}`); return fileFact(file);
  });
  for (const stage of ['windows', 'freeze', 'hosts', 'packages', 'provenance', 'distribution', 'ls', 'github', 'hp'])
    state.stages[stage] = { status: 'PASS', facts: [fileFact(f.signed)] };
  state.stages.packages.facts = payloads; state.stages.provenance.publicFacts = [fileFact(f.signed)];
  state.freeze = { source: state.source, publicationNotes: fileFact(f.signed), updateCheck: updateBinding(''),
    inputs: digest(state.inputs), criteriaSha256: digest(acceptanceCriteria(state)), payloads: [fileFact(f.signed)] };
  state.freeze.sha256 = digest(state.freeze); state.hpCommit = 'c'.repeat(40);
  state.releaseState = 'RELEASE_INCIDENT'; state.publicationStarted = true;
  state.stages.postrelease = { status: 'FAIL', reason: 'earlier A7 failure' };
  state.blocker = { stage: 'postrelease', reason: 'earlier A7 failure' };
  const report = reportTemplate(state, 'A7', digest(payloads), POST_TESTS);
  report.tests = report.tests.map(row => ({ ...row, result: 'PASS', measured: row.expected,
    oracle: 'retained A7 evidence', evidence: [fileFact(f.signed)] }));
  const reportPath = path.join(f.root, state.inputs.postreleaseReport); fs.mkdirSync(path.dirname(reportPath), { recursive: true });
  report.tests[0].result = 'FAIL'; fs.writeFileSync(reportPath, JSON.stringify(report)); atomicJson(f.statePath, state);
  const record = makeHpRecord(state.candidate.version, state.inputs.date, payloads);
  const template = lang => `<html lang="${lang}"><section id="downloads"><h2 id="hypha-download-title">Hypha {{KIRIN_HYPHA_LATEST_VERSION}}</h2><time datetime="{{KIRIN_HYPHA_PUBLISHED_AT}}">{{KIRIN_HYPHA_PUBLISHED_AT}}</time><a href="/downloads/kirin-hypha/macos">PKG</a><a href="/downloads/kirin-hypha/windows">Windows VST3 + AAX</a>macOS 12+ (VST3 + Audio Unit + AAX) / Windows 10/11 (VST3 + AAX)<a href="{{KIRIN_HYPHA_RELEASE_NOTES_URL}}">{{KIRIN_HYPHA_LATEST_VERSION}}</a></section></html>`;
  const fetcher = async (url, options = {}) => {
    if (url.includes('deployment-source')) return new Response(state.hpCommit);
    if (url.includes('/downloads/kirin-hypha/')) {
      const target = url.endsWith('/macos') ? 'macos' : 'windows';
      if (options.redirect === 'manual') return new Response(null, { status: 302,
        headers: { Location: record.downloads[target].url, 'Cache-Control': 'no-store' } });
      return new Response(fs.readFileSync(payloads.find(fact => path.basename(fact.path) === installerFilename(state.candidate.version, target)).path));
    }
    if (url.startsWith('https://github.com/')) {
      const name = decodeURIComponent(url.split('/').at(-1));
      return new Response(fs.readFileSync([...payloads, fileFact(f.signed)].find(fact => path.basename(fact.path) === name).path));
    }
    return new Response(renderHpPage(template(url.includes('/ja/') ? 'ja' : 'en'), record));
  };
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null, fetcher }, inspection = await release.inspectRelease(f.statePath, deps);
  await assert.rejects(release.planReleaseReconciliation(f.statePath, inspection, deps), /failure/i);
  report.tests[0].result = 'PASS'; fs.writeFileSync(reportPath, JSON.stringify(report));
  const plan = await release.planReleaseReconciliation(f.statePath, inspection, deps);
  const result = await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps);
  assert.equal(result.releaseState, 'RELEASE_COMPLETE'); assert.equal(result.stages.postrelease.previous[0].status, 'FAIL');
});

test('provenance stage persistence with substituted verifier does not test the real provenance verifier', async t => {
  const { digest, acceptanceCriteria } = await import('./hypha_release_contract.mjs');
  const f = fixture(t), state = structuredClone(f.state);
  for (const stage of ['windows', 'freeze', 'hosts', 'packages']) state.stages[stage] = { status: 'PASS', facts: [fileFact(f.signed)] };
  state.freeze = { source: state.source, publicationNotes: fileFact(f.signed), updateCheck: updateBinding(''),
    inputs: digest(state.inputs), criteriaSha256: digest(acceptanceCriteria(state)), payloads: [fileFact(f.signed)] };
  state.freeze.sha256 = digest(state.freeze);
  state.stages['source-delivery'].evidence = { legalDir: path.join(f.root, 'legal') };
  state.stages.provenance = { status: 'FAIL', reason: 'previous verification failed' };
  state.blocker = { stage: 'provenance', reason: 'previous verification failed' }; atomicJson(f.statePath, state);
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null, verify: async (_state, stage) => {
    assert.equal(stage, 'provenance'); return { facts: [fileFact(f.signed)], publicFacts: [fileFact(f.signed)], aliases: ['fixture'] };
  } };
  const plan = await release.planReleaseReconciliation(f.statePath, await release.inspectRelease(f.statePath, deps), deps);
  const result = await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps);
  assert.deepEqual(result.stages.provenance.publicFacts, [fileFact(f.signed)]);
  assert.deepEqual(result.stages.provenance.aliases, ['fixture']);
});

test('CI reconciliation reads only the exact successful run and never dispatches a job', async t => {
  const { CI_ORACLES } = await import('./hypha_release_contract.mjs');
  const f = fixture(t), state = structuredClone(f.state);
  state.stages = { 'provenance-inputs': { status: 'PASS', facts: [fileFact(f.signed)] }, ci: { status: 'FAIL' } };
  state.inputs.ciRun = '71'; state.blocker = { stage: 'ci', reason: 'previous failure' };
  const workflow = path.join(f.root, '.github/workflows/ci.yml'); fs.mkdirSync(path.dirname(workflow), { recursive: true });
  fs.writeFileSync(workflow, 'fixture workflow'); atomicJson(f.statePath, state);
  const checks = Object.values(CI_ORACLES).flatMap(row => row.checks);
  const names = new Set(['public history identity', 'release source contract (macos)', 'auval arm64 (AU validation)',
    'windows VST3 preflight', ...checks.map(check => check.job)]);
  const jobs = [...names].map((name, index) => ({ id: index + 1, name, run_id: 71, head_sha: state.candidate.commit,
    run_attempt: 1, status: 'completed', conclusion: 'success',
    steps: [...new Set(checks.filter(check => check.job === name).map(check => check.step))].map(name => ({ name, status: 'completed', conclusion: 'success' })) }));
  let reads = 0;
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null, run: async (tool, args) => {
    reads++; assert.equal(tool, 'gh'); assert.equal(args[0], 'api'); assert.ok(!args.includes('--method'));
    if (args[1].includes('/jobs?')) return JSON.stringify({ jobs, total_count: jobs.length });
    return JSON.stringify({ head_sha: state.candidate.commit, status: 'completed', conclusion: 'success', path: '.github/workflows/ci.yml',
      name: 'CI', event: 'push', run_attempt: 1, head_repository: { full_name: 'heyalohaloha/kirin_hypha' } });
  } };
  const plan = await release.planReleaseReconciliation(f.statePath, await release.inspectRelease(f.statePath, deps), deps);
  const result = await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps);
  assert.equal(reads, 4); assert.equal(result.stages.ci.evidence.runId, '71');
  assert.equal(result.stages.ci.previous[0].status, 'FAIL');
});

test('a previously passing CI gate is rechecked before a later reconciliation', async t => {
  const f = fixture(t); f.state.inputs.ciRun = '71'; atomicJson(f.statePath, f.state);
  let reads = 0, stageReads = 0;
  const deps = { snapshot: f.snapshot, run: async (tool, args) => {
    reads++; assert.equal(tool, 'gh'); assert.equal(args[0], 'api');
    return JSON.stringify({ head_sha: f.state.candidate.commit, status: 'completed', conclusion: 'failure',
      path: '.github/workflows/ci.yml', name: 'CI', event: 'push', run_attempt: 1,
      head_repository: { full_name: 'heyalohaloha/kirin_hypha' } });
  }, verify: async () => { stageReads++; return { facts: [fileFact(f.signed)] }; } };
  await assert.rejects(release.planReleaseReconciliation(f.statePath, await release.inspectRelease(f.statePath, deps), deps), /successful exact-commit/i);
  assert.equal(reads, 1); assert.equal(stageReads, 0); assert.equal(JSON.parse(fs.readFileSync(f.statePath)).stages.windows.status, 'FAIL');
});

test('manufacturing input reconciliation rechecks the factory pin and material approval', async t => {
  const { hash } = await import('./hypha_release_contract.mjs');
  const f = fixture(t), state = structuredClone(f.state);
  state.inputs.windowsFactoryRepository = 'fixture/factory'; state.inputs.windowsFactoryCommit = 'c'.repeat(40);
  state.stages = { 'provenance-inputs': { status: 'FAIL', reason: 'previous check failed' } };
  state.blocker = { stage: 'provenance-inputs', reason: 'previous check failed' };
  const cmake = 'set(KIRIN_HYPHA_DATA_SOURCES\n ${CMAKE_CURRENT_SOURCE_DIR}/../asset.png)\n';
  fs.mkdirSync(path.join(f.root, 'juce_shell')); fs.writeFileSync(path.join(f.root, 'juce_shell/CMakeLists.txt'), cmake);
  fs.writeFileSync(path.join(f.root, 'asset.png'), 'fixture asset');
  const registry = path.join(f.root, 'docs/provenance/asset_distribution_registry.json'); fs.mkdirSync(path.dirname(registry), { recursive: true });
  const spec = { schema: 'hypha-asset-distribution-registry-v1', embedContractSha256: hash(cmake),
    assets: [{ path: 'asset.png', sha256: hash('fixture asset'), status: 'verified', rightsEvidence: 'fixture', allowedUses: ['binary'] }] };
  fs.writeFileSync(registry, JSON.stringify(spec)); atomicJson(f.statePath, state);
  const deps = { snapshot: f.snapshot }, inspection = await release.inspectRelease(f.statePath, deps);
  const plan = await release.planReleaseReconciliation(f.statePath, inspection, deps);
  const result = await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps);
  assert.equal(result.stages['provenance-inputs'].status, 'PASS'); assert.equal(result.stages['provenance-inputs'].evidence.approved, true);
  fs.writeFileSync(path.join(f.root, 'asset.png'), 'changed'); assert.throws(() => checkFacts(result.stages['provenance-inputs'].facts), /changed/i);
});


test('a stopped child leaves RUNNING without blocker; explicit reconciliation keeps its lock and signed output', async t => {
  const f = fixture(t); delete f.state.stages.windows; delete f.state.blocker;
  f.state.inputs.windowsFactoryRepository = 'fixture/factory'; f.state.inputs.windowsFactoryCommit = 'c'.repeat(40);
  f.state.inputs.windowsPromotionRun = '123'; f.state.inputs.windowsArtifactArchive = path.join(f.root, 'archive.zip');
  atomicJson(f.statePath, f.state);
  const moduleUrl = new URL('../release_hypha.mjs', import.meta.url).href;
  const code = `import * as R from ${JSON.stringify(moduleUrl)}; import fs from 'node:fs';
    const statePath = process.argv[1], state = JSON.parse(fs.readFileSync(statePath));
    await R.executeRelease(state, { execute: true, until: 'packages' }, { statePath,
      snapshot: () => state.source, clean: () => {}, log: () => {}, actions: { windows: async () => process.exit(23) } });`;
  const child = spawnSync(process.execPath, ['--input-type=module', '-e', code, f.statePath], { encoding: 'utf8' });
  assert.equal(child.status, 23, child.stderr);
  const saved = JSON.parse(fs.readFileSync(f.statePath));
  assert.equal(saved.stages.windows.status, 'RUNNING'); assert.equal(saved.blocker, undefined);
  const lock = `${f.statePath}.lock`; assert.ok(fs.existsSync(lock));
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null,
    verify: async (_state, stage) => { assert.equal(stage, 'windows'); return { facts: [fileFact(f.signed)] }; } };
  const plan = await release.planReleaseReconciliation(f.statePath, await release.inspectRelease(f.statePath, deps), deps);
  assert.equal(plan.stage, 'windows');
  await assert.rejects(release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps), /EEXIST/);
  assert.ok(fs.existsSync(lock)); fs.unlinkSync(lock); // This fixture's child has exited and owned this lock.
  const result = await release.applyReleaseReconciliation(f.statePath, plan, plan.sha256, deps);
  assert.equal(result.stages.windows.status, 'PASS'); assert.equal(result.stages.windows.previous[0].status, 'RUNNING');
  assert.deepEqual(result.stages['macos-aax'], f.state.stages['macos-aax']);
  assert.equal(fs.readFileSync(f.signed, 'utf8'), 'signed fixture');
});

test('ambiguous interrupted stages and inconsistent predecessors are refused', async t => {
  const f = fixture(t), deps = { snapshot: f.snapshot, verifyPrerequisites: async () => null,
    verify: async () => ({ facts: [fileFact(f.signed)] }) };
  for (const mutate of [
    state => { state.stages.ci.status = 'RUNNING'; },
    state => { state.stages.ci.status = 'PENDING'; },
    state => { state.blocker = { stage: 'ci' }; },
    state => { state.stages.freeze = { status: 'PASS', facts: [fileFact(f.signed)] }; },
    state => { state.stages.unknown = { status: 'RUNNING' }; },
  ]) {
    const state = structuredClone(f.state); state.stages.windows.status = 'RUNNING'; delete state.blocker;
    mutate(state); atomicJson(f.statePath, state);
    await assert.rejects(release.planReleaseReconciliation(f.statePath, await release.inspectRelease(f.statePath, deps), deps), /ambiguous/i);
    assert.deepEqual(JSON.parse(fs.readFileSync(f.statePath)), state);
  }
});

test('factory or unrecognized provenance recovery is refused before any verifier runs', async t => {
  const f = fixture(t), state = structuredClone(f.state);
  for (const stage of ['windows', 'freeze', 'hosts', 'packages']) state.stages[stage] = { status: 'PASS', facts: [fileFact(f.signed)] };
  state.stages['source-delivery'].evidence = { legalDir: path.join(f.root, 'legal'), extensionDigest: 'fixture' };
  state.stages.provenance = { status: 'FAIL' }; state.blocker = { stage: 'provenance' }; atomicJson(f.statePath, state);
  let verifierCalls = 0;
  const deps = { snapshot: f.snapshot, verifyPrerequisites: async () => { verifierCalls++; }, verify: async () => { verifierCalls++; } };
  const before = fs.readFileSync(f.statePath);
  await assert.rejects(release.planReleaseReconciliation(f.statePath, await release.inspectRelease(f.statePath, deps), deps), /Factory provenance reconciliation is unsupported/i);
  assert.equal(verifierCalls, 0); assert.deepEqual(fs.readFileSync(f.statePath), before);
  state.stages['source-delivery'].evidence = { legalDir: path.join(f.root, 'legal') };
  state.stages.ci.previousProfileReceipt = { path: 'fixture' }; atomicJson(f.statePath, state);
  await assert.rejects(release.planReleaseReconciliation(f.statePath, await release.inspectRelease(f.statePath, deps), deps), /Factory provenance reconciliation is unsupported/i);
  const fakeBody = { schema: 'hypha-release-reconciliation-plan-v1', stage: 'provenance' };
  const fakePlan = { ...fakeBody, sha256: (await import('./hypha_release_contract.mjs')).digest(fakeBody) };
  await assert.rejects(release.applyReleaseReconciliation(f.statePath, fakePlan, fakePlan.sha256, deps), /Factory provenance reconciliation is unsupported/i);
});
