import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { assertRawSourceUnchanged, rawSourceSnapshot, inspectNativeLinkMap,
  assertPluginvalQualification, PLUGINVAL_SHA256 } from './unsigned_qualification.mjs';

test('raw source is bound to committed blobs and the approved patched JUCE tree', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'raw-juce-fixture-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const juce = path.join(root, 'juce_shell/JUCE');
  fs.mkdirSync(juce, { recursive: true });
  const git = (directory, args) => execFileSync('git', args, { cwd: directory, stdio: 'pipe', encoding: 'utf8' }).trim();
  for (const directory of [root, juce]) {
    git(directory, ['init']); git(directory, ['config', 'user.name', 'Fixture']);
    git(directory, ['config', 'user.email', 'fixture@example.invalid']);
    fs.writeFileSync(path.join(directory, 'source'), 'original\n');
    git(directory, ['add', 'source']); git(directory, ['commit', '-m', 'fixture source']);
  }
  fs.mkdirSync(path.join(root, 'scripts')); fs.mkdirSync(path.join(root, 'juce_shell/patches'));
  fs.writeFileSync(path.join(juce, 'source'), 'patched\n');
  const patch = git(juce, ['diff', '--no-ext-diff']);
  fs.writeFileSync(path.join(root, 'juce_shell/patches/0001-fixture.patch'), patch + '\n');
  fs.writeFileSync(path.join(root, 'scripts/verify_juce_patch_state.sh'), 'PATCHES=(\n  "0001-fixture.patch::"\n)\n');
  git(root, ['add', '.']); git(root, ['commit', '-m', 'pin approved patch']);
  const indexBefore = git(juce, ['ls-files', '--stage']);
  const before = rawSourceSnapshot(root);
  assert.equal(before.commit, git(root, ['rev-parse', 'HEAD']));
  assert.equal(before.files.find(f => f.path === 'source').blob, git(root, ['hash-object', '--no-filters', 'source']));
  assertRawSourceUnchanged(before, rawSourceSnapshot(root));
  assert.equal(git(juce, ['ls-files', '--stage']), indexBefore);
  for (const [directory, bytes] of [[juce, 'patched\r\n'], [juce, 'original\n'], [root, 'original\r\n']]) {
    fs.writeFileSync(path.join(directory, 'source'), bytes);
    assert.throws(() => rawSourceSnapshot(root), /canonical Git blob/);
    fs.writeFileSync(path.join(directory, 'source'), directory === juce ? 'patched\n' : 'original\n');
  }
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
