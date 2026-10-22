import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { makeState, executeRelease, STAGES } from '../release_hypha.mjs';
import { fileFact } from './hypha_release_contract.mjs';
import { confirmLsReadback } from './hypha_release_ls.mjs';

test('LS key failure and unconfirmed reads checkpoint; a verified-content failure is an incident', async t => {
  for (const failure of ['key', 2, 1]) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-ls-action-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
    fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
    const source = { commit: 'a'.repeat(40), bNumber: ['B', 1234].join('-'), state: 'clean source', fingerprint: 'b'.repeat(64) };
    const statePath = path.join(root, 'release_state/profile.json');
    const state = makeState(root, { state: statePath, lsState: 'release_state/ls-targets.json' }, () => source);
    const evidence = path.join(root, 'evidence'); fs.writeFileSync(evidence, 'retained fixture evidence');
    for (const stage of STAGES.slice(0, STAGES.indexOf('ls'))) state.stages[stage] = { status: 'PASS', facts: [fileFact(evidence)] };
    let reads = 0, keys = 0;
    const key = () => { keys++; assert.ok(!state.publicationStarted); if (failure === 'key') throw new Error('unavailable'); return crypto.randomBytes(24).toString('hex'); };
    const run = async (tool, args, options) => {
      reads++; assert.equal(state.publicationStarted, true); assert.ok(args.includes('--json'));
      fs.writeFileSync(options.stdoutFile, JSON.stringify({ checks: [{ id: 'LS/11/size', ok: false, expected: 1234, actual: 1235 }] }));
      throw Object.assign(new Error('content mismatch'), { exitCode: failure });
    };
    const actions = { ls: () => confirmLsReadback(state, run, () => {}, key) };
    await executeRelease(state, { execute: true, until: 'hp' }, { statePath, snapshot: () => source, clean: () => {}, actions, log: () => {} });
    assert.equal(keys, 1, JSON.stringify(state.blocker));
    assert.equal(reads, failure === 'key' ? 0 : 1, JSON.stringify(state.blocker));
    if (failure !== 'key') {
      const fact = state.stages.ls.evidence.apiChecks;
      assert.ok(fs.existsSync(fact.path));
      assert.equal(JSON.parse(fs.readFileSync(fact.path)).checks[0].id, 'LS/11/size');
    }
    assert.equal(state.stages.ls.status, failure === 1 ? 'FAIL' : 'PENDING');
    assert.equal(state.releaseState === 'RELEASE_INCIDENT', failure === 1);
    assert.equal(fs.existsSync(`${statePath}.lock`), false);
  }
});
