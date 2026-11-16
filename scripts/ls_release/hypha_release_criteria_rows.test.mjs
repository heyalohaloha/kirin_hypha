// Row kinds added to the fix rows: full transitions, demanded evidence kinds, platform reasons, new findings.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { makeState } from '../release_hypha.mjs';
import { assertState, digest, fileFact, reportTemplate, validateReport, acceptanceCriteria } from './hypha_release_contract.mjs';
import { FIX_ROWS_SCHEMA, FULL_TRANSITIONS, REQUIRED_DAWS, hostTestIds } from './hypha_release_criteria.mjs';
import { updateBinding } from '../updates/update_key_binding.mjs';

const SOURCE = { commit: 'a'.repeat(40), bNumber: 'B-1', fingerprint: 'b'.repeat(64) };
const PROMISE = 'The clock keeps every frame.';
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-criteria-rows-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const write = (name, value) => {
    const file = path.isAbsolute(name) ? name : path.join(root, name);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, typeof value === 'string' ? value : JSON.stringify(value)); return file;
  };
  write('crates/hypha_pre/Cargo.toml', 'version = "1.2.3"\n');
  const notes = write('notes.md', `## Fixes in 1.2.3\n\n- ${PROMISE}\n`);
  const host = { kind: 'host', chain: 'PRE -> limiter -> POST', depth: 'deep', oracle: 'host operation',
    expected: { passed: true, newIssues: 0 } };
  const rows = REQUIRED_DAWS.map((daw, index) => ({ ...host, id: `host-${index}`, promise: PROMISE, daw,
    os: daw === 'Cakewalk' ? 'windows' : 'macos', hosts: [daw === 'Cakewalk' ? 'Windows/x64/VST3' : 'macOS/x86_64/VST3'] }));
  const list = fileFact(write('transitions.json', { items: ['insert', 'reopen', 'bounce'] }));
  const full = { ...host, id: 'all-transitions', scope: FULL_TRANSITIONS, daw: 'Pro Tools', os: 'macos', hosts: ['macOS/x86_64/AAX'],
    checklist: { path: 'transitions.json', sha256: list.sha256 }, expected: { itemsWithResult: 3, newIssues: 0 } };
  const spec = { schema: FIX_ROWS_SCHEMA, release: '1.2.3', rows: [...rows, full] };
  const fixes = path.join(root, 'fixes.json');
  const init = value => { write(fixes, value); return makeState(root, { state: path.join(root, 'release_state/profile.json'), fixRows: fixes, notes }, () => SOURCE); };
  const patch = (id, change) => ({ ...spec, rows: spec.rows.map(row => (row.id === id ? { ...row, ...change } : row)) });
  return { root, write, spec, full, init, patch };
}

test('one Pro Tools full-transitions row is required and binds its checklist and item count', t => {
  const f = fixture(t);
  const state = f.init(f.spec); assertState(state);
  assert.ok(hostTestIds(state).includes('all-transitions'));
  assert.deepEqual(state.expected['all-transitions'], { itemsWithResult: 3, newIssues: 0 });
  assert.throws(() => f.init({ ...f.spec, rows: f.spec.rows.slice(0, -1) }), /exactly one Pro Tools full-transitions row/);
  assert.throws(() => f.init({ ...f.spec, rows: [...f.spec.rows, { ...f.full, id: 'again', os: 'windows', hosts: ['Windows/x64/AAX'] }] }),
    /exactly one Pro Tools full-transitions row/);
  for (const change of [{ daw: 'REAPER' }, { promise: PROMISE }, { promises: [] }, { checklist: undefined },
    { checklist: 'transitions.json' }, { checklist: { path: 'transitions.json' } },
    { checklist: { path: 'transitions.json', sha256: '0'.repeat(64) } }, { checklist: { path: 'missing.json', sha256: '0'.repeat(64) } },
    { expected: { itemsWithResult: 2, newIssues: 0 } }, { expected: { itemsWithResult: '3', newIssues: 0 } },
    { expected: { newIssues: 0 } }, { expected: { itemsWithResult: 3 } }, { scope: 'some-transitions' }]) {
    assert.throws(() => f.init(f.patch('all-transitions', change)), /all-transitions|ENOENT/, JSON.stringify(change));
  }
  assert.throws(() => f.init(f.patch('host-0', { scope: 'other' })), /scope/);
  assert.throws(() => f.init(f.patch('host-0', { scope: FULL_TRANSITIONS })), /need Pro Tools/);
  for (const items of [['insert', 'insert', 'bounce'], ['insert', '', 'bounce'], 'insert']) {
    const list = fileFact(f.write('transitions.json', { items }));
    assert.throws(() => f.init(f.patch('all-transitions', { checklist: { path: 'transitions.json', sha256: list.sha256 } })), /checklist/);
  }
  const text = fileFact(f.write('transitions.json', 'insert\nreopen\nbounce\n'));
  assert.throws(() => f.init(f.patch('all-transitions', { checklist: { path: 'transitions.json', sha256: text.sha256 } })), /checklist/);
  // A checklist edited after --init stops the next stage.
  f.write('transitions.json', { items: ['insert', 'reopen', 'bounce'] });
  const bound = f.init(f.spec);
  f.write('transitions.json', { items: ['insert', 'reopen', 'export'] });
  assert.throws(() => assertState(bound), /checklist/);
});

