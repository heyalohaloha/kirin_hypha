import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { currentReleaseIdentity } from './build_kirin_hypha_release_set.mjs';
import { inferBNumber as installerB } from '../windows/build-installer.mjs';
import { inferBNumber as zipB, parseArgs as zipArgs } from './build_kirin_hypha_windows_vst3_zip.mjs';
import { readReleaseSourceIdentity } from './release_source_identity.mjs';
import { bindWindowsSourceIdentity } from '../windows/windows_source_identity.mjs';

test('unnumbered merge uses only an identical-tree numbered commit and retains the exact HEAD', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-merge-identity-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const git = args => execFileSync('git', args, { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
  git(['init']); git(['config', 'user.name', 'Fixture']); git(['config', 'user.email', 'fixture@example.invalid']);
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.1.51"\n');
  fs.writeFileSync(path.join(root, 'source'), 'first'); git(['add', '.']); git(['commit', '-m', '[B-100] fixture']);
  const numbered = git(['rev-parse', 'HEAD']); git(['commit', '--allow-empty', '-m', 'merge candidate']);
  const merge = git(['rev-parse', 'HEAD']);
  const identity = readReleaseSourceIdentity({ root });
  assert.equal(identity.commit, merge); assert.equal(identity.bNumber, 'B-100');
  assert.equal(identity.bNumberSourceCommit, numbered);
  assert.equal(currentReleaseIdentity({ root }).commit, merge);
  for (const read of [currentReleaseIdentity, installerB, zipB]) {
    const value = read({ root });
    assert.equal(typeof value === 'string' ? value : value.bNumber, 'B-100');
  }
  git(['commit', '--allow-empty', '-m', '[B-101] identical tree']);
  git(['commit', '--allow-empty', '-m', 'second merge candidate']);
  for (const read of [readReleaseSourceIdentity, currentReleaseIdentity, installerB, zipB]) {
    const value = read({ root }); assert.equal(typeof value === 'string' ? value : value.bNumber, 'B-101');
  }
  fs.writeFileSync(path.join(root, 'source'), 'different'); git(['add', '.']); git(['commit', '-m', 'different unnumbered source']);
  for (const read of [readReleaseSourceIdentity, currentReleaseIdentity, installerB, zipB]) {
    assert.throws(() => read({ root }), /no identical-tree/);
  }
});

for (const subject of ['Merge pull request #999 from fixture', 'merge to main [ci full]']) {
  test(`unsigned installer and ZIP accept depth=1 ${subject}; formal paths reject it`, t => {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-shallow-'));
    t.after(() => fs.rmSync(temp, { recursive: true, force: true }));
    const original = path.join(temp, 'original'), root = path.join(temp, 'shallow');
    fs.mkdirSync(original);
    const git = args => execFileSync('git', args, { cwd: original, stdio: 'pipe', encoding: 'utf8' }).trim();
    git(['init']); git(['config', 'user.name', 'Fixture']); git(['config', 'user.email', 'fixture@example.invalid']);
    fs.writeFileSync(path.join(original, 'source'), 'original');
    git(['add', '.']); git(['commit', '-m', '[B-100] numbered']);
    git(['commit', '--allow-empty', '-m', subject]);
    execFileSync('git', ['clone', '--depth', '1', `file://${original}`, root], { stdio: 'pipe' });
    assert.equal(execFileSync('git', ['rev-parse', '--is-shallow-repository'], { cwd: root, encoding: 'utf8' }).trim(), 'true');
    const commit = git(['rev-parse', 'HEAD']);
    assert.throws(() => readReleaseSourceIdentity({ root }), /no identical-tree/);
    assert.deepEqual(bindWindowsSourceIdentity({}, { root, diagnostic: true }), { bNumber: null, commit });
    const args = ['--release-kind', 'ci', '--payload-signing', 'unsigned'];
    const opts = zipArgs(args, { root });
    assert.equal(opts.bNumber, null); assert.equal(opts.commit, commit);
    for (const changed of [{ bNumber: 'B-999' }, { commit: '0'.repeat(40) }]) {
      assert.throws(() => bindWindowsSourceIdentity(changed, { root, diagnostic: true }), /differs/);
    }
    assert.throws(() => bindWindowsSourceIdentity({}, { root }), /no identical-tree/);
    for (const extra of [['--release-kind', 'ls'], ['--payload-signing', 'signed']]) {
      assert.throws(() => zipArgs([...args, ...extra], { root }), /no identical-tree/);
    }
  });
}
