import test from 'node:test';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { inspectBundle, preparePlan, quoteShell, snapshotBundle, validateRoles, verifySnapshot } from './aax_swap_preflight.mjs';
const posixTest = process.platform === 'win32' ? test.skip : test;


function fixture(t) {
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'aax-swap-preflight-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const bundle = path.join(root, "bundle-quote'$().aaxplugin");
  fs.mkdirSync(path.join(bundle, 'Contents'), { recursive: true });
  fs.writeFileSync(path.join(bundle, 'Contents', 'data'), 'candidate');
  return { root, bundle };
}

posixTest('shell quoting preserves special characters as one literal argument', () => {
  const value = "space ' \" $()\nsemicolon;";
  const result = spawnSync('/bin/sh', ['-c', `printf %s ${quoteShell(value)}`], { encoding: 'utf8' });
  assert.equal(result.status, 0);
  assert.equal(result.stdout, value);
  assert.throws(() => quoteShell('bad\0value'), /NUL/);
});

posixTest('preflight hashes bundle deterministically and rejects changed expected hash', t => {
  const { bundle } = fixture(t);
  const first = inspectBundle(bundle);
  assert.match(first.sha256, /^[a-f0-9]{64}$/);
  assert.equal(inspectBundle(bundle, first.sha256).sha256, first.sha256);
  fs.writeFileSync(path.join(bundle, 'Contents', 'data'), 'different');
  assert.throws(() => inspectBundle(bundle, first.sha256), /hash mismatch/);
});

posixTest('preflight rejects symlinks and hardlinks including a linked ancestor', t => {
  const { root, bundle } = fixture(t);
  const link = path.join(root, 'linked');
  fs.symlinkSync(bundle, link);
  assert.throws(() => inspectBundle(link), /symlink/);
  fs.symlinkSync('data', path.join(bundle, 'Contents', 'link'));
  assert.throws(() => inspectBundle(bundle), /symlink/);
  fs.unlinkSync(path.join(bundle, 'Contents', 'link'));
  fs.linkSync(path.join(bundle, 'Contents', 'data'), path.join(root, 'hardlink'));
  assert.throws(() => inspectBundle(bundle), /hardlink/);
});

posixTest('preflight requires exactly the manifest PRE and POST roles', () => {
  const manifest = [{ role: 'PRE' }, { role: 'POST' }];
  assert.doesNotThrow(() => validateRoles(manifest, ['PRE', 'POST']));
  assert.throws(() => validateRoles(manifest, ['PRE', 'OTHER']), /role/);
  assert.throws(() => validateRoles(manifest, ['PRE', 'PRE']), /role/);
  assert.throws(() => validateRoles([{ role: 'PRE' }, { role: 'OTHER' }], ['PRE', 'OTHER']), /role/);
});

posixTest('prepare plan binds exactly two fixed manifest destinations and verifies both sources', t => {
  const { root, bundle } = fixture(t);
  const second = path.join(root, 'second.aaxplugin');
  fs.mkdirSync(second);
  fs.writeFileSync(path.join(second, 'payload'), 'post');
  const manifest = [
    { role: 'PRE', install_relative: 'Library/Application Support/Avid/Audio/Plug-Ins/Kirin Hypha PRE.aaxplugin' },
    { role: 'POST', install_relative: 'Library/Application Support/Avid/Audio/Plug-Ins/Kirin Hypha POST.aaxplugin' },
  ];
  const input = [
    { role: 'PRE', source: bundle, sha256: inspectBundle(bundle).sha256 },
    { role: 'POST', source: second, sha256: inspectBundle(second).sha256 },
  ];
  const plan = preparePlan(manifest, input);
  assert.deepEqual(plan.map(x => x.target), manifest.map(x => `/${x.install_relative}`));
  assert.throws(() => preparePlan(manifest, [{ ...input[0], sha256: '0'.repeat(64) }, input[1]]), /hash mismatch/);
  assert.throws(() => preparePlan([{ ...manifest[0], install_relative: 'Library/Other.aaxplugin' }, manifest[1]], input), /destination/);
  assert.throws(() => preparePlan(manifest, [input[0], { ...input[1], role: 'OTHER' }]), /role/);
});


