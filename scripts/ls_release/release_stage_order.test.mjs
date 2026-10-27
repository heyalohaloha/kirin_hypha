import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { makeState, executeRelease, STAGES, parseReleaseArgs } from '../release_hypha.mjs';
import { Checkpoint, fileFact, atomicJson } from './hypha_release_contract.mjs';

const source = { commit: 'a'.repeat(40), bNumber: 'B-1234', state: 'clean source', fingerprint: 'b'.repeat(64), juce: null };
function fixture(t) {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-order-')));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
  const statePath = path.join(root, 'release_state/state.json');
  const state = makeState(root, { state: statePath, windowsFactoryRepository: 'fixture/factory', windowsFactoryCommit: 'c'.repeat(40) }, () => source);
  const evidence = path.join(root, 'evidence'); fs.writeFileSync(evidence, 'fixture');
  state.inputs.windowsPromotionRun = '123'; state.inputs.windowsArtifactArchive = evidence; atomicJson(statePath, state);
  const calls = []; const actions = Object.fromEntries(STAGES.map(stage => [stage, async () => { calls.push(stage); return { facts: [fileFact(evidence)] }; }]));
  const deps = { statePath, snapshot: () => source, clean: () => {}, actions, log: () => {} };
  return { state, actions, calls, deps };
}

test('LS pending does not stop GitHub, HP and A7; resume reuses all completed work', async t => {
  const f = fixture(t); let attempts = 0;
  f.actions.ls = async () => { attempts++; if (attempts === 1) throw new Checkpoint('LS pending'); return {}; };
  await executeRelease(f.state, { execute: true, until: 'complete' }, f.deps);
  assert.deepEqual(f.calls.slice(-3), ['github', 'hp', 'postrelease']);
  assert.equal(f.state.stages.ls.status, 'PENDING'); assert.equal(f.state.releaseState, 'RELEASED');
  const prior = [...f.calls];
  await executeRelease(f.state, { execute: true, until: 'complete' }, f.deps);
  assert.deepEqual(f.calls, prior); assert.equal(attempts, 2); assert.equal(f.state.releaseState, 'RELEASE_COMPLETE');
});

test('LS can run after A5 before HP, but only all stages produce complete', async t => {
  const f = fixture(t);
  assert.equal(parseReleaseArgs(['--state', 'x', '--until', 'ls']).until, 'ls');
  await executeRelease(f.state, { execute: true, until: 'ls' }, f.deps);
  assert.equal(f.state.stages.ls.status, 'PASS'); assert.equal(f.state.stages.github, undefined);
  assert.notEqual(f.state.releaseState, 'RELEASE_COMPLETE');
  await executeRelease(f.state, { execute: true, until: 'complete' }, f.deps);
  assert.equal(f.state.releaseState, 'RELEASE_COMPLETE');
});

test('a persisted complete state without every required PASS is rejected', async t => {
  const f = fixture(t);
  f.state.releaseState = 'RELEASE_COMPLETE';
  f.state.stages.postrelease = { status: 'PASS' };
  await assert.rejects(executeRelease(f.state, { execute: true, until: 'complete' }, f.deps), /complete.*stages/i);
});

test('last LS cannot complete if approved public copy or release notes changed', async t => {
  const { publicationBinding } = await import('./publication_input.mjs');
  const { hpReleaseNotice, makeHpRecord, installerFilename } = await import('./hypha_release_hp_data.mjs');
  for (const altered of ['copy', 'notes']) {
    const f = fixture(t), version = f.state.candidate.version, date = f.state.inputs.date;
    const notes = path.join(f.state.root, 'notes.md'), input = path.join(f.state.root, 'public.json');
    fs.writeFileSync(notes, 'reviewed notes'); f.state.inputs.notes = notes;
    const facts = ['macos', 'windows'].map(target => ({ path: installerFilename(version, target), bytes: 1, sha256: 'a'.repeat(64) }));
    const notice = hpReleaseNotice(makeHpRecord(version, date, facts));
    fs.writeFileSync(input, JSON.stringify({ schema: 'hypha-publication-v1', version, date, notice }));
    f.state.publication = publicationBinding(f.state, input);
    for (const stage of STAGES.filter(name => name !== 'ls')) f.state.stages[stage] = { status: 'PASS' };
    f.state.releaseState = 'RELEASED';
    if (altered === 'copy') { notice.ja.summary += ' changed'; fs.writeFileSync(input, JSON.stringify({ schema: 'hypha-publication-v1', version, date, notice })); }
    else fs.writeFileSync(notes, 'changed notes');
    await executeRelease(f.state, { execute: true, until: 'ls' }, f.deps);
    assert.notEqual(f.state.releaseState, 'RELEASE_COMPLETE');
    assert.match(f.state.blocker.reason, /changed/i);
  }
});

