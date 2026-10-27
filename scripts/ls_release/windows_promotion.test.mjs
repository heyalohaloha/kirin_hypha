import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import * as release from '../release_hypha.mjs';
import * as factory from '../provenance/factory_artifact.mjs';
import { atomicJson, Checkpoint, fileFact } from './hypha_release_contract.mjs';

function fixture(t) {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-promotion-')));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
  const source = { commit: 'a'.repeat(40), bNumber: 'B-1234', state: 'clean source', fingerprint: 'b'.repeat(64) };
  const statePath = path.join(root, 'release_state/profile.json');
  const snapshot = () => source;
  const state = release.makeState(root, { state: statePath, windowsFactoryRepository: 'fixture/factory',
    windowsFactoryCommit: 'c'.repeat(40) }, snapshot);
  const archive = path.join(root, 'delivery.zip');
  execFileSync('python3', ['-c', "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w'); z.writestr('file','fixture archive'); z.close()", archive]);
  atomicJson(statePath, state);
  const options = { bindWindowsPromotion: '123', windowsArtifactArchive: archive };
  const record = { id: 123, head_sha: 'c'.repeat(40), head_repository: { full_name: 'fixture/factory' }, path: '.github/workflows/hypha-windows-signing.yml', name: 'Hypha Windows signing factory', event: 'workflow_dispatch', status: 'completed', conclusion: 'success' };
  const jobs = ['Hypha unsigned Windows VST3', 'Hypha signed Windows installer', 'Hypha validated Windows installer promotion'].map((name, i) => ({ name, status: 'completed', conclusion: i === 2 ? 'success' : 'skipped' }));
  const artifact = { name: 'KirinHypha-Windows-signed-full', id: 55, expired: false, digest: `sha256:${fileFact(archive).sha256}`, workflow_run: { id: 123, head_sha: record.head_sha } };
  const factoryRun = async (tool, args) => JSON.stringify(args[1].includes('/jobs?') ? { jobs } : args[1].includes('/artifacts?') ? { artifacts: [artifact] } : record);
  return { root, statePath, state, snapshot, options, factoryRun };
}

test('manufacturing pins factory identity without waiting for delivery; Windows still checkpoints', async t => {
  const f = fixture(t);
  assert.equal(factory.requireFactoryIdentity(f.state).repository, 'fixture/factory');
  assert.throws(() => factory.requireFactoryIdentity({ inputs: {} }));
  assert.throws(() => factory.requireFactoryDelivery(f.state), Checkpoint);
  const actions = release.realActions(f.state, {}, () => { throw new Error('no commands'); }, () => {});
  await assert.rejects(actions.windows(), Checkpoint);
});

test('promotion binds once, survives reload, and leaves earlier manufacturing receipts unchanged', async t => {
  const f = fixture(t);
  f.state.stages['macos-aax'] = { status: 'PASS', facts: [] };
  atomicJson(f.statePath, f.state);
  const updated = await release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.factoryRun });
  assert.equal(updated.inputs.windowsPromotionRun, '123');
  assert.equal(updated.inputs.windowsArtifactArchive, f.options.windowsArtifactArchive);
  assert.deepEqual(updated.stages, f.state.stages);
  assert.equal(JSON.parse(fs.readFileSync(f.statePath)).inputs.windowsPromotionRun, '123');
  factory.requireFactoryDelivery(updated);
  await assert.rejects(() => release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.factoryRun }));
  assert.equal(fs.existsSync(`${f.statePath}.lock`), false);
});

test('binding rejects execution, incomplete input, an existing binding, started Windows, freeze and foreign source', async t => {
  const f = fixture(t), args = ['--state', f.statePath, '--bind-windows-promotion', '123'];
  assert.throws(() => release.parseReleaseArgs(args));
  const complete = [...args, '--windows-artifact-archive', f.options.windowsArtifactArchive];
  assert.equal(release.parseReleaseArgs(complete).bindWindowsPromotion, '123');
  for (const flag of ['--execute', '--init', '--dry-run']) assert.throws(() => release.parseReleaseArgs([...complete, flag]));
  assert.throws(() => release.parseReleaseArgs(['--state', f.statePath, '--init', '--windows-promotion-run', '123']));
  assert.throws(() => release.parseReleaseArgs(['--state', f.statePath, '--init', '--windows-artifact-archive', f.options.windowsArtifactArchive]));
  for (const change of [
    { freeze: { sha256: 'd'.repeat(64) } }, { stages: { windows: { status: 'RUNNING' } } },
    { stages: { windows: { status: 'PASS' } } }, { stages: { windows: { status: 'FAIL' } } },
    { inputs: { ...f.state.inputs, windowsPromotionRun: '9' } },
    { inputs: { ...f.state.inputs, windowsArtifactArchive: 'already.zip' } },
  ]) {
    atomicJson(f.statePath, { ...f.state, ...change });
    const before = fs.readFileSync(f.statePath);
    await assert.rejects(() => release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.factoryRun }));
    assert.deepEqual(fs.readFileSync(f.statePath), before);
  }
  atomicJson(f.statePath, f.state);
  await assert.rejects(() => release.bindWindowsPromotion(f.statePath, f.options, () => ({ commit: 'e'.repeat(40) })));
  fs.writeFileSync(`${f.statePath}.lock`, 'foreign lock');
  await assert.rejects(() => release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.factoryRun }));
  assert.equal(fs.readFileSync(`${f.statePath}.lock`, 'utf8'), 'foreign lock');
});

