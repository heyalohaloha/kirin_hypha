import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { capture, protectedRoots, run, snapshot, verify } from './native_storage_guard.mjs';

const script = fileURLToPath(new URL('./native_storage_guard.mjs', import.meta.url));
function fixture(t) {
  // Always below the working build/checkout; never create a fake tree in the real account home.
  const parent = path.join(process.cwd(), 'target', 'native-storage-guard-fixtures');
  fs.mkdirSync(parent, { recursive: true });
  const root = fs.mkdtempSync(path.join(parent, 'case-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const protectedRoot = path.join(root, 'protected');
  fs.mkdirSync(protectedRoot);
  return { root, protectedRoot, roots: [protectedRoot], state: path.join(root, 'baseline.json') };
}

test('unchanged nested files pass; repeated snapshots do not change metadata', t => {
  const f = fixture(t);
  fs.mkdirSync(path.join(f.protectedRoot, 'presence'));
  fs.writeFileSync(path.join(f.protectedRoot, 'presence', 'fixture.json'), '{}');
  const before = capture(f.roots);
  assert.equal(run('setup', f.state, f.roots), 1);
  assert.equal(run('cleanup', f.state, f.roots), 1);
  assert.deepEqual(capture(f.roots), before);
});

test('create then delete is detected from parent directory timestamps', t => {
  const f = fixture(t);
  const dir = path.join(f.protectedRoot, 'presence');
  fs.mkdirSync(dir);
  // Deterministic on filesystems with coarse timestamps, without sleeping.
  fs.utimesSync(dir, 1, 1);
  const before = capture(f.roots);
  const lease = path.join(dir, 'transient.json');
  fs.writeFileSync(lease, '{}');
  fs.unlinkSync(lease);
  assert.deepEqual(fs.readdirSync(dir), []);
  assert.throws(() => verify(before, f.roots), /storage changed/);
});

test('equal-size overwrite with restored mtime still fails', t => {
  const f = fixture(t);
  const file = path.join(f.protectedRoot, 'fixture.json');
  fs.writeFileSync(file, 'first');
  fs.utimesSync(file, 1, 1);
  const before = capture(f.roots);
  fs.writeFileSync(file, 'other');
  fs.utimesSync(file, 1, 1);
  assert.notDeepEqual(snapshot(f.protectedRoot)[1].metadata, before[0].entries[1].metadata);
  assert.throws(() => verify(before, f.roots), /storage changed/);
});

test('deleted and renamed entries fail', t => {
  const f = fixture(t);
  const file = path.join(f.protectedRoot, 'fixture.json');
  fs.writeFileSync(file, '{}');
  const before = capture(f.roots);
  fs.renameSync(file, `${file}.renamed`);
  assert.throws(() => verify(before, f.roots), /storage changed/);
  fs.unlinkSync(`${file}.renamed`);
  assert.throws(() => verify(before, f.roots), /storage changed/);
});

test('missing roots are read-only no-ops; a newly created root is detected', t => {
  const f = fixture(t);
  const roots = [path.join(f.root, 'absent', 'Kirin OS')];
  assert.equal(run('setup', f.state, roots), 0);
  assert.equal(run('cleanup', f.state, roots), 0);
  assert.equal(fs.existsSync(path.dirname(roots[0])), false);
  fs.mkdirSync(roots[0], { recursive: true });
  assert.throws(() => run('cleanup', f.state, roots), /storage changed/);
});

test('missing, corrupt and mismatched baselines fail closed', t => {
  const f = fixture(t);
  assert.throws(() => run('cleanup', f.state, f.roots));
  fs.writeFileSync(f.state, '{');
  assert.throws(() => run('cleanup', f.state, f.roots));
  fs.writeFileSync(f.state, '[]');
  assert.throws(() => run('cleanup', f.state, f.roots), /incompatible/);
  assert.throws(() => run('unknown', f.state, f.roots));
});

test('baseline writes into a protected tree are rejected before mutation', t => {
  const f = fixture(t);
  const before = capture(f.roots);
  assert.throws(() => run('setup', path.join(f.protectedRoot, 'baseline.json'), f.roots), /outside/);
  assert.deepEqual(capture(f.roots), before);
});

test('symlink targets are read, cycles bounded, and baseline symlinks rejected', t => {
  if (process.platform === 'win32') { t.skip('creating symlinks requires a Windows privilege'); return; }
  const f = fixture(t);
  const file = path.join(f.root, 'target.json');
  fs.writeFileSync(file, 'one');
  fs.symlinkSync(file, path.join(f.protectedRoot, 'link'));
  fs.symlinkSync(f.protectedRoot, path.join(f.protectedRoot, 'cycle'));
  fs.symlinkSync(path.join(f.root, 'missing'), path.join(f.protectedRoot, 'dangling'));
  const before = capture(f.roots);
  verify(before, f.roots);
  fs.writeFileSync(file, 'two');
  assert.throws(() => verify(before, f.roots), /storage changed/);
  fs.symlinkSync(file, f.state);
  assert.throws(() => run('setup', f.state, f.roots), /symbolic link/);
  assert.equal(fs.readFileSync(file, 'utf8'), 'two');
});

test('OS home resolution ignores a redirected HOME', t => {
  if (process.platform === 'win32') { t.skip('HOME is a POSIX override'); return; }
  const f = fixture(t);
  const expression = `import { protectedRoots } from ${JSON.stringify(new URL('./native_storage_guard.mjs', import.meta.url).href)};
    process.stdout.write(JSON.stringify(protectedRoots()));`;
  const result = spawnSync(process.execPath, ['--input-type=module', '-e', expression],
    { env: { ...process.env, HOME: f.root }, encoding: 'utf8' });
  assert.equal(result.status, 0);
  assert.deepEqual(JSON.parse(result.stdout), protectedRoots(os.userInfo().homedir));
});

test('both macOS locations and Windows roaming/local locations are covered', t => {
  const f = fixture(t);
  assert.deepEqual(protectedRoots(f.root, 'darwin', {}), [
    path.join(f.root, 'Documents', 'Kirin OS'), path.join(f.root, 'Library', 'Application Support', 'Kirin OS'),
  ]);
  assert.deepEqual(protectedRoots(f.root, 'win32', {}), [
    path.join(f.root, 'Documents', 'Kirin OS'), path.join(f.root, 'AppData', 'Roaming', 'Kirin OS'),
    path.join(f.root, 'AppData', 'Local', 'Kirin OS'),
  ]);
});

test('CLI refuses invalid arguments without exposing private paths', t => {
  const f = fixture(t);
  const result = spawnSync(process.execPath, [script, 'cleanup', f.state], { encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /FAIL/);
  assert.ok(!result.stderr.includes(f.root));
});

test('CTest filtered runs include the guard; cleanup detects writes even after native failure', t => {
  const f = fixture(t);
  const source = path.join(f.root, 'juce_shell');
  const scripts = path.join(f.root, 'scripts');
  const build = path.join(f.root, 'build');
  fs.mkdirSync(source); fs.mkdirSync(scripts);
  const cmakeGuard = fileURLToPath(new URL('../juce_shell/cmake/NativeStorageGuard.cmake', import.meta.url));
  // Use the real CMake fixture wiring and guard implementation, but exclusively disposable data.
  fs.writeFileSync(path.join(scripts, 'native_storage_guard.mjs'),
    `import { run } from ${JSON.stringify(new URL('./native_storage_guard.mjs', import.meta.url).href)};
     run(process.argv[2], process.argv[3], ${JSON.stringify(f.roots)});`);
  fs.writeFileSync(path.join(scripts, 'native_storage_guard.test.mjs'), '');
  fs.copyFileSync(new URL('./native_path_budget.mjs', import.meta.url), path.join(scripts, 'native_path_budget.mjs'));
  fs.writeFileSync(path.join(scripts, 'fail.mjs'),
    `import fs from 'node:fs'; fs.writeFileSync(${JSON.stringify(path.join(f.protectedRoot, 'lease'))}, '{}'); process.exit(7);`);
  const cmakePath = value => value.replaceAll('\\', '/');
  fs.writeFileSync(path.join(source, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.22)
project(StorageGuardFixture NONE)
enable_testing()
add_test(NAME native_ok COMMAND "${cmakePath(process.execPath)}" -e "process.exit(0)")
add_test(NAME native_failure COMMAND "${cmakePath(process.execPath)}" "${cmakePath(path.join(scripts, 'fail.mjs'))}")
include("${cmakePath(cmakeGuard)}")
`);
  const invoke = (command, args) => spawnSync(command, args, { encoding: 'utf8' });
  const configured = invoke('cmake', ['-S', source, '-B', build]);
  assert.equal(configured.status, 0, configured.stderr);
  const inventory = invoke('ctest', ['--test-dir', build, '--show-only=json-v1', '-R', '^native_ok$']);
  assert.equal(inventory.status, 0, inventory.stderr);
  assert.deepEqual(JSON.parse(inventory.stdout).tests.map(item => item.name).sort(),
    ['kirin_native_path_budget_cleanup', 'kirin_native_path_budget_setup',
      'kirin_native_storage_guard_cleanup', 'kirin_native_storage_guard_setup', 'native_ok']);
  const pass = invoke('ctest', ['--test-dir', build, '--output-on-failure', '-R', '^native_ok$']);
  assert.equal(pass.status, 0, pass.stdout + pass.stderr);
  const fail = invoke('ctest', ['--test-dir', build, '--output-on-failure', '-R', '^native_failure$']);
  assert.notEqual(fail.status, 0);
  assert.match(fail.stdout, /kirin_native_storage_guard_cleanup[^\n]*Failed/);
  assert.match(fail.stdout, /native_failure[^\n]*Failed/);
  // A failed setup must prevent the native body from starting, not silently disable protection.
  fs.unlinkSync(path.join(f.protectedRoot, 'lease'));
  fs.writeFileSync(path.join(scripts, 'native_storage_guard.mjs'), 'process.exit(3);');
  const blocked = invoke('ctest', ['--test-dir', build, '--output-on-failure', '-R', '^native_failure$']);
  assert.notEqual(blocked.status, 0);
  assert.match(blocked.stdout, /native_failure[^\n]*Not Run/);
  assert.equal(fs.existsSync(path.join(f.protectedRoot, 'lease')), false);
});
