import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  canonicalBLabel,
  duplicateAllowlistErrors,
  duplicateBGroups,
  parseAllowlist,
  parseReachableCommits,
  renderAllowlist,
  unreachableSemverTags,
} from './check_public_history.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const sha = (digit) => digit.repeat(40);
const commit = (digit, subject) => ({ sha: sha(digit), subject });

test('only a canonical leading B label enters duplicate accounting', () => {
  assert.equal(canonicalBLabel('[B-014] first split'), 'B-014');
  assert.equal(canonicalBLabel('[B-14] normalized label'), 'B-014');
  assert.equal(canonicalBLabel('prefix [B-014]'), null);
  assert.equal(canonicalBLabel('[B-447/B-448] composite legacy label'), null);
  assert.equal(canonicalBLabel('[ci full] [B-014]'), null);
});

test('git log parser requires complete records and full SHAs', () => {
  assert.deepEqual(
    parseReachableCommits(`${sha('a')}\0[B-001] one\0`),
    [{ sha: sha('a'), subject: '[B-001] one' }],
  );
  assert.throws(() => parseReachableCommits(`${sha('a')}\0`), /incomplete/);
  assert.throws(() => parseReachableCommits(`short\0subject\0`), /invalid full SHA/);
});

test('exact historical duplicate SHA sets pass', () => {
  const commits = [
    commit('a', '[B-014] part one'),
    commit('b', '[B-014] part two'),
    commit('c', '[B-015] unique'),
  ];
  const source = renderAllowlist(duplicateBGroups(commits));
  assert.deepEqual(duplicateAllowlistErrors(commits, source), []);
});

test('an unapproved duplicate and a changed approved set both fail', () => {
  const original = [commit('a', '[B-014] one'), commit('b', '[B-014] two')];
  const source = renderAllowlist(duplicateBGroups(original));
  const addedGroup = [...original, commit('c', '[B-015] one'), commit('d', '[B-015] two')];
  assert.match(duplicateAllowlistErrors(addedGroup, source).join('\n'), /unapproved duplicate B-015/);
  const changedSet = [commit('a', '[B-014] one'), commit('c', '[B-014] replacement')];
  const errors = duplicateAllowlistErrors(changedSet, source).join('\n');
  assert.match(errors, /unapproved SHA/);
  assert.match(errors, /lost approved SHA/);
});

test('a stale or manually reordered allowlist fails', () => {
  const commits = [commit('a', '[B-014] one'), commit('b', '[B-014] two')];
  const source = renderAllowlist(duplicateBGroups(commits));
  assert.match(
    duplicateAllowlistErrors([commits[0]], source).join('\n'),
    /stale duplicate allowlist group/,
  );
  const reordered = source.replace(`B-014\t${sha('a')}\nB-014\t${sha('b')}`, `B-014\t${sha('b')}\nB-014\t${sha('a')}`);
  assert.match(duplicateAllowlistErrors(commits, reordered).join('\n'), /exact deterministic/);
});

test('allowlist parser rejects short SHAs and duplicate rows', () => {
  assert.throws(() => parseAllowlist('B-014\tshort\n'), /invalid full SHA/);
  assert.throws(
    () => parseAllowlist(`B-014\t${sha('a')}\nB-014\t${sha('a')}\n`),
    /repeats/,
  );
});

test('only strict public SemVer tags must reach candidate main', () => {
  const tags = ['v1.1.48', 'v2.0.0-rc.1', 'release-1.0.0', 'v1.1.47'];
  assert.deepEqual(unreachableSemverTags(tags, (tag) => tag === 'v1.1.48'), ['v1.1.47']);
});

test('CI runs the public history contract without an event condition', () => {
  const workflow = fs.readFileSync(path.join(repoRoot, '.github', 'workflows', 'ci.yml'), 'utf8');
  const start = workflow.indexOf('  public-history:\n');
  const end = workflow.indexOf('\n  test:\n', start);
  assert.ok(start >= 0 && end > start, 'public-history job must precede the release-source job');
  const job = workflow.slice(start, end);
  assert.match(job, /name: public history identity/);
  assert.match(job, /fetch-depth: 0/);
  assert.match(job, /node --test scripts\/public_history\.test\.mjs/);
  assert.match(job, /node scripts\/check_public_history\.mjs --tip HEAD/);
  assert.doesNotMatch(job, /^\s+if:/m);
});

test('the public history policy names the protected main checks and merge method', () => {
  const policy = fs.readFileSync(path.join(repoRoot, 'docs', 'public_history_identity.md'), 'utf8');
  for (const check of [
    'public history identity',
    'release source contract (macos)',
    'auval arm64 (AU validation)',
    'windows VST3 preflight',
  ]) {
    assert.match(policy, new RegExp(check.replace(/[()]/g, '\\$&')));
  }
  assert.match(policy, /Merge commits are the only enabled pull-request merge method/);
  assert.match(policy, /Force pushes and branch deletion are disabled/);
});

test('public measurement claims retain the audited scope and neutral standard label', () => {
  const readme = fs.readFileSync(path.join(repoRoot, 'README.md'), 'utf8');
  const audit = fs.readFileSync(
    path.join(repoRoot, 'docs', 'hypha_bs1770_5_r128_v5_audit_20260831.md'),
    'utf8',
  );
  assert.match(readme, /docs\/hypha_bs1770_5_r128_v5_audit_20260831\.md/);
  assert.doesNotMatch(readme, /ITU-R BS\.1770-4/);
  assert.match(audit, /9cc500b4df83f7c21855c74dce795ef5209a752bf884253ae57d0ce512efb062/);
  assert.match(audit, /94d78c8e5399291e1c69440c5096b7925f211c93/);
  assert.match(audit, /公式test set全70素材の実測は完了した/);
  assert.doesNotMatch(audit, /Hypha自身で公式test setを完走していない/);
});