posixTest('snapshot contains the authenticated file bytes and rejects changed content', t => {
  const { bundle } = fixture(t);
  const expected = inspectBundle(bundle).sha256;
  const snapshot = snapshotBundle(bundle, expected);
  assert.equal(snapshot.sha256, expected);
  assert.equal(Buffer.from(snapshot.files[0].base64, 'base64').toString(), 'candidate');
  assert.equal(snapshot.files[0].path, 'Contents/data');
  fs.writeFileSync(path.join(bundle, 'Contents', 'data'), 'substitution');
  assert.throws(() => snapshotBundle(bundle, expected), /hash mismatch/);
});


posixTest('snapshot validation rejects tampering, traversal and unknown entry kinds', t => {
  const { bundle } = fixture(t);
  const snapshot = snapshotBundle(bundle, inspectBundle(bundle).sha256);
  assert.doesNotThrow(() => verifySnapshot(snapshot, snapshot.sha256));
  const changed = structuredClone(snapshot);
  changed.files[0].base64 = Buffer.from('tamper').toString('base64');
  assert.throws(() => verifySnapshot(changed, snapshot.sha256), /hash/);
  const escape = structuredClone(snapshot);
  escape.entries.at(-1).path = '../outside';
  assert.throws(() => verifySnapshot(escape, snapshot.sha256), /path/);
  const link = structuredClone(snapshot);
  link.entries.at(-1).kind = 'symlink';
  assert.throws(() => verifySnapshot(link, snapshot.sha256), /kind/);
});

posixTest('preflight delegates source reads to the descriptor-relative scanner', t => {
  const { bundle } = fixture(t);
  const original = fs.openSync;
  let openedThroughPath = false;
  fs.openSync = function (name, ...args) {
    if (typeof name === 'string' && name.endsWith('/Contents/data')) openedThroughPath = true;
    return original.call(fs, name, ...args);
  };
  try { inspectBundle(bundle); } finally { fs.openSync = original; }
  assert.equal(openedThroughPath, false);
});

posixTest('snapshot metadata modifications are rejected independently of file bytes', t => {
  const { bundle } = fixture(t);
  const snapshot = snapshotBundle(bundle, inspectBundle(bundle).sha256);
  const changed = structuredClone(snapshot);
  changed.attributes[0].values.push({ name: 'com.example.extra', base64: 'dGVzdA==' });
  assert.throws(() => verifySnapshot(changed, snapshot.sha256), /metadata/);
});

posixTest('snapshot rejects an expected metadata hash that changed after inspection', t => {
  const { bundle } = fixture(t);
  const inspected = inspectBundle(bundle);
  assert.throws(() => snapshotBundle(bundle, inspected.sha256, '0'.repeat(64)), /metadata hash mismatch/);
});

posixTest('unsafe modes and unsupported security attributes are rejected before authorisation', t => {
  const { bundle } = fixture(t);
  const snapshot = snapshotBundle(bundle, inspectBundle(bundle).sha256);
  const unsafe = structuredClone(snapshot);
  unsafe.entries[0].mode = 0o777;
  unsafe.sha256 = createHash('sha256').update(JSON.stringify(unsafe.entries)).digest('hex');
  assert.throws(() => verifySnapshot(unsafe, unsafe.sha256), /mode/);
  const attribute = structuredClone(snapshot);
  attribute.attributes[0].values.push({ name: 'com.apple.system.Security', base64: 'dGVzdA==' });
  attribute.metadataSha256 = createHash('sha256').update(JSON.stringify(attribute.attributes)).digest('hex');
  assert.throws(() => verifySnapshot(attribute, attribute.sha256), /unsupported.*attribute/);
});

posixTest('source argument whitespace is refused before authorization', t => {
  const { root, bundle } = fixture(t);
  const input = ['PRE', 'POST'].map(role => ({ role, source: bundle, sha256: inspectBundle(bundle).sha256 }));
  const manifest = input.map(({ role }) => ({ role, install_relative: `Library/Application Support/Avid/Audio/Plug-Ins/Kirin Hypha ${role}.aaxplugin` }));
  for (const suffix of [' space', '\nline', '\ttab']) {
    const alias = path.join(root, 'input' + suffix); fs.mkdirSync(alias);
    assert.throws(() => preparePlan(manifest, [{ ...input[0], source: alias }, input[1]]), /whitespace/);
  }
});