function retainCompletedProfile(f) {
  atomicJson(f.deps.statePath, f.state);
  const original = fileFact(f.deps.statePath), directory = path.join(path.dirname(f.deps.statePath), 'source-adapter-original-profiles');
  fs.mkdirSync(directory, { recursive: true });
  const copy = path.join(directory, `${original.sha256}.json`); fs.copyFileSync(f.deps.statePath, copy);
  return fileFact(copy);
}

test('completed saved profile permits only a locked CI requalification and keeps failures incomplete', async t => {
  for (const pending of [false, true]) {
    const f = fixture(t);
    for (const stage of STAGES) f.state.stages[stage] = { status: 'PASS' };
    f.state.releaseState = 'RELEASE_COMPLETE'; retainCompletedProfile(f);
    f.state.stages.ci.status = 'PENDING';
    f.actions.ci = async () => {
      assert.equal(fs.existsSync(`${f.deps.statePath}.lock`), true);
      assert.equal(JSON.parse(fs.readFileSync(f.deps.statePath)).releaseState, 'RELEASED');
      if (pending) throw new Checkpoint('CI unavailable');
      return {};
    };
    await executeRelease(f.state, { execute: true, until: 'complete' }, f.deps);
    assert.equal(f.state.releaseState, pending ? 'RELEASED' : 'RELEASE_COMPLETE');
    assert.equal(f.state.stages.ci.status, pending ? 'PENDING' : 'PASS');
  }
});

test('completed requalification rejects altered in-memory profile', async t => {
  const f = fixture(t);
  for (const stage of STAGES) f.state.stages[stage] = { status: 'PASS' };
  f.state.releaseState = 'RELEASE_COMPLETE'; retainCompletedProfile(f);
  f.state.stages.ci.status = 'PENDING'; f.state.candidate.version = '9.9.9';
  await assert.rejects(executeRelease(f.state, { execute: true, until: 'complete' }, f.deps), /complete|requalification/i);
});

test('concurrent profile change during CI requalification is never overwritten', async t => {
  const f = fixture(t);
  for (const stage of STAGES) f.state.stages[stage] = { status: 'PASS' };
  f.state.releaseState = 'RELEASE_COMPLETE'; retainCompletedProfile(f);
  f.state.stages.ci.status = 'PENDING';
  const clean = () => { const stored = JSON.parse(fs.readFileSync(f.deps.statePath)); stored.note = 'concurrent'; atomicJson(f.deps.statePath, stored); };
  await assert.rejects(executeRelease(f.state, { execute: true, until: 'complete' }, { ...f.deps, clean }), /requalification.*changed/i);
  assert.equal(JSON.parse(fs.readFileSync(f.deps.statePath)).note, 'concurrent');
  assert.equal(fs.existsSync(`${f.deps.statePath}.lock`), false);
});

test('failed CI requalification preserves incident and never leaves COMPLETE', async t => {
  const f = fixture(t);
  for (const stage of STAGES) f.state.stages[stage] = { status: 'PASS' };
  f.state.releaseState = 'RELEASE_COMPLETE'; f.state.publicationStarted = true; retainCompletedProfile(f);
  f.state.stages.ci.status = 'PENDING'; f.actions.ci = async () => { throw new Error('confirmed CI failure'); };
  await executeRelease(f.state, { execute: true, until: 'complete' }, f.deps);
  assert.equal(f.state.releaseState, 'RELEASE_INCIDENT'); assert.equal(f.state.stages.ci.status, 'FAIL');
});

test('CI requalification persists the original receipt before running the verifier', async t => {
  const f = fixture(t);
  for (const stage of STAGES) f.state.stages[stage] = { status: 'PASS' };
  f.state.releaseState = 'RELEASE_COMPLETE'; retainCompletedProfile(f);
  const original = fileFact(f.deps.statePath), dir = path.join(path.dirname(f.deps.statePath), 'source-adapter-original-profiles');
  const receipt = fileFact(path.join(dir, `${original.sha256}.json`)); f.state.stages.ci.status = 'PENDING';
  f.actions.ci = async () => {
    assert.deepEqual(JSON.parse(fs.readFileSync(f.deps.statePath)).stages.ci.previousProfileReceipt, receipt);
    return {};
  };
  await executeRelease(f.state, { execute: true, until: 'complete' }, f.deps);
  assert.equal(f.state.releaseState, 'RELEASE_COMPLETE');
});

