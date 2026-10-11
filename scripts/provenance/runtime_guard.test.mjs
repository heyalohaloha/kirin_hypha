import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { assertRustRuntime } from './runtime_guard.mjs';
import { produceMac } from '../ls_release/hypha_release_local.mjs';
import { realActions } from '../release_hypha.mjs';
import { fileFact } from '../ls_release/hypha_release_contract.mjs';

test('every producer and freeze reject a different Rust runtime before building/signing', async t => {
  assertRustRuntime('rustc 1.94.1 (e408947bf 2026-03-25)');
  for (const text of ['rustc 1.94.10 (hash date)', 'rustc 1.94.1-nightly (hash date)', 'rustc 1.99.0 (hash date)', 'unknown']) {
    assert.throws(() => assertRustRuntime(text), /retained notices/);
  }
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-runtime-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.writeFileSync(path.join(root, 'rustc'), '#!/bin/sh\nprintf "rustc 1.99.0 (fixture date)\\n"\n', { mode: 0o755 });
  const adopted = path.resolve(import.meta.dirname, '../..');
  const result = spawnSync('bash', ['scripts/build_juce_universal.sh'], {
    cwd: adopted, env: { ...process.env, PATH: `${root}:${process.env.PATH}` }, encoding: 'utf8' });
  assert.equal(result.status, 1); assert.match(result.stderr, /retained notices/);
  assert.doesNotMatch(result.stdout, /cargo build/);
  for (const aax of [false, true]) {
    const calls = [];
    await assert.rejects(() => produceMac({ root }, async (tool,args) => {
      calls.push([tool,args]); throw new Error('runtime mismatch');
    }, aax), /runtime mismatch/);
    assert.equal(calls.length, 1); assert.equal(calls[0][0], 'node');
  }
  const state = { root, inputs: { notes: path.join(root, 'rustc'), sdk: root },
    stages: Object.fromEntries(['macos-au-vst3','macos-aax','windows'].map(stage => [stage,{facts:[fileFact(path.join(root,'rustc'))]}])) };
  const actions = realActions(state, {}, async tool => tool === 'rustc' ? 'rustc 1.99.0 (fixture date)' : 'fixture', () => {});
  await assert.rejects(() => actions.freeze(), /retained notices/);
  assert.equal(state.freeze, undefined);
  const windows = fs.readFileSync(path.join(adopted, 'scripts/build_aax_windows.ps1'), 'utf8');
  const macAax = fs.readFileSync(path.join(adopted, 'scripts/build_aax_universal.sh'), 'utf8');
  for (const source of [windows, macAax]) assert.ok(source.indexOf('runtime_guard.mjs') < source.indexOf('cargo build'));
});