test('host rows expect no new findings, and CI rows carry neither scope nor evidence kinds', t => {
  const f = fixture(t);
  for (const expected of [{ passed: true }, { passed: true, newIssues: 1 }, { passed: true, newIssues: '0' }, { passed: true, newIssues: null }]) {
    assert.throws(() => f.init(f.patch('host-0', { expected })), /newIssues 0/);
  }
  const ci = { id: 'ci', kind: 'ci', promise: PROMISE, hosts: [], depth: 'deep', oracle: 'pair-recovery-parity', expected: { passed: true } };
  assertState(f.init({ ...f.spec, rows: [...f.spec.rows, ci] }));
  for (const change of [{ scope: FULL_TRANSITIONS }, { evidence: ['audio'] }]) {
    assert.throws(() => f.init({ ...f.spec, rows: [...f.spec.rows, { ...ci, ...change }] }), /scope and evidence/);
  }
});

test('platformSensitive accepts a promise or a promise with a reason', t => {
  const f = fixture(t);
  const windows = { ...f.spec.rows[2], id: 'windows', os: 'windows', hosts: ['Windows/x64/VST3'] };
  const rows = [...f.spec.rows, windows];
  assertState(f.init({ ...f.spec, rows, platformSensitive: [{ promise: PROMISE, reason: 'File paths differ by OS.' }] }));
  assertState(f.init({ ...f.spec, rows, platformSensitive: [PROMISE] }));
  assert.throws(() => f.init({ ...f.spec, rows }), /already verified in REAPER/);
  for (const entry of [{ promise: PROMISE }, { promise: PROMISE, reason: '' }, { promise: PROMISE, reason: 7 },
    { promise: 'Unknown.', reason: 'x' }, { promise: PROMISE, reason: 'x', other: 1 }, [PROMISE], null]) {
    assert.throws(() => f.init({ ...f.spec, rows, platformSensitive: [entry] }), /platformSensitive must list/, JSON.stringify(entry));
  }
  assert.throws(() => f.init({ ...f.spec, rows, platformSensitive: [PROMISE, { promise: PROMISE, reason: 'x' }] }), /platformSensitive must list/);
});

test('a row passes only with every demanded evidence kind retained and no new findings', t => {
  const f = fixture(t);
  for (const evidence of [[], ['video'], ['audio', 'audio'], 'audio']) {
    assert.throws(() => f.init(f.patch('host-0', { evidence })), /evidence kinds/);
  }
  const state = f.init(f.patch('host-0', { evidence: ['audio', 'screen', 'binary'] }));
  state.freeze = { inputs: digest(state.inputs), criteriaSha256: digest(acceptanceCriteria(state)), updateCheck: updateBinding('') };
  state.freeze.sha256 = digest(state.freeze);
  const plain = fileFact(f.write('note.json', { reviewed: true }));
  const kinds = Object.fromEntries(['audio', 'screen', 'binary'].map(kind => [kind, { kind, ...fileFact(f.write(`${kind}.bin`, kind)) }]));
  const check = change => {
    const report = reportTemplate(state, 'A4', state.freeze.sha256, hostTestIds(state));
    for (const row of report.tests) Object.assign(row, { result: 'PASS', measured: structuredClone(row.expected), oracle: 'retained host run', evidence: [plain] });
    change(report.tests.find(row => row.id === 'host-0'), report.tests.find(row => row.id === 'all-transitions'));
    return validateReport(state, f.write(state.inputs.hostsReport, report), 'A4', state.freeze.sha256, hostTestIds(state));
  };
  const all = [plain, kinds.audio, kinds.screen, kinds.binary];
  assert.ok(check(row => { row.evidence = all; }).some(fact => fact.sha256 === kinds.audio.sha256));
  assert.throws(() => check(() => {}), /host-0 needs retained audio evidence/);
  assert.throws(() => check(row => { row.evidence = [plain, kinds.audio, kinds.binary]; }), /needs retained screen evidence/);
  assert.throws(() => check(row => { row.evidence = [plain, kinds.audio, kinds.screen, { kind: 'binary', path: kinds.binary.path }]; }), /needs retained binary evidence/);
  assert.throws(() => check(row => { row.evidence = [plain, kinds.audio, kinds.screen, { ...kinds.binary, sha256: '0'.repeat(64) }]; }), /evidence hash mismatch/);
  assert.throws(() => check(row => { row.evidence = [...all, { ...plain, kind: 'video' }]; }), /unknown evidence kind/);
  assert.throws(() => check(row => { row.evidence = all; row.measured.newIssues = 1; }), /measured values fail/);
  assert.throws(() => check(row => { row.evidence = all; delete row.measured.newIssues; }), /measured values fail/);
  assert.throws(() => check((row, full) => { row.evidence = all; full.measured.itemsWithResult = 2; }), /all-transitions measured values fail/);
  assert.throws(() => check((row, full) => { row.evidence = all; full.measured.newIssues = 2; }), /all-transitions measured values fail/);
});