test('CI requalification refuses a missing or altered original profile receipt', async t => {
  for (const damage of ['missing', 'altered']) {
    const f = fixture(t);
    for (const stage of STAGES) f.state.stages[stage] = { status: 'PASS' };
    f.state.releaseState = 'RELEASE_COMPLETE'; const receipt = retainCompletedProfile(f);
    if (damage === 'missing') fs.unlinkSync(receipt.path); else fs.writeFileSync(receipt.path, 'changed');
    f.state.stages.ci.status = 'PENDING';
    await assert.rejects(executeRelease(f.state, { execute: true, until: 'complete' }, f.deps), /ENOENT|receipt/i);
    const stored = JSON.parse(fs.readFileSync(f.deps.statePath));
    assert.equal(stored.releaseState, 'RELEASE_COMPLETE'); assert.equal(stored.stages.ci.status, 'PASS');
  }
});


test('a stopped CI requalification retains its original receipt in the running profile', t => {
  const f = fixture(t);
  for (const stage of STAGES) f.state.stages[stage] = { status: 'PASS' };
  f.state.releaseState = 'RELEASE_COMPLETE'; const receipt = retainCompletedProfile(f);
  const moduleUrl = new URL('../release_hypha.mjs', import.meta.url).href;
  const code = `import fs from 'node:fs'; import { executeRelease, STAGES } from ${JSON.stringify(moduleUrl)};
    const path = process.argv[1], state = JSON.parse(fs.readFileSync(path)); state.stages.ci.status = 'PENDING';
    const actions = Object.fromEntries(STAGES.map(stage => [stage, async () => stage === 'ci' ? process.exit(73) : {}]));
    await executeRelease(state, { execute: true, until: 'complete' }, { statePath: path, snapshot: () => state.source, clean: () => {}, actions, log: () => {} });`;
  const child = spawnSync(process.execPath, ['--input-type=module', '-e', code, f.deps.statePath], { encoding: 'utf8' });
  assert.equal(child.status, 73, child.stderr);
  const stored = JSON.parse(fs.readFileSync(f.deps.statePath));
  assert.equal(stored.releaseState, 'RELEASED'); assert.equal(stored.stages.ci.status, 'RUNNING');
  assert.deepEqual(stored.stages.ci.previousProfileReceipt, receipt); assert.equal(fs.existsSync(`${f.deps.statePath}.lock`), true);
});

test('CI requalification refuses a symlinked original-receipt directory', async t => {
  const f = fixture(t);
  for (const stage of STAGES) f.state.stages[stage] = { status: 'PASS' };
  f.state.releaseState = 'RELEASE_COMPLETE'; const receipt = retainCompletedProfile(f);
  const directory = path.dirname(receipt.path), moved = `${directory}-moved`;
  fs.renameSync(directory, moved); fs.symlinkSync(moved, directory, 'dir');
  f.state.stages.ci.status = 'PENDING';
  await assert.rejects(executeRelease(f.state, { execute: true, until: 'complete' }, f.deps), /symlink/i);
  assert.equal(JSON.parse(fs.readFileSync(f.deps.statePath)).releaseState, 'RELEASE_COMPLETE');
});

test('already COMPLETE cannot be reused or planned after public notes change', async t => {
  const { publicationBinding } = await import('./publication_input.mjs');
  const { hpReleaseNotice, makeHpRecord, installerFilename } = await import('./hypha_release_hp_data.mjs');
  const f = fixture(t), version = f.state.candidate.version, date = f.state.inputs.date;
  const notes = path.join(f.state.root, 'notes.md'), input = path.join(f.state.root, 'public.json');
  fs.writeFileSync(notes, 'reviewed'); f.state.inputs.notes = notes;
  const facts = ['macos', 'windows'].map(target => ({ path: installerFilename(version, target), bytes: 1, sha256: 'a'.repeat(64) }));
  const notice = hpReleaseNotice(makeHpRecord(version, date, facts));
  fs.writeFileSync(input, JSON.stringify({ schema: 'hypha-publication-v1', version, date, notice }));
  f.state.publication = publicationBinding(f.state, input);
  for (const stage of STAGES) f.state.stages[stage] = { status: 'PASS' };
  f.state.releaseState = 'RELEASE_COMPLETE'; fs.writeFileSync(notes, 'modified after approval');
  for (const execute of [false, true]) await assert.rejects(executeRelease(f.state, { execute, until: 'complete' }, f.deps), /changed/i);
  assert.deepEqual(f.calls, []);
});