test('real coordinator checkpoints before starting Windows, so delivery can bind without repeating Mac manufacturing', async t => {
  const f = fixture(t);
  const { CI_ORACLES } = await import('./hypha_release_contract.mjs');
  f.state.inputs.ciRun = '71';
  for (const stage of release.STAGES.slice(0, release.STAGES.indexOf('windows')))
    f.state.stages[stage] = { status: 'PASS', facts: [fileFact(f.options.windowsArtifactArchive)] };
  const checks = Object.values(CI_ORACLES).flatMap(row => row.checks);
  const names = new Set(['public history identity', 'release source contract (macos)',
    'auval arm64 (AU validation)', 'windows VST3 preflight', ...checks.map(c => c.job)]);
  const jobs = [...names].map((name, index) => ({ id: index + 1, name, run_id: 71,
    head_sha: f.state.candidate.commit, run_attempt: 1, status: 'completed', conclusion: 'success',
    steps: [...new Set(checks.filter(c => c.job === name).map(c => c.step))].map(step => ({ name: step, status: 'completed', conclusion: 'success' })) }));
  const run = async (tool, args) => {
    assert.equal(tool, 'gh');
    assert.equal(args[0], 'api');
    if (args[1].includes('/jobs?')) return JSON.stringify({ jobs, total_count: jobs.length });
    return JSON.stringify({ head_sha: f.state.candidate.commit, status: 'completed', conclusion: 'success',
      path: '.github/workflows/ci.yml', name: 'CI', event: 'push', run_attempt: 1,
      head_repository: { full_name: 'heyalohaloha/kirin_hypha' } });
  };
  await release.executeRelease(f.state, { execute: true, until: 'packages' }, {
    statePath: f.statePath, snapshot: f.snapshot, clean: () => {}, run, log: () => {}, platform: 'darwin' });
  assert.equal(f.state.blocker.stage, 'windows', f.state.blocker.reason);
  assert.equal(f.state.stages.windows, undefined);
  assert.equal(f.state.stages.freeze, undefined);
  assert.equal(f.state.stages['macos-aax'].status, 'PASS');
  const result = await release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.factoryRun });
  assert.equal(result.inputs.windowsPromotionRun, '123');
});

test('factory coordinator actions also checkpoint before starting Windows, so Mac can be manufactured first', async t => {
  const f = fixture(t);
  for (const stage of release.STAGES.slice(0, release.STAGES.indexOf('windows')))
    f.state.stages[stage] = { status: 'PASS', facts: [fileFact(f.options.windowsArtifactArchive)] };
  let windowsActionCalled = false;
  const actions = Object.fromEntries(release.STAGES.map(stage => [stage, async () => {
    if (stage === 'windows') windowsActionCalled = true;
    return { facts: [fileFact(f.options.windowsArtifactArchive)] };
  }]));
  await release.executeRelease(f.state, { execute: true, until: 'packages' }, {
    statePath: f.statePath, snapshot: f.snapshot, clean: () => {}, run: async () => '{}', log: () => {},
    platform: 'darwin', actions });
  assert.equal(f.state.blocker.stage, 'windows', f.state.blocker.reason);
  assert.equal(windowsActionCalled, false);
  assert.equal(f.state.stages.windows, undefined);
  const result = await release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.factoryRun });
  assert.equal(result.inputs.windowsPromotionRun, '123');
});

test('real producer guard rejects a non-macOS platform before commands or state changes', async t => {
  const f = fixture(t), before = fs.readFileSync(f.statePath);
  await assert.rejects(release.executeRelease(f.state, { execute: true, until: 'packages' }, {
    statePath: f.statePath, snapshot: f.snapshot, platform: 'linux',
    clean: () => assert.fail('platform guard must precede source checks'),
    run: () => assert.fail('platform guard must precede commands'), log: () => {},
  }), /runs on macOS/);
  assert.deepEqual(fs.readFileSync(f.statePath), before);
  assert.equal(fs.existsSync(`${f.statePath}.lock`), false);
});
