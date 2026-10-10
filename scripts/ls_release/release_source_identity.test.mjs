import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { readReleaseSourceIdentity } from './release_source_identity.mjs';

test('unnumbered merge uses only an identical-tree numbered commit and retains the exact HEAD', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-merge-identity-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const git = args => execFileSync('git', args, { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
  git(['init']); git(['config', 'user.name', 'Fixture']); git(['config', 'user.email', 'fixture@example.invalid']);
  fs.writeFileSync(path.join(root, 'source'), 'first'); git(['add', '.']); git(['commit', '-m', '[B-100] fixture']);
  const numbered = git(['rev-parse', 'HEAD']); git(['commit', '--allow-empty', '-m', 'merge candidate']);
  const merge = git(['rev-parse', 'HEAD']);
  const identity = readReleaseSourceIdentity({ root });
  assert.equal(identity.commit, merge); assert.equal(identity.bNumber, 'B-100');
  assert.equal(identity.bNumberSourceCommit, numbered);
  fs.writeFileSync(path.join(root, 'source'), 'different'); git(['add', '.']); git(['commit', '-m', 'different unnumbered source']);
  assert.throws(() => readReleaseSourceIdentity({ root }), /no identical-tree/);
});
