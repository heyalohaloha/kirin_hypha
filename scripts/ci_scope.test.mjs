import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { classify, scope } from './ci_scope.mjs';

const ci = readFileSync(new URL('../.github/workflows/ci.yml', import.meta.url), 'utf8');
const job = name => ci.split(`  ${name}:\n`)[1]?.split(/\n  [\w-]+:\n/)[0] ?? '';
const base = 'a'.repeat(40), head = 'b'.repeat(40);
const pr = { EVENT_NAME: 'pull_request', BASE_SHA: base, HEAD_SHA: head };

test('only documentation is light; code, build files, tooling and workflows are full', () => {
  assert.equal(classify(['README.md', 'docs/manual/time.md', 'docs/assets/screen.png']).full, false);
  for (const path of ['juce_shell/src/PluginProcessor.cpp', 'crates/kirin_measure/src/lib.rs', 'scripts/ci_scope.mjs',
    'scripts/ls_release/hypha_release_local.mjs', '.github/workflows/ci.yml', 'juce_shell/README.md', 'Cargo.lock',
    'juce_shell/CMakeLists.txt', 'docsx/a.md', 'README.md.sh']) {
    assert.equal(classify(['README.md', path]).full, true, path);
  }
  assert.equal(classify([]).full, true);
  assert.equal(classify(undefined).full, true);
});

test('push, dispatch, missing ranges and diff failures run everything', () => {
  const git = () => 'README.md\n';
  for (const EVENT_NAME of ['push', 'workflow_dispatch', 'pull_request_target', undefined]) {
    assert.equal(scope({ ...pr, EVENT_NAME }, git).full, true);
  }
  assert.equal(scope({ ...pr, BASE_SHA: '' }, git).full, true);
  assert.equal(scope({ ...pr, HEAD_SHA: 'main' }, git).full, true);
  assert.equal(scope(pr, () => { throw new Error('fatal: bad revision'); }).full, true);
  let args;
  assert.equal(scope(pr, received => { args = received; return 'README.md\ndocs/a.md\n'; }).full, false);
  assert.deepEqual(args, ['diff', '--name-only', '--no-renames', `${base}...${head}`]);
});

test('skipped jobs follow the scope and a failed scope cannot skip them', () => {
  const scopeJob = job('scope');
  assert.match(scopeJob, /fetch-depth: 0/);
  assert.match(scopeJob, /BASE_SHA: \$\{\{ github\.event\.pull_request\.base\.sha }}/);
  assert.match(scopeJob, /run: node scripts\/ci_scope\.mjs >> "\$GITHUB_OUTPUT"/);
  for (const name of ['auval-arm64', 'windows-vst3-preflight']) {
    const body = job(name);
    assert.match(body, /needs: \[scope]/, name);
    assert.match(body, /if: \$\{\{ !cancelled\(\) && needs\.scope\.outputs\.full != 'false' && \(/, name);
  }
  const phases = job('release-source');
  assert.match(phases, /needs: \[scope]/);
  assert.match(phases, /RUN_PHASE: \$\{\{ matrix\.phase == 'checks' \|\| needs\.scope\.outputs\.full != 'false' }}/);
  const steps = phases.split('    steps:\n')[1].split(/\n(?=      - )/);
  for (const step of steps) assert.match(step, /if: .*env\.RUN_PHASE == 'true'/, step.split('\n')[0]);
});

test('every run records whether it tested the pull request head tree', () => {
  const history = job('public-history');
  assert.match(history, /- name: Compare tested tree with head\n        id: tree/);
  assert.match(history, /- name: Tested tree equals head\n        if: steps\.tree\.outputs\.same == 'true'/);
  assert.match(history, /HEAD_SHA: \$\{\{ github\.event\.pull_request\.head\.sha \|\| github\.sha }}/);
});
