import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { capture, protectedRoots, run, snapshot, verify } from './native_storage_guard.mjs';
import { exclusionPaths, exclusionRules } from './native_storage_exclusions.mjs';
import { testOwnership } from './native_storage_guard_ownership_cases.mjs';
import { cleanupChild, contractTimeoutMs, fixture, spawnChild, stopSocket, waitChild } from './native_storage_guard_test_helpers.mjs';

const script = fileURLToPath(new URL('./native_storage_guard.mjs', import.meta.url));
test('native storage guard contract', { timeout: contractTimeoutMs }, async suite => {
await suite.test('unchanged nested files pass; repeated snapshots do not change metadata', t => {
  const f = fixture(t);
  fs.mkdirSync(path.join(f.protectedRoot, 'presence'));
  fs.writeFileSync(path.join(f.protectedRoot, 'presence', 'fixture.json'), '{}');
  const before = capture(f.roots);
  assert.equal(run('setup', f.state, f.roots), 1);
  assert.equal(run('cleanup', f.state, f.roots), 1);
  assert.deepEqual(capture(f.roots), before);
});

await suite.test('create then delete is detected from parent directory timestamps', t => {
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

await suite.test('equal-size overwrite with restored mtime still fails', t => {
  const f = fixture(t);
  const file = path.join(f.protectedRoot, 'fixture.json');
  fs.writeFileSync(file, 'first');
  fs.utimesSync(file, 1, 1);
  const before = capture(f.roots);
  fs.writeFileSync(file, 'other');
  fs.utimesSync(file, 1, 1);
  assert.throws(() => verify(before, f.roots), /storage changed/);
});

await suite.test('deleted and renamed entries fail', t => {
  const f = fixture(t);
  const file = path.join(f.protectedRoot, 'fixture.json');
  fs.writeFileSync(file, '{}');
  const before = capture(f.roots);
  fs.renameSync(file, `${file}.renamed`);
  assert.throws(() => verify(before, f.roots), /storage changed/);
  fs.unlinkSync(`${file}.renamed`);
  assert.throws(() => verify(before, f.roots), /storage changed/);
});

await suite.test('missing roots are read-only no-ops; a newly created Hypha subtree is detected', t => {
  const f = fixture(t);
  const roots = [path.join(f.root, 'absent', 'Kirin OS')];
  assert.equal(run('setup', f.state, roots), 0);
  assert.equal(run('cleanup', f.state, roots), 0);
  assert.equal(fs.existsSync(path.dirname(roots[0])), false);
  fs.mkdirSync(roots[0], { recursive: true });
  assert.throws(() => run('cleanup', f.state, roots), /storage changed/); // Even empty roots are observed.
  fs.mkdirSync(path.join(roots[0], 'plugin_data', 'new-project'), { recursive: true });
  assert.throws(() => run('cleanup', f.state, roots), /storage changed/);
});

await suite.test('missing, corrupt and mismatched baselines fail closed', t => {
  const f = fixture(t);
  assert.throws(() => run('cleanup', f.state, f.roots));
  fs.writeFileSync(f.state, '{');
  assert.throws(() => run('cleanup', f.state, f.roots));
  fs.writeFileSync(f.state, '[]');
  assert.throws(() => run('cleanup', f.state, f.roots), /incompatible/);
  assert.throws(() => run('unknown', f.state, f.roots));
});

await suite.test('baseline writes into a protected tree are rejected before mutation', t => {
  const f = fixture(t);
  const before = capture(f.roots);
  assert.throws(() => run('setup', path.join(f.protectedRoot, 'baseline.json'), f.roots), /outside/);
  assert.deepEqual(capture(f.roots), before);
  const excluded = path.join(f.storageRoot, 'Code Cache');
  fs.mkdirSync(excluded);
  assert.throws(() => run('setup', path.join(excluded, 'baseline.json'), f.roots), /outside/);
  assert.deepEqual(fs.readdirSync(excluded), []);
  if (process.platform !== 'win32') {
    const alias = path.join(f.root, 'alias');
    fs.symlinkSync(excluded, alias);
    assert.throws(() => run('setup', path.join(alias, 'baseline.json'), f.roots), /outside/);
    assert.deepEqual(fs.readdirSync(excluded), []);
  }
});

await suite.test('symlink targets are read, cycles bounded, and baseline symlinks rejected', t => {
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

await suite.test('Unix sockets are observed without connecting; metadata, deletion and replacement still fail', async t => {
  if (process.platform === 'win32') { t.skip('Unix domain filesystem sockets are POSIX-only'); return; }
  const f = fixture(t);
  // Use a short relative pathname only in the child cwd, below the disposable fixture.
  // This avoids Darwin's socket pathname limit without changing the test process cwd.
  const code = `const net=require('node:net'); let connections=0;
    const server=net.createServer(socket=>{++connections;socket.destroy();});
    server.listen('s',()=>process.send({ready:true}));
    process.on('message',message=>{if(message==='stop')server.close(()=>{
      process.send({connections},()=>process.exit(0));});});`;
  const child = spawnChild(t, ['-e', code], { cwd: f.protectedRoot, stdio: ['ignore', 'ignore', 'pipe', 'ipc'] });
  let stderr = '', replacement;
  child.stderr.on('data', bytes => { stderr += bytes; });
  try {
    const ready = await waitChild(child, 'message');
    assert.deepEqual(ready[0], { ready: true });
    const socket = path.join(f.protectedRoot, 's');
    assert.ok(fs.lstatSync(socket).isSocket());
    const before = capture(f.roots);
    assert.equal(run('setup', f.state, f.roots), 1);
    assert.equal(run('cleanup', f.state, f.roots), 1);
    assert.deepEqual(capture(f.roots), before);
    fs.chmodSync(socket, (fs.lstatSync(socket).mode & 0o777) ^ 0o100);
    assert.throws(() => verify(before, f.roots), /storage changed/);
    const changed = capture(f.roots);
    await stopSocket(child);
    assert.equal(fs.existsSync(socket), false);
    assert.throws(() => verify(changed, f.roots), /storage changed/);
    replacement = spawnChild(t, ['-e', code], { cwd: f.protectedRoot, stdio: ['ignore', 'ignore', 'pipe', 'ipc'] });
    replacement.stderr.on('data', bytes => { stderr += bytes; });
    const recreated = await waitChild(replacement, 'message');
    assert.deepEqual(recreated[0], { ready: true });
    assert.ok(fs.lstatSync(socket).isSocket());
    assert.throws(() => verify(changed, f.roots), /storage changed/);
    await stopSocket(replacement);
    fs.writeFileSync(socket, '');
    assert.throws(() => verify(changed, f.roots), /storage changed/);
  } finally {
    const cleanups = await Promise.allSettled([cleanupChild(child), cleanupChild(replacement)]);
    for (const result of cleanups) if (result.status === 'rejected') throw result.reason;
    assert.equal(stderr, '');
  }
});

await suite.test('child responses depend on notifications and exit, without per-response deadlines', async t => {
  const early = spawnChild(t, ['-e', 'process.exit(0)'], { stdio: 'ignore' });
  await assert.rejects(waitChild(early, 'message'), /exited before/);
  const peer = spawnChild(t, ['-e', `process.on('message',()=>process.send('response'));process.send('ready');`],
    { stdio: ['ignore', 'ignore', 'ignore', 'ipc'] });
  assert.deepEqual(await waitChild(peer, 'message'), ['ready']);
  let responded = false;
  const response = waitChild(peer, 'message').then(value => { responded = true; return value; });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(responded, false);
  peer.send('respond');
  assert.deepEqual(await response, ['response']);
  const done = waitChild(peer, 'exit'); peer.kill('SIGTERM');
  assert.equal((await done)[1], 'SIGTERM');
  assert.equal((await waitChild(peer, 'exit'))[1], 'SIGTERM');
});

await suite.test('whole-contract cancellation releases a silent fixture child', async t => {
  const helper = new URL('./native_storage_guard_test_helpers.mjs', import.meta.url).href;
  const source = `import test from 'node:test';
    import {spawnChild,waitChild} from ${JSON.stringify(helper)};
    test('stalled contract',{timeout:100},async t=>{
      const peer=spawnChild(t,['-e',"process.on('message',()=>{})"],{stdio:['ignore','ignore','ignore','ipc']});
      await waitChild(peer,'message');
    });`;
  const child = spawnChild(t, ['--input-type=module', '-e', source], { stdio: ['ignore', 'pipe', 'pipe'] });
  let output = '';
  child.stdout.on('data', bytes => { output += bytes; });
  child.stderr.on('data', bytes => { output += bytes; });
  assert.equal(await new Promise(resolve => child.once('close', resolve)), 1);
  assert.match(output, /test timed out after/);
});

await suite.test('OS home resolution ignores a redirected HOME', t => {
  if (process.platform === 'win32') { t.skip('HOME is a POSIX override'); return; }
  const f = fixture(t);
  const expression = `import { protectedRoots } from ${JSON.stringify(new URL('./native_storage_guard.mjs', import.meta.url).href)};
    process.stdout.write(JSON.stringify(protectedRoots()));`;
  const result = spawnSync(process.execPath, ['--input-type=module', '-e', expression],
    { env: { ...process.env, HOME: f.root }, encoding: 'utf8' });
  assert.equal(result.status, 0);
  assert.deepEqual(JSON.parse(result.stdout), protectedRoots(os.userInfo().homedir));
});

await suite.test('both macOS locations and Windows roaming/local locations are covered', t => {
  const f = fixture(t);
  assert.deepEqual(protectedRoots(f.root, 'darwin', {}), [
    path.join(f.root, 'Documents', 'Kirin OS'), path.join(f.root, 'Library', 'Application Support', 'Kirin OS'),
  ]);
  assert.deepEqual(protectedRoots(f.root, 'win32', {}), [
    path.join(f.root, 'Documents', 'Kirin OS'), path.join(f.root, 'AppData', 'Roaming', 'Kirin OS'),
    path.join(f.root, 'AppData', 'Local', 'Kirin OS'),
  ]);
});

await suite.test('CLI refuses invalid arguments without exposing private paths', t => {
  const f = fixture(t);
  const result = spawnSync(process.execPath, [script, 'cleanup', f.state], { encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /FAIL/);
  assert.ok(!result.stderr.includes(f.root));
});

// Expectations are independent of the exclusion implementation and source extraction.
const guardedPaths = [
  'plugin_data/project/instance/pre/record.json', 'plugin_data/project/record_reservation/fixture',
  'plugin_data/reference/v2/library/manifest.json', 'plugin_data/reference/v2/library/sets.json',
  'plugin_data/reference/v2/sources/fixture', 'plugin_data/reference/v2/a_bindings/fixture',
  'plugin_data/reference/v2/recovery_acknowledgements/fixture',
  'plugin_data/reference/v2/requests/fixture', 'plugin_data/reference/v2/library/events/fixture',
  'plugin_data/reference/v2/a_captures/fixture', 'plugin_data/hypha_ab/v1/capabilities/fixture',
  'plugin_data/hypha_ab/v1/preparations/fixture', 'plugin_data/pre_display/v1/guides/fixture',
  'plugin_data/pre_display/v1/acknowledgements/fixture', 'plugin_data/hypha_capture/v1/requests/fixture',
  'plugin_data/appearance/v1/hypha-jungle-preference.json', 'plugin_data/appearance/v1/hypha-jungle-preference.lock',
  'plugin_data/appearance/v1/os-jungle-activation.json', 'identity.json', 'licence.json',
  'config.json.backup', 'database.sqlite-wal', 'plugin_data/config.json', 'unexpected/new/writer',
  'Local Storage/unexpected', 'Local Storage/leveldb-other/file', 'GPUCache-backup/file',
  'plugin_data/GPUCache/file', 'other/folder-scan/object', 'PLUGIN_DATA/CONSTRUCTOR/file',
  'plugin_data/__proto__/file', '.config.json.tmp-other', '.config.json.tmp-123-4/unknown', 'config.json/unknown',
];

await suite.test('one-byte creation and same-size overwrite anywhere outside exclusions fail', t => {
  const f = fixture(t);
  for (const relative of guardedPaths) {
    const file = path.join(f.storageRoot, relative);
    const before = capture(f.roots);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, Buffer.from([0]));
    assert.throws(() => verify(before, f.roots), /storage changed/, relative);
    fs.utimesSync(file, 1, 1);
    const overwritten = capture(f.roots);
    fs.writeFileSync(file, Buffer.from([1]));
    fs.utimesSync(file, 1, 1);
    assert.throws(() => verify(overwritten, f.roots), /storage changed/, relative);
  }
});

await suite.test('new unknown writes in every macOS and Windows root fail without a source rule', t => {
  const f = fixture(t);
  for (const platform of ['darwin', 'win32']) {
    const roots = protectedRoots(path.join(f.root, platform), platform, {});
    run('setup', f.state, roots);
    for (const root of roots) {
      const file = path.join(root, 'unexpected', 'new-writer');
      fs.mkdirSync(path.dirname(file), { recursive: true });
      fs.writeFileSync(file, Buffer.from([1]));
      assert.throws(() => run('cleanup', f.state, roots), /storage changed/);
      fs.rmSync(root, { recursive: true });
    }
    run('cleanup', f.state, roots);
  }
});

await suite.test('cleanup detects transient leases, renames, removals and symlink target writes', t => {
  const f = fixture(t);
  const lease = path.join(f.protectedRoot, 'lease');
  fs.utimesSync(f.protectedRoot, 1, 1);
  run('setup', f.state, f.roots);
  fs.writeFileSync(lease, 'x'); fs.unlinkSync(lease);
  assert.throws(() => run('cleanup', f.state, f.roots), /storage changed/);
  fs.writeFileSync(lease, 'x');
  run('setup', f.state, f.roots);
  fs.renameSync(lease, lease + '-renamed');
  assert.throws(() => run('cleanup', f.state, f.roots), /storage changed/);
  run('setup', f.state, f.roots);
  fs.unlinkSync(lease + '-renamed');
  assert.throws(() => run('cleanup', f.state, f.roots), /storage changed/);
  if (process.platform === 'win32') return;
  const target = path.join(f.root, 'symlink-target');
  fs.writeFileSync(target, 'x');
  fs.symlinkSync(target, lease);
  run('setup', f.state, f.roots);
  fs.writeFileSync(target, 'y');
  assert.throws(() => run('cleanup', f.state, f.roots), /storage changed/);
});

await suite.test('legacy whole-tree, narrowed v2 and changed-exclusion baselines are rejected', t => {
  const f = fixture(t);
  for (const baseline of [capture(f.roots), { schema: 'hypha-storage-guard-v2', before: capture(f.roots) },
    { schema: 'hypha-storage-guard-v3', exclusions: exclusionRules, before: capture(f.roots) }]) {
    fs.writeFileSync(f.state, JSON.stringify(baseline));
    assert.throws(() => run('cleanup', f.state, f.roots), /incompatible/);
  }
  run('setup', f.state, f.roots);
  const baseline = JSON.parse(fs.readFileSync(f.state, 'utf8'));
  baseline.exclusions.pop();
  fs.writeFileSync(f.state, JSON.stringify(baseline));
  assert.throws(() => run('cleanup', f.state, f.roots), /incompatible/);
  run('setup', f.state, f.roots);
  const changedPattern = JSON.parse(fs.readFileSync(f.state, 'utf8'));
  changedPattern.exclusions.find(rule => rule.pattern).pattern = '.*';
  fs.writeFileSync(f.state, JSON.stringify(changedPattern));
  assert.throws(() => run('cleanup', f.state, f.roots), /incompatible/);
});

// Independent log-shaped expectations: adding an exclusion must also update this review fixture.
const osPaths = ['Code Cache/js/data', 'GPUCache/data', 'DawnGraphiteCache/data',
  'DawnWebGPUCache/data', 'GraphiteDawnCache/data', 'DevToolsActivePort', 'DIPS', 'DIPS-wal',
  'Local Storage/leveldb/LOG', 'Preferences', 'Session Storage/LOG',
  'SingletonCookie', 'SingletonLock', 'SingletonSocket', 'config.json', '.config.json.tmp-123-4', 'folder-scan/roots/fixture/objects/one'];

await suite.test('only exact root-relative exclusions ignore creation, replacement and removal', t => {
  const f = fixture(t);
  assert.deepEqual([...exclusionPaths].sort(), osPaths.map(name =>
    name.startsWith('.config.json.tmp-') ? '.config.json.tmp-<pid>-<counter>'
      : name.startsWith('Local Storage/') ? 'Local Storage/leveldb' : name.split('/')[0]).sort());
  run('setup', f.state, f.roots);
  for (const relative of osPaths) {
    const file = path.join(f.storageRoot, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, 'x');
  }
  run('cleanup', f.state, f.roots);
  for (const relative of osPaths) fs.unlinkSync(path.join(f.storageRoot, relative));
  run('cleanup', f.state, f.roots);
  for (const relative of osPaths) {
    const file = path.join(f.storageRoot, relative.toUpperCase().replace('LOCAL STORAGE/', 'Local Storage/'));
    fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, 'x');
  }
  run('cleanup', f.state, f.roots);
});

await suite.test('app config save and numeric temporary siblings pass; unrelated files remain guarded', t => {
  const f = fixture(t), config = path.join(f.storageRoot, 'config.json');
  run('setup', f.state, f.roots);
  const temporary = path.join(f.storageRoot, '.config.json.tmp-321-1');
  fs.writeFileSync(temporary, '{"enabled":true}');
  run('cleanup', f.state, f.roots); // Also tolerate a snapshot while the save is in progress.
  fs.renameSync(temporary, config);
  run('cleanup', f.state, f.roots);
  fs.writeFileSync(path.join(f.storageRoot, '.config.json.tmp-321-other'), 'x');
  assert.throws(() => run('cleanup', f.state, f.roots), /storage changed/);
});

await suite.test('excluded-only parent creation and removal pass; mixed contents and symlinks remain guarded', t => {
  const f = fixture(t), parent = path.join(f.storageRoot, 'Local Storage');
  run('setup', f.state, f.roots);
  const cache = path.join(parent, 'leveldb', 'LOG');
  fs.mkdirSync(path.dirname(cache), { recursive: true }); fs.writeFileSync(cache, 'x');
  run('cleanup', f.state, f.roots);
  fs.rmSync(parent, { recursive: true });
  run('cleanup', f.state, f.roots);
  fs.mkdirSync(parent);
  run('cleanup', f.state, f.roots);
  fs.writeFileSync(path.join(parent, 'unknown'), Buffer.from([1]));
  assert.throws(() => run('cleanup', f.state, f.roots), /storage changed/);
  run('setup', f.state, f.roots);
  fs.renameSync(parent, parent + '-old'); fs.mkdirSync(parent);
  fs.renameSync(path.join(parent + '-old', 'unknown'), path.join(parent, 'unknown'));
  fs.rmdirSync(parent + '-old');
  assert.throws(() => run('cleanup', f.state, f.roots), /storage changed/);
  fs.rmSync(parent, { recursive: true });
  run('setup', f.state, f.roots);
  if (process.platform === 'win32') return;
  const target = path.join(f.root, 'external-parent'); fs.mkdirSync(target);
  fs.symlinkSync(target, parent);
  assert.throws(() => run('cleanup', f.state, f.roots), /storage changed/);
});

await testOwnership(suite);

await suite.test('concurrent app-owned updates pass; an additional Hypha write fails', async t => {
  const f = fixture(t);
  const code = `const fs=require('node:fs'), path=require('node:path');
    const names=${JSON.stringify(osPaths)};
    let generation=0, running=true;
    function mutate(){for(const name of names){fs.mkdirSync(path.dirname(name),{recursive:true});
      fs.writeFileSync(name,String(generation));} ++generation;}
    function tick(){if(running){mutate();setImmediate(tick);}}
    process.on('message',message=>{if(message==='stop'){running=false;process.send({generation},()=>process.exit(0));}
      else {mutate();process.send({generation});}});
    mutate();process.send({ready:true});tick();`;
  const child = spawnChild(t, ['-e', code],
    { cwd: f.storageRoot, stdio: ['ignore', 'ignore', 'pipe', 'ipc'] });
  let stderr = '';
  child.stderr.on('data', bytes => { stderr += bytes; });
  try {
    assert.deepEqual((await waitChild(child, 'message'))[0], { ready: true });
    run('setup', f.state, f.roots);
    const progress = waitChild(child, 'message'); child.send('advance');
    assert.ok((await progress)[0].generation >= 2);
    run('cleanup', f.state, f.roots);
    const file = path.join(f.protectedRoot, 'accidental-write');
    fs.writeFileSync(file, Buffer.from([1]));
    assert.throws(() => run('cleanup', f.state, f.roots), /storage changed/);
    const stopped = waitChild(child, 'exit'); child.send('stop');
    assert.equal((await stopped)[0], 0);
  } finally { await cleanupChild(child); }
  assert.equal(stderr, '');
});

await suite.test('CTest filtered runs include the guard; cleanup detects writes even after native failure', t => {
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
    `import fs from 'node:fs'; fs.writeFileSync(${JSON.stringify(path.join(f.protectedRoot, 'lease'))}, 'x'); process.exit(7);`);
  fs.writeFileSync(path.join(scripts, 'os.mjs'),
    `import fs from 'node:fs'; fs.writeFileSync(${JSON.stringify(path.join(f.storageRoot, 'DevToolsActivePort'))}, 'x');`);
  const cmakePath = value => value.replaceAll('\\', '/');
  fs.writeFileSync(path.join(source, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.22)
project(StorageGuardFixture NONE)
enable_testing()
add_test(NAME native_ok COMMAND "${cmakePath(process.execPath)}" "${cmakePath(path.join(scripts, 'os.mjs'))}")
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
  const contract = invoke('ctest', ['--test-dir', build, '--show-only=json-v1', '-R', '^kirin_native_storage_guard_contract$']);
  const timeout = JSON.parse(contract.stdout).tests.find(item => item.name === 'kirin_native_storage_guard_contract')
    .properties.find(item => item.name === 'TIMEOUT').value;
  assert.equal(timeout, 60); assert.ok(contractTimeoutMs < timeout * 1000);
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
});
