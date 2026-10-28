import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import * as release from '../release_hypha.mjs';
import { atomicJson, acceptanceCriteria, digest, fileFact } from './hypha_release_contract.mjs';

function fixture(t) {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-reconcile-')));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
  const source = { commit: 'a'.repeat(40), bNumber: 'B-1', state: 'clean source', fingerprint: 'b'.repeat(64) };
  const snapshot = () => source, statePath = path.join(root, 'release_state/state.json');
  const state = release.makeState(root, { state: statePath }, snapshot);
  const signed = path.join(root, 'signed.bin'); fs.writeFileSync(signed, 'signed fixture');
  state.stages['macos-aax'] = { status: 'PASS', facts: [fileFact(signed)] };
  atomicJson(statePath, state);
  const decision = path.join(root, 'decision.json');
  const write = changes => fs.writeFileSync(decision, JSON.stringify({ schema: 'hypha-owner-acceptance-override-v1',
    release: '1.2.3', candidateId: state.candidate.id, excludedRequiredTest: 'Exact-4-S-samples',
    excludedResultMustBe: 'NOT_REQUIRED_OWNER_OVERRIDE', ...changes }));
  write({});
  const bind = () => release.bindOwnerOverrides(statePath, [decision], snapshot);
  return { state, statePath, snapshot, decision, write, bind };
}

test('explicit pre-freeze override binding preserves manufactured receipts', t => {
  const f = fixture(t); const result = f.bind();
  assert.equal(result.ownerOverrides[0].candidateId, f.state.candidate.id);
  assert.equal(result.ownerOverrides[0].testId, 'Exact-4-S-samples');
  assert.deepEqual(result.stages, f.state.stages);
});

test('override binding rejects a frozen candidate without changing profile', t => {
  const f = fixture(t);
  f.state.freeze = { criteriaSha256: digest(acceptanceCriteria(f.state)) };
  atomicJson(f.statePath, f.state); const before = fs.readFileSync(f.statePath);
  assert.throws(f.bind, /freeze/i);
  assert.deepEqual(fs.readFileSync(f.statePath), before);
});

test('override binding rejects another candidate and detects changed decision', t => {
  const f = fixture(t); f.write({ candidateId: 'another' });
  assert.throws(f.bind, /candidate|override/i);
  f.write({}); const bound = f.bind();
  f.write({ excludedRequiredTest: 'pair/combinations' });
  assert.throws(() => release.checkBoundOwnerOverrides(bound), /changed/i);
});

test('late binding requires exact candidate, rejects duplicate rows and changed retained payload', t => {
  const f = fixture(t), before = fs.readFileSync(f.statePath);
  f.write({ candidateId: null }); assert.throws(f.bind, /candidate/i);
  assert.deepEqual(fs.readFileSync(f.statePath), before);
  f.write({}); f.bind(); const bound = fs.readFileSync(f.statePath);
  assert.throws(f.bind, /twice/i); assert.deepEqual(fs.readFileSync(f.statePath), bound);
  fs.writeFileSync(f.state.stages['macos-aax'].facts[0].path, 'changed payload');
  assert.throws(() => release.bindOwnerOverrides(f.statePath, [f.decision], f.snapshot), /changed/i);
  assert.deepEqual(fs.readFileSync(f.statePath), bound);
});

test('late override CLI is a separate explicit operation', () => {
  const parsed = release.parseReleaseArgs(['--state', 'x', '--bind-owner-override', 'decision.json']);
  assert.deepEqual(parsed.bindOwnerOverride, ['decision.json']);
  assert.throws(() => release.parseReleaseArgs(['--state', 'x', '--bind-owner-override', 'decision.json', '--execute']), /separate/);
  assert.throws(() => release.parseReleaseArgs(['--state', 'x', '--bind-owner-override', 'decision.json', '--inspect']), /separate/);
});
