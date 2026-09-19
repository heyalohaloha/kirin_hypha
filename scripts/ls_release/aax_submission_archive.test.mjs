import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  assertTreeMatchesManifest,
  buildTreeManifest,
  evidenceRecord,
  extractAndVerifyArchive,
  materializeVerifiedTree,
  parseNotarytoolLog,
  requireBundleRoots,
  runEvidenceCommand,
  validateNotaryEvidenceLinks,
  verifyEvidenceFile,
} from './aax_submission_archive.mjs';

test('AAX submission archive evidence rejects every payload and notary relinking class', (context) => {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-aax-evidence-'));
  context.after(() => fs.rmSync(fixture, { recursive: true, force: true }));
  const payload = path.join(fixture, 'Kirin Hypha 1.1.49 AAX');
  const pre = path.join(payload, 'Kirin Hypha PRE.aaxplugin');
  const post = path.join(payload, 'Kirin Hypha POST.aaxplugin');
  const preBinary = path.join(pre, 'Contents/MacOS/Kirin Hypha PRE');
  const postBinary = path.join(post, 'Contents/MacOS/Kirin Hypha POST');
  const resource = path.join(pre, 'Contents/Resources/preset.txt');
  fs.mkdirSync(path.dirname(preBinary), { recursive: true });
  fs.mkdirSync(path.dirname(postBinary), { recursive: true });
  fs.mkdirSync(path.dirname(resource), { recursive: true });
  fs.writeFileSync(preBinary, 'universal PRE fixture\n');
  fs.writeFileSync(postBinary, 'universal POST fixture\n');
  fs.writeFileSync(resource, 'resource fixture\n');
  fs.symlinkSync('preset.txt', path.join(path.dirname(resource), 'current.txt'));

  const manifest = buildTreeManifest(payload);
  assert.doesNotThrow(() => requireBundleRoots(manifest, [
    'Kirin Hypha PRE.aaxplugin',
    'Kirin Hypha POST.aaxplugin',
  ]));
  assert.doesNotThrow(() => assertTreeMatchesManifest(payload, manifest));

  fs.writeFileSync(postBinary, 'arm64-only replacement fixture\n');
  assert.throws(() => assertTreeMatchesManifest(payload, manifest), /does not match/);
  fs.writeFileSync(postBinary, 'universal POST fixture\n');
  fs.writeFileSync(resource, 'changed resource fixture\n');
  assert.throws(() => assertTreeMatchesManifest(payload, manifest), /does not match/);
  fs.writeFileSync(resource, 'resource fixture\n');
  fs.unlinkSync(path.join(path.dirname(resource), 'current.txt'));
  fs.symlinkSync('../MacOS/Kirin Hypha PRE', path.join(path.dirname(resource), 'current.txt'));
  assert.throws(() => assertTreeMatchesManifest(payload, manifest), /does not match/);
  fs.unlinkSync(path.join(path.dirname(resource), 'current.txt'));
  fs.symlinkSync('preset.txt', path.join(path.dirname(resource), 'current.txt'));
  fs.chmodSync(resource, 0o600);
  assert.throws(() => assertTreeMatchesManifest(payload, manifest), /does not match/);
  fs.chmodSync(resource, 0o644);
  assert.doesNotThrow(() => assertTreeMatchesManifest(payload, manifest));

  const missingPre = structuredClone(manifest);
  missingPre.entries = missingPre.entries.filter(
    (entry) => !entry.path.startsWith('Kirin Hypha PRE.aaxplugin'),
  );
  assert.throws(() => requireBundleRoots(missingPre, [
    'Kirin Hypha PRE.aaxplugin',
    'Kirin Hypha POST.aaxplugin',
  ]), /must contain exactly/);

  const archive = path.join(fixture, 'Kirin-Hypha-1.1.49-macOS-AAX.zip');
  fs.writeFileSync(archive, 'first archive bytes\n');
  const archiveRecord = evidenceRecord(archive, fixture);
  fs.writeFileSync(archive, 'same name, different archive bytes\n');
  assert.throws(() => verifyEvidenceFile(archiveRecord, fixture, 'fixture archive'), /do not match/);

  const logJson = {
    archiveFilename: 'Kirin-Hypha-1.1.49-macOS-AAX.zip',
    issues: [],
    jobId: '12345678-1234-1234-1234-123456789abc',
    logFormatVersion: 1,
    status: 'Accepted',
  };
  const log = parseNotarytoolLog(JSON.stringify(logJson));
  const receipt = {
    submission: { id: log.job_id, status: 'Accepted', name: log.archive_filename },
    archive: { file_name: log.archive_filename, root_name: manifest.root_name },
  };
  assert.doesNotThrow(() => validateNotaryEvidenceLinks(receipt, manifest, log));
  assert.throws(
    () => validateNotaryEvidenceLinks({
      ...receipt,
      submission: { ...receipt.submission, id: 'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa' },
    }, manifest, log),
    /job ID/,
  );
  assert.throws(() => parseNotarytoolLog('{broken json'), /valid JSON/);
  assert.throws(
    () => parseNotarytoolLog(JSON.stringify({ ...logJson, status: 'Invalid' })),
    /expected Accepted/,
  );
  assert.throws(
    () => runEvidenceCommand('/definitely/missing/notarytool', []),
    /could not start/,
  );

  fs.writeFileSync(resource, 'payload replaced after verification\n');
  assert.throws(() => assertTreeMatchesManifest(payload, manifest), /does not match/);
});

test('verified AAX payload is materialized from the preserved archive bytes', {
  skip: process.platform !== 'darwin',
}, (context) => {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-aax-materialize-'));
  context.after(() => fs.rmSync(fixture, { recursive: true, force: true }));
  const payload = path.join(fixture, 'Kirin Hypha 1.1.49 AAX');
  for (const role of ['PRE', 'POST']) {
    const bundle = path.join(payload, `Kirin Hypha ${role}.aaxplugin`);
    fs.mkdirSync(path.join(bundle, 'Contents/MacOS'), { recursive: true });
    fs.writeFileSync(path.join(bundle, 'Contents/MacOS', `Kirin Hypha ${role}`), `${role}\n`);
  }
  const manifest = buildTreeManifest(payload);
  const archive = path.join(fixture, 'Kirin-Hypha-1.1.49-macOS-AAX.zip');
  runEvidenceCommand('ditto', [
    '-c', '-k', '--norsrc', '--noqtn', '--keepParent', payload, archive,
  ]);
  const commands = [];
  const runner = (command, args, options) => {
    commands.push([command, ...args]);
    return runEvidenceCommand(command, args, options);
  };
  const extracted = extractAndVerifyArchive({
    archivePath: archive,
    manifest,
    destinationParent: path.join(fixture, 'extract'),
    runner,
  });
  const materialized = path.join(fixture, 'materialized');
  materializeVerifiedTree(extracted, materialized, manifest, runner);
  assert.deepEqual(commands.map((command) => command.slice(0, 3)), [
    ['ditto', '-x', '-k'],
    ['ditto', extracted, materialized],
  ]);
  assert.doesNotThrow(() => assertTreeMatchesManifest(materialized, manifest));
  fs.writeFileSync(
    path.join(materialized, 'Kirin Hypha POST.aaxplugin/Contents/MacOS/Kirin Hypha POST'),
    'swapped after materialization\n',
  );
  assert.throws(() => assertTreeMatchesManifest(materialized, manifest), /does not match/);
});
