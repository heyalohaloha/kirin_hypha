import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { inspectManufacturingInputs } from './release_preflight_inputs.mjs';
import { stagePlan } from './release_stages.mjs';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-preflight-input-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const publication = JSON.parse(fs.readFileSync(new URL('../../docs/ls_release/hypha_publication.example.json', import.meta.url), 'utf8'));
  fs.writeFileSync(path.join(root, 'publication.json'), JSON.stringify(publication));
  fs.writeFileSync(path.join(root, 'notes.md'), '- Correct labels.\n');
  const manufacturing = { root, version: '1.2.3', candidateId: 'fixture-candidate', date: '2026-10-01',
    state: 'release_state/new.json', ownerOverrides: [], publicationInput: 'publication.json', notes: 'notes.md', stages: stagePlan('complete') };
  const calls = [];
  const run = config => inspectManufacturingInputs(config, { verifyJuce: selected => { calls.push(selected); return true; } });
  return { root, manufacturing, calls, run };
}

test('preparation reuses real publication and order contracts without writing source or state', t => {
  const f = fixture(t), before = fs.readdirSync(f.root).map(n => [n, fs.readFileSync(path.join(f.root, n), 'utf8')]);
  const checks = f.run({ manufacturing: f.manufacturing });
  assert.equal(checks.length, 5); assert.ok(checks.every(c => c.result === 'PASS'));
  assert.deepEqual(f.calls, [fs.realpathSync(f.root)]);
  assert.deepEqual(fs.readdirSync(f.root).map(n => [n, fs.readFileSync(path.join(f.root, n), 'utf8')]), before);
  assert.ok(!fs.existsSync(path.join(f.root, 'release_state')));
});

test('missing inputs, stale public version, an existing state and reordered LS stay CHECKPOINT', t => {
  const f = fixture(t);
  assert.ok(f.run({}).every(c => c.result === 'CHECKPOINT'));
  const check = (change, id) => assert.equal(f.run({ manufacturing: { ...f.manufacturing, ...change } }).find(c => c.id === id).result, 'CHECKPOINT');
  check({ ownerOverrides: undefined }, 'init-owner-overrides');
  check({ version: '1.2.4' }, 'hp-publication-input');
  check({ publicationInput: 'absent.json' }, 'hp-publication-input');
  check({ stages: [...f.manufacturing.stages].reverse() }, 'ls-stage-order');
  check({ stages: f.manufacturing.stages.filter(s => s !== 'ls') }, 'ls-stage-order');
  check({ state: '../profile.json' }, 'init-state-not-overwritten');
  fs.mkdirSync(path.join(f.root, 'release_state'));
  fs.writeFileSync(path.join(f.root, f.manufacturing.state), 'kept');
  check({}, 'init-state-not-overwritten');
  assert.equal(fs.readFileSync(path.join(f.root, f.manufacturing.state), 'utf8'), 'kept');
  assert.equal(inspectManufacturingInputs({ manufacturing: f.manufacturing }, { verifyJuce: () => false })
    .find(c => c.id === 'juce-tracked-patches').result, 'CHECKPOINT');
});

test('override preparation binds the existing candidate contract and rejects mismatches or duplicate rows', t => {
  const f = fixture(t), decision = { schema: 'hypha-owner-acceptance-override-v1', release: '1.2.3',
    candidateId: f.manufacturing.candidateId, excludedRequiredTest: 'macOS/x86_64/AAX',
    excludedResultMustBe: 'NOT_REQUIRED_OWNER_OVERRIDE' };
  const file = path.join(f.root, 'override.json');
  const run = value => { fs.writeFileSync(file, JSON.stringify(value)); return f.run({ manufacturing: {
    ...f.manufacturing, ownerOverrides: ['override.json'] } }).find(c => c.id === 'init-owner-overrides').result; };
  assert.equal(run(decision), 'PASS');
  for (const change of [{ release: '1.2.4' }, { candidateId: 'other' }, { excludedRequiredTest: 'unknown' }])
    assert.equal(run({ ...decision, ...change }), 'CHECKPOINT');
  run(decision);
  assert.equal(f.run({ manufacturing: { ...f.manufacturing, ownerOverrides: [file, file] } })
    .find(c => c.id === 'init-owner-overrides').result, 'CHECKPOINT');
});
