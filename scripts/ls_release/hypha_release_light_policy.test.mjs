import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fixRows, REQUIRED_DAWS, LIGHT_FIX_ROWS_SCHEMA, checkFixRows } from './hypha_release_criteria.mjs';
import { makeState } from '../release_hypha.mjs';
import { assertState, SAFETY_EXPECTED } from './hypha_release_contract.mjs';
import { BASELINE_EXPECTED } from './hypha_release_light_policy.mjs';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-light-policy-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const notes = path.join(root, 'notes.md'), file = path.join(root, 'rows.json');
  fs.writeFileSync(notes, '- Labels remain readable.\n');
  const promise = 'Labels remain readable.';
  const rows = REQUIRED_DAWS.map((daw, i) => ({ id: `baseline-${i}`, kind: 'host', scope: 'ordinary-pass',
    depth: 'baseline', daw, os: daw === 'Cakewalk' ? 'windows' : 'macos',
    hosts: [daw === 'Cakewalk' ? 'Windows/x64/VST3' : daw === 'Logic Pro' ? 'macOS/x86_64/AU' : daw === 'Pro Tools' ? 'macOS/x86_64/AAX' : 'macOS/x86_64/VST3'],
    chain: 'PRE -> POST', oracle: 'retained ordinary host run', expected: { ...BASELINE_EXPECTED } }));
  rows.push({ id: 'changed-labels', kind: 'host', depth: 'deep', promise, daw: 'REAPER', os: 'macos',
    hosts: ['macOS/x86_64/VST3'], chain: 'PRE -> POST', oracle: 'real host label observation',
    expected: { readable: true, newIssues: 0 } });
  const spec = { schema: LIGHT_FIX_ROWS_SCHEMA, release: '1.1.58', rows,
    impact: [{ promise, scope: 'common', reason: 'Shared renderer; representative REAPER on macOS.',
      targets: [{ daw: 'REAPER', os: 'macos', formats: ['VST3'] }], transitions: [] }] };
  const candidate = { version: '1.1.58' };
  const run = value => { fs.writeFileSync(file, JSON.stringify(value)); return fixRows(file, candidate, undefined, notes); };
  return { root, spec, run, file, notes, candidate };
}

test('light policy keeps all DAW ordinary passes but uses one explicit deep representative', t => {
  const f = fixture(t), bound = f.run(f.spec);
  assert.equal(bound.rows.length, 8);
  const state = { candidate: f.candidate, fixRows: bound, requiredHostTests: [], inputs: { notes: f.notes },
    expected: Object.fromEntries(bound.rows.map(row => [row.id, row.expected])) };
  checkFixRows(state);
  fs.appendFileSync(f.file, ' ');
  assert.throws(() => checkFixRows(state), /changed|mismatch/);
});

test('light ordinary passes reject missing DAWs, missing steps and findings', t => {
  const f = fixture(t);
  for (let i = 0; i < 7; i++) {
    const spec = structuredClone(f.spec); spec.rows.splice(i, 1);
    assert.throws(() => f.run(spec), /ordinary pass/);
  }
  for (const key of Object.keys(BASELINE_EXPECTED)) {
    const spec = structuredClone(f.spec); delete spec.rows[0].expected[key];
    assert.throws(() => f.run(spec), /Ordinary pass|newIssues/);
  }
  for (const change of [{ promise: 'Labels remain readable.' }, { depth: 'deep' }, { kind: 'ci' }]) {
    const spec = structuredClone(f.spec); Object.assign(spec.rows[0], change);
    assert.throws(() => f.run(spec));
  }
});

test('light impact rejects incomplete, unknown, unsupported or unproved targets', t => {
  const f = fixture(t);
  for (const change of [{ reason: '' }, { scope: 'unknown' }, { targets: [] }, { transitions: ['loop'] },
    { targets: [{ daw: 'Unknown', os: 'macos', formats: ['VST3'] }] },
    { targets: [{ daw: 'Cubase', os: 'macos', formats: ['VST3'] }] },
    { targets: [{ daw: 'REAPER', os: 'windows', formats: ['AU'] }] },
    { targets: [{ daw: 'REAPER', os: 'macos', formats: ['AAX'] }] }]) {
    const spec = structuredClone(f.spec); Object.assign(spec.impact[0], change);
    assert.throws(() => f.run(spec), /Light impact/);
  }
  const absent = structuredClone(f.spec); absent.impact = [];
  assert.throws(() => f.run(absent), /each release-note/);
  const invalid = structuredClone(f.spec); invalid.rows[7].hosts = ['macOS/unknown/VST3'];
  assert.throws(() => f.run(invalid), /shipping platform/);
});

