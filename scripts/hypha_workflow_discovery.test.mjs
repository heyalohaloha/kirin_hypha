import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const guide = 'docs/hypha_release_entry.md';
const build = 'docs/hypha_build_entry.md';

function validateEntrances(agents, claude, readme) {
  for (const text of [agents, claude, readme]) {
    assert.ok(text.includes(guide), 'missing release guide');
    assert.ok(text.includes(build), 'missing build-only guide');
  }
  assert.match(claude, /^@AGENTS\.md$/m, 'Claude must import the shared project contract');
  assert.ok(agents.indexOf(guide) < agents.indexOf('## 公開リリース3チャネル'), 'entry is not visible at startup');
  assert.ok(readme.indexOf(guide) < readme.indexOf('## Start in under a minute'), 'README hides the developer entry');
}

test('Codex, Claude and README expose the same existing guides at the start', () => {
  validateEntrances(read('AGENTS.md'), read('CLAUDE.md'), read('README.md'));
  for (const file of [guide, build, 'scripts/build_hypha.mjs', 'scripts/release_hypha.mjs']) {
    assert.ok(fs.statSync(path.join(root, file)).isFile(), `missing target: ${file}`);
  }
});

test('missing guides or replacing the Claude import cannot silently break discovery', () => {
  const a = read('AGENTS.md'), c = read('CLAUDE.md'), r = read('README.md');
  assert.throws(() => validateEntrances(a, c.replaceAll(guide, 'missing.md'), r), /missing release guide/);
  assert.throws(() => validateEntrances(a, c.replace('@AGENTS.md', '`@AGENTS.md`'), r), /import/);
  assert.throws(() => validateEntrances(a, c, r.replaceAll(build, 'missing.md')), /missing build-only guide/);
});

test('the guide distinguishes help, resume, old worktrees and mandatory release boundaries', () => {
  const text = read(guide);
  for (const token of ['--release --help', 'build-only', '作業checkout内', 'release_state/',
    '古いworktree', '別checkout', 'LS', '公開承認', 'RELEASE_COMPLETE']) {
    assert.ok(text.includes(token), `missing workflow boundary: ${token}`);
  }
});

test('both entry help modes work from another directory without SDK or signing credentials', () => {
  const env = { PATH: process.env.PATH, ...(process.platform === 'win32' ? { SystemRoot: process.env.SystemRoot } : {}) };
  for (const args of [['--help'], ['--release', '--help']]) {
    const output = execFileSync(process.execPath, [path.join(root, 'scripts/build_hypha.mjs'), ...args],
      { cwd: path.dirname(root), env, encoding: 'utf8' });
    assert.match(output, /Usage:/);
    assert.ok(args.includes('--release') ? output.includes('HP upload') : output.includes('DIAGNOSTIC'));
  }
});

test('quick diagnostic build is visible without changing the full-format and release boundaries', () => {
  for (const file of ['AGENTS.md', 'README.md', guide, build]) {
    assert.ok(read(file).includes('--without-aax'), `missing quick diagnostic entry: ${file}`);
  }
  assert.match(read(build), /通常のPro Tools/);
  assert.ok(read(build).includes('--verify-only'));
});
