import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import test from 'node:test';

const root = path.resolve(import.meta.dirname, '..');
// Execute the actual scripts in an empty tree. Every external tool is intercepted:
// no compiler, SDK, product directory, signing identity or user preference is touched.
function commands(t, name, jobs, cpuQueryFails = false) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-build-plan-'));
  t.after(() => fs.rmSync(tmp, { recursive: true, force: true }));
  const checkout = path.join(tmp, 'checkout'); fs.mkdirSync(path.join(checkout, 'scripts'), { recursive: true });
  fs.copyFileSync(path.join(root, 'scripts', name), path.join(checkout, 'scripts', name));
  const sdk = path.join(tmp, 'sdk'); fs.mkdirSync(path.join(sdk, 'Interfaces/ACF'), { recursive: true });
  const log = path.join(tmp, 'commands'), init = path.join(tmp, 'init.sh');
  fs.writeFileSync(init, `
record() { printf '%s\\0' "$@" >> "$PLAN_LOG"; printf '\\n' >> "$PLAN_LOG"; }
cargo() { record cargo "$@"; }
cmake() { record cmake "$@"; }
lipo() { record lipo "$@"; [[ "$1" != -archs ]] || echo 'x86_64 arm64'; }
node() { record node "$@"; }
bash() { record bash "$@"; }
sysctl() { [[ "\${PLAN_CPU_QUERY_FAILED:-0}" != 1 ]] || return 1; echo 4; }
uname() { echo Darwin; }
function /usr/libexec/PlistBuddy() { echo true; }
`);
  execFileSync('/bin/bash', [path.join(checkout, 'scripts', name), ...(name.includes('aax') ? ['--sdk', sdk, '--license-confirmed'] : [])],
    { env: { PATH: process.env.PATH, BASH_ENV: init, PLAN_LOG: log, CMAKE_BUILD_PARALLEL_LEVEL: jobs,
      KIRIN_HYPHA_UPDATE_PUBLIC_KEY: '', PLAN_CPU_QUERY_FAILED: cpuQueryFails ? '1' : '0' }, stdio: 'pipe' });
  return fs.readFileSync(log, 'utf8').trim().split('\n').map(line => line.split('\0').slice(0, -1)
    .map(arg => arg.replaceAll(checkout, '<root>').replaceAll(sdk, '<sdk>')));
}
for (const name of ['build_juce_universal.sh', 'build_aax_universal.sh']) {
  test(`${name}: one locked dual-target Cargo invocation; concurrency changes no payload inputs`, t => {
    const serial = commands(t, name, '1'), parallel = commands(t, name, '4');
    const cargo = parallel.filter(args => args[0] === 'cargo');
    assert.deepEqual(cargo, [['cargo', 'build', '--release', '-p', 'kirin_hypha_ffi', '--target',
      'x86_64-apple-darwin', '--target', 'aarch64-apple-darwin', '--locked']]);
    const builds = parallel.filter(args => args[0] === 'cmake' && args[1] === '--build');
    assert.ok(builds.length > 0);
    for (const args of builds) assert.equal(args[args.indexOf('--parallel') + 1], '4');
    const withoutScheduling = rows => rows.map(args => {
      const result = [...args], index = result.indexOf('--parallel');
      if (index >= 0) result.splice(index, 2);
      return result;
    });
    assert.deepEqual(withoutScheduling(parallel), withoutScheduling(serial));
    const lipo = parallel.find(args => args[0] === 'lipo' && args[1] === '-create');
    assert.deepEqual(lipo, ['lipo', '-create', 'target/x86_64-apple-darwin/release/libkirin_hypha_ffi.a',
      'target/aarch64-apple-darwin/release/libkirin_hypha_ffi.a', '-output', 'target/universal/libkirin_hypha_ffi.a']);
    if (name.includes('juce')) assert.ok(builds[0].includes('--clean-first'));
    const automatic = commands(t, name, '');
    assert.equal(automatic.find(args => args[0] === 'cmake' && args[1] === '--build').at(-1), '4');
    const fallback = commands(t, name, '', true);
    assert.equal(fallback.find(args => args[0] === 'cmake' && args[1] === '--build').at(-1), '2');
    assert.throws(() => commands(t, name, '0'));
    assert.throws(() => commands(t, name, '65'));
  });
}

test('retained build products must match every byte, entry and mode; one bit cannot pass', async t => {
  const { verifyBuildBytes } = await import('./ls_release/verify_build_bytes.mjs');
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-build-bytes-'));
  t.after(() => fs.rmSync(tmp, { recursive: true, force: true }));
  const serial = path.join(tmp, 'serial'), parallel = path.join(tmp, 'parallel');
  fs.mkdirSync(serial); fs.mkdirSync(parallel);
  for (const dir of [serial, parallel]) fs.writeFileSync(path.join(dir, 'payload'), Buffer.from([0, 127, 128, 255]), { mode: 0o644 });
  assert.equal(verifyBuildBytes(serial, parallel).passed, true);
  assert.throws(() => verifyBuildBytes(serial, serial));
  fs.writeFileSync(path.join(parallel, 'payload'), Buffer.from([0, 126, 128, 255]));
  assert.throws(() => verifyBuildBytes(serial, parallel));
  fs.copyFileSync(path.join(serial, 'payload'), path.join(parallel, 'payload'));
  fs.chmodSync(path.join(parallel, 'payload'), 0o600);
  assert.throws(() => verifyBuildBytes(serial, parallel));
  fs.chmodSync(path.join(parallel, 'payload'), 0o644);
  fs.writeFileSync(path.join(parallel, 'extra'), 'extra');
  assert.throws(() => verifyBuildBytes(serial, parallel));
});
