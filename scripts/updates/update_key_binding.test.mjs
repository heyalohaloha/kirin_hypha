import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import test from 'node:test';
import { jucePublicKey } from './update_manifest.mjs';
import { validatePublicKey, updateBinding, inspectUpdateBinary, verifyUpdatePlist,
  assertPackageUpdateBinding } from './update_key_binding.mjs';

const key = jucePublicKey(crypto.generateKeyPairSync('rsa', { modulusLength: 2048, publicExponent: 65537 }).publicKey);
const digest = updateBinding(key).publicKeySha256;
function temporary(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-update-key-test-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true })); return root;
}

test('canonical empty key disables checking; zero/short/even/non-2048-bit modulus is rejected', () => {
  assert.deepEqual(updateBinding(''), { protocol: 1, publicKeySha256: '' });
  assert.equal(validatePublicKey(key), key);
  assert.equal(digest, crypto.createHash('sha256').update(key, 'utf8').digest('hex'));
  for (const invalid of [`10001,${'0'.repeat(512)}`, `10001,${'1'.repeat(512)}`,
    `10001,${'f'.repeat(511)}0`, key.toUpperCase(), ` ${key}`, key.slice(0, -1)]) {
    assert.throws(() => validatePublicKey(invalid), /RSA-2048/);
  }
});

test('CMake each configure clears stale cache; explicit approved env and validation agree', t => {
  const root = temporary(t), build = path.join(root, 'build');
  const trust = path.resolve(import.meta.dirname, '../../juce_shell/cmake/UpdateTrust.cmake').replaceAll('\\', '/');
  fs.writeFileSync(path.join(root, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.22)\nproject(UpdateTrust NONE)\ninclude("${trust}")\nfile(WRITE "\${CMAKE_BINARY_DIR}/digest.txt" "\${KIRIN_HYPHA_UPDATE_KEY_SHA256}")\n`);
  const configure = value => execFileSync('cmake', ['-S', root, '-B', build, `-DKIRIN_HYPHA_UPDATE_PUBLIC_KEY=${key}`],
    { env: { ...process.env, KIRIN_HYPHA_UPDATE_PUBLIC_KEY_INPUT: value }, stdio: 'pipe' });
  configure(key); assert.equal(fs.readFileSync(path.join(build, 'digest.txt'), 'utf8'), digest);
  for (const cleared of ['', undefined]) {
    configure(cleared); assert.equal(fs.readFileSync(path.join(build, 'digest.txt'), 'utf8'), '');
  }
  for (const invalid of [`10001,${'0'.repeat(512)}`, `10001,${'f'.repeat(511)}0`]) {
    assert.throws(() => configure(invalid), error => error.status !== 0);
  }
});

test('actual distributed non-fat bytes require matching semantic key markers', t => {
  const root = temporary(t), binary = path.join(root, 'binary');
  const write = text => fs.writeFileSync(binary, text);
  write(`slice1\0KirinHyphaUpdateKeySha256=${digest};\0slice2\0KirinHyphaUpdateKeySha256=${digest};`);
  assert.equal(inspectUpdateBinary(binary, key).publicKeySha256, digest);
  assert.throws(() => inspectUpdateBinary(binary, ''), /mismatch/);
  write(`KirinHyphaUpdateKeySha256=${digest};\0KirinHyphaUpdateKeySha256=disabled;`);
  assert.throws(() => inspectUpdateBinary(binary, key), /mismatch/);
  write('missing'); assert.throws(() => inspectUpdateBinary(binary, ''), /missing/);
  write('KirinHyphaUpdateKeySha256=disabled;'); assert.equal(inspectUpdateBinary(binary, '').publicKeySha256, '');
});

test('AU protocol1 alone cannot authorize network; every format plist binds approved digest', () => {
  const plist = { KirinHyphaUpdateKeySha256: digest, KirinHyphaUpdateProtocol: 1,
    AudioComponents: [{ resourceUsage: { 'temporary-exception.files.all.read-write': true, 'network.client': true } }] };
  verifyUpdatePlist(plist, key, true);
  assert.throws(() => verifyUpdatePlist({ ...plist, KirinHyphaUpdateKeySha256: '' }, key, true), /digest/);
  assert.throws(() => verifyUpdatePlist(plist, '', true), /digest/);
  const disabled = { KirinHyphaUpdateKeySha256: '', AudioComponents: [{ resourceUsage: { 'temporary-exception.files.all.read-write': true } }] };
  verifyUpdatePlist(disabled, '', true);
  assert.throws(() => verifyUpdatePlist({ ...disabled, KirinHyphaUpdateProtocol: 1 }, '', true), /Disabled/);
  assert.throws(() => verifyUpdatePlist({ KirinHyphaUpdateKeySha256: '' }, key), /digest/);
});

test('package provenance rejects mixed role/format keys, missing payload hashes and duplicate records', () => {
  const value = { ...updateBinding(key), binaries: ['AU/PRE', 'AU/POST'].map(id => ({
    format: id.split('/')[0], role: id.split('/')[1], binarySha256: 'a'.repeat(64), publicKeySha256: digest })) };
  assertPackageUpdateBinding(value, key, ['AU/PRE', 'AU/POST']);
  value.binaries[1].publicKeySha256 = ''; assert.throws(() => assertPackageUpdateBinding(value, key, ['AU/PRE', 'AU/POST']), /mismatch/);
  value.binaries[1] = value.binaries[0]; assert.throws(() => assertPackageUpdateBinding(value, key, ['AU/PRE', 'AU/POST']), /mismatch/);
});
