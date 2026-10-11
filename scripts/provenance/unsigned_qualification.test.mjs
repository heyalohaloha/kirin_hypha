import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { assertRawSourceUnchanged, rawSourceSnapshot, inspectNativeLinkMap,
  assertPluginvalQualification, PLUGINVAL_SHA256 } from './unsigned_qualification.mjs';

test('raw source qualification detects line-ending changes hidden by logical patch comparison', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'raw-juce-fixture-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const juce = path.join(root, 'juce_shell/JUCE');
  fs.mkdirSync(juce, { recursive: true });
  for (const directory of [root, juce]) {
    execFileSync('git', ['init'], { cwd: directory, stdio: 'ignore' });
    fs.writeFileSync(path.join(directory, 'source'), 'original\r\n');
    execFileSync('git', ['add', 'source'], { cwd: directory });
  }
  const before = rawSourceSnapshot(root);
  assertRawSourceUnchanged(before, rawSourceSnapshot(root));
  fs.writeFileSync(path.join(juce, 'source'), 'original\n');
  assert.throws(() => assertRawSourceUnchanged(before, rawSourceSnapshot(root)), /Raw source bytes changed/);
});

test('native linker evidence requires all enabled codec families, rather than source mentions', () => {
  const map = 'Publics by Value\n FLAC__read ogg_stream_init vorbis_info_init jpeg_read png_read inflateEnd';
  assert.equal(inspectNativeLinkMap(map).length, 6);
  for (const name of ['FLAC__read', 'ogg_stream_init', 'vorbis_info_init', 'jpeg_read', 'png_read', 'inflateEnd']) {
    assert.throws(() => inspectNativeLinkMap(map.replace(name, 'absent')), /evidence missing/);
  }
  assert.throws(() => inspectNativeLinkMap('source contains FLAC__read'), /actual MSVC/);
});

test('plugin qualification is bound to both complete bundle inventories and exact tool bytes', () => {
  const binaries = [{ role: 'PRE' }, { role: 'POST' }];
  const files = [{ path: 'Contents/payload', sha256: 'a'.repeat(64) }];
  const record = { schema: 'hypha-pluginval-qualification-v1', toolSha256: PLUGINVAL_SHA256,
    results: binaries.map(b => ({ bundle: `Kirin Hypha ${b.role}.vst3`, exitCode: 0 })), before: [files, files], after: [files, files] };
  assertPluginvalQualification(record, binaries, () => [...files]);
  assert.throws(() => assertPluginvalQualification(record, binaries, () => [...files, { path: 'added', sha256: 'b'.repeat(64) }]), /Payload differs/);
  assert.throws(() => assertPluginvalQualification({ ...record, toolSha256: '0'.repeat(64) }, binaries, () => files), /incomplete/);
  const failed = structuredClone(record); failed.results[1].exitCode = 7;
  assert.throws(() => assertPluginvalQualification(failed, binaries, () => files), /incomplete/);
  const changed = JSON.parse(JSON.stringify(record)); changed.after[0][0].sha256 = 'b'.repeat(64);
  assert.throws(() => assertPluginvalQualification(changed, binaries, () => files), /changed/);
});
