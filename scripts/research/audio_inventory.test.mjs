import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, symlink, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { inventory } from './audio_inventory.mjs';

test('inventory reads only metadata and does not silently label files as independent songs', async () => {
  const root = await mkdtemp(path.join(os.tmpdir(), 'hypha-inventory-test-'));
  try {
    await writeFile(path.join(root, 'not actually audio.WAV'), '123');
    await writeFile(path.join(root, '._audio.wav'), 'resource fork');
    await mkdir(path.join(root, 'node_modules'));
    await writeFile(path.join(root, 'node_modules', 'ignored.wav'), 'x');
    await symlink(path.join(root, 'not actually audio.WAV'), path.join(root, 'link.wav'));
    const report = await inventory([root, root]);
    assert.equal(report.files, 1);
    assert.equal(report.bytes, 3);
    assert.equal(report.payload_read, false);
    assert.equal(report.entries[0].song_identity, null);
    assert.equal(report.entries[0].assignment, 'unassigned');
    assert.equal(report.by_extension['.wav'], 1);
    assert.deepEqual(report.failures, []);
  } finally { await rm(root, { recursive: true, force: true }); }
});

test('missing source root fails, not an empty successful corpus', async () => {
  await assert.rejects(inventory(['/a-nonexistent-hypha-research-input']));
});
