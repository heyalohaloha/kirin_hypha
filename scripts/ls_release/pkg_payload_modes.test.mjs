import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { containerDirectories, normalizeContainerDirectories, verifyContainerDirectories } from './pkg_payload_modes.mjs';

test('PKG container directories become 755 under an owner-only umask; bundle contents stay untouched', {
  skip: process.platform === 'win32',
}, t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-pkg-modes-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const payload = path.join(root, 'payload');
  const legal = 'Library/Application Support/Kirin Hypha/Legal';
  const bundle = 'Library/Audio/Plug-Ins/Components/Kirin Hypha PRE.component';
  const previous = process.umask(0o077);
  try {
    fs.mkdirSync(path.join(payload, legal), { recursive: true });
    fs.mkdirSync(path.join(payload, bundle, 'Contents'), { recursive: true });
  } finally { process.umask(previous); }
  fs.chmodSync(path.join(payload, bundle, 'Contents'), 0o700);
  assert.throws(() => verifyContainerDirectories(payload, [legal, bundle]), /not 755/);
  normalizeContainerDirectories(payload, [legal, bundle]);
  assert.deepEqual(containerDirectories(payload, [legal, bundle]).map(d => path.relative(payload, d)), ['',
    'Library', 'Library/Application Support', 'Library/Application Support/Kirin Hypha',
    'Library/Audio', 'Library/Audio/Plug-Ins', 'Library/Audio/Plug-Ins/Components']);
  assert.equal(fs.statSync(path.join(payload, bundle, 'Contents')).mode & 0o777, 0o700, 'bundle contents unchanged');
  for (const mode of [0o775, 0o777, 0o750]) {
    fs.chmodSync(path.join(payload, 'Library/Application Support/Kirin Hypha'), mode);
    assert.throws(() => verifyContainerDirectories(payload, [legal, bundle]), /not 755/, mode.toString(8));
  }
  assert.throws(() => containerDirectories(payload, ['../outside']), /outside/);
});