test('platform differences and changed transitions need explicit deep host rows', t => {
  const f = fixture(t), spec = structuredClone(f.spec);
  spec.impact[0].scope = 'platform';
  assert.throws(() => f.run(spec), /windows|operating systems/);
  spec.impact[0].targets.push({ daw: 'REAPER', os: 'windows', formats: ['VST3'] });
  spec.rows.push({ ...structuredClone(spec.rows[7]), id: 'win-labels', os: 'windows', hosts: ['Windows/x64/VST3'] });
  spec.impact[0].transitions = ['stop -> loop -> resume'];
  assert.throws(() => f.run(spec), /deep host evidence/);
  for (const row of spec.rows.slice(7)) row.expected.transitions = { 'stop -> loop -> resume': true };
  assert.equal(f.run(spec).rows.length, 9);
});

test('old schema and old release are not silently converted', t => {
  const f = fixture(t), old = structuredClone(f.spec);
  old.schema = 'hypha-release-fix-rows-v2';
  assert.throws(() => f.run(old), /deep oracle|scope/);
  const prior = structuredClone(f.spec); prior.release = '1.1.57'; f.candidate.version = prior.release;
  assert.throws(() => f.run(prior), /from 1.1.58/);
});


test('new policy passes real initialization without changing safety or frozen bound expectations', t => {
  const f = fixture(t);
  fs.mkdirSync(path.join(f.root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(f.root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.1.58"\n');
  f.run(f.spec);
  const state = makeState(f.root, { state: path.join(f.root, 'release_state/profile.json'),
    fixRows: f.file, notes: f.notes }, () => ({ commit: 'a'.repeat(40), bNumber: 'B-1', fingerprint: 'b'.repeat(64) }));
  assertState(state);
  for (const [id, expected] of Object.entries(SAFETY_EXPECTED)) {
    assert.ok(state.requiredHostTests.includes(id));
    assert.deepEqual(state.expected[id], expected);
  }
  state.expected['baseline-0'].saved = false;
  assert.throws(() => assertState(state), /expected|changed/);
});

test('light rejects prose-only notes and deep rows with no observed promise', t => {
  const f = fixture(t), empty = structuredClone(f.spec);
  fs.writeFileSync(f.notes, 'Labels remain readable.\n'); empty.impact = []; empty.rows.pop();
  assert.throws(() => f.run(empty), /each release-note/);
  fs.writeFileSync(f.notes, '- Labels remain readable.\n');
  for (const expected of [{newIssues: 0}, {newIssues: 0, passed: true},
    {newIssues: 0, readable: null}, {newIssues: 0, readable: {}}, {newIssues: 0, readable: 'PENDING'}]) {
    const bad = structuredClone(f.spec); bad.rows[7].expected = expected;
    assert.throws(() => f.run(bad), /deep host evidence/);
  }
  const ci = structuredClone(f.spec); ci.rows[7].kind = 'ci'; ci.rows[7].oracle = 'editor-reachability';
  assert.throws(() => f.run(ci), /real-host/);
});
test('baseline and deep rows share the DAW format compatibility contract', t => {
  const f = fixture(t);
  for (const daw of ['Logic Pro', 'Pro Tools', 'Cubase']) {
    const bad = structuredClone(f.spec), row = bad.rows.find(r => r.daw === daw);
    row.hosts = ['macOS/x86_64/' + (daw === 'Cubase' ? 'AU' : 'VST3')];
    assert.throws(() => f.run(bad), /shipping platform/);
    bad.rows = structuredClone(f.spec.rows); bad.rows[7].daw = daw;
    bad.rows[7].hosts = row.hosts; bad.impact[0].targets[0] = {daw, os:'macos', formats:[row.hosts[0].split('/')[2]]};
    assert.throws(() => f.run(bad), /shipping platform/);
  }
});

test('deep observations reject nested verdict-only and trimmed pending values', t => {
  const f=fixture(t);
  for (const expected of [{check:{passed:true}}, {check:{newIssues:0}}, {readable:' PENDING '}, {check:{state:' UNKNOWN '}}]) {
    const spec=structuredClone(f.spec); spec.rows.at(-1).expected={newIssues:0,...expected};
    assert.throws(()=>f.run(spec),/deep host evidence/);
  }
  const spec=structuredClone(f.spec);spec.rows.at(-1).expected={newIssues:0,restoredRoles:['PRE','POST']};
  assert.ok(f.run(spec));
  spec.rows.at(-1).expected={newIssues:0,restoredRoles:[]};assert.throws(()=>f.run(spec),/deep host evidence/);
});
test('light duplicate host formats are rejected before freezing',t=> {
  const f=fixture(t);f.spec.rows.at(-1).hosts.push(f.spec.rows.at(-1).hosts[0]);
  assert.throws(()=>f.run(f.spec),/Duplicate light host/);
});
