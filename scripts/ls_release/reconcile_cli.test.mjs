import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { makeState, runReleaseCli } from '../release_hypha.mjs';
import { atomicJson } from './hypha_release_contract.mjs';

test('release CLI binds an existing decision and inspects the same private candidate', async t => {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-reconcile-cli-')));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
  fs.writeFileSync(path.join(root, '.gitignore'), 'release_state/\n');
  const git = args => execFileSync('git', args, { cwd: root, stdio: 'pipe' });
  git(['init', '-q']); git(['add', '.']);
  git(['-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'B-1 fixture']);
  const statePath = path.join(root, 'release_state/profile.json');
  const state = makeState(root, { state: statePath }); atomicJson(statePath, state);
  const decision = path.join(root, 'release_state/decision.json');
  fs.writeFileSync(decision, JSON.stringify({ schema: 'hypha-owner-acceptance-override-v1', release: '1.2.3',
    candidateId: state.candidate.id, excludedRequiredTest: 'pair/combinations', excludedResultMustBe: 'NOT_REQUIRED_OWNER_OVERRIDE' }));
  await runReleaseCli(['--state', statePath, '--bind-owner-override', decision], root);
  const bound = JSON.parse(fs.readFileSync(statePath)); assert.equal(bound.ownerOverrides[0].candidateId, state.candidate.id);
  const inspection = await runReleaseCli(['--state', statePath, '--inspect'], root);
  assert.equal(inspection.candidateId, state.candidate.id); assert.equal(inspection.releaseState, 'DIAGNOSTIC');
  fs.appendFileSync(decision, ' ');
  await assert.rejects(runReleaseCli(['--state', statePath, '--inspect'], root), /decision changed/i);
});
