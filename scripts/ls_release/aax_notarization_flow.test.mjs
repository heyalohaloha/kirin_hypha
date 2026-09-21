import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  AAX_NOTARIZATION_RECEIPT_NAME,
  submitMacAaxForNotarization,
  verifyMacAaxNotarizationReceipt,
} from './aax_notarization_receipt.mjs';
import {
  AAX_APPLE_AUTHORITY,
  AAX_APPLE_TEAM_ID,
  AAX_PACE_PUBLISHER_ID,
  AAX_PACE_SIGNER_NAME,
} from './aax_bundle_verify.mjs';
import { buildTreeManifest, sha256File } from './aax_submission_archive.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const jobId = '12345678-1234-1234-1234-123456789abc';

function git(root, ...args) {
  const result = spawnSync('git', [
    '-c', 'user.name=AAX Flow Test',
    '-c', 'user.email=test@example.invalid',
    '-c', 'commit.gpgsign=false',
    '-C', root, ...args,
  ], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stdout + result.stderr);
}

function treeHash(bundlePath) {
  const manifest = buildTreeManifest(bundlePath);
  return crypto.createHash('sha256').update(JSON.stringify(manifest)).digest('hex');
}

function fixtureBundleVerifier({ bundlePath, executableName }) {
  return {
    binarySha256: sha256File(path.join(bundlePath, 'Contents/MacOS', executableName)),
    treeManifestSha256: treeHash(bundlePath),
    apple: {
      cdhash: crypto.createHash('sha1').update(executableName).digest('hex'),
      authority: AAX_APPLE_AUTHORITY,
      teamId: AAX_APPLE_TEAM_ID,
    },
    pace: {
      signerName: AAX_PACE_SIGNER_NAME,
      signerGuid: '991BD2C1-1D2D-A27D-819E-E60035AB5695',
      publisherId: AAX_PACE_PUBLISHER_ID,
    },
  };
}

function fixtureBundleCopyVerifier({ sourcePath, destinationPath, spec }) {
  assert.deepEqual(
    buildTreeManifest(destinationPath, path.basename(destinationPath)),
    buildTreeManifest(sourcePath, path.basename(sourcePath)),
  );
  return fixtureBundleVerifier({
    bundlePath: destinationPath,
    executableName: spec.executable_name,
  });
}

function createCommandRunner(store) {
  const archives = new Map();
  const commands = [];
  let acceptedName = '';
  let acceptedHash = '';
  let onlineHashOverride = '';
  const copyTree = (source, destination) => {
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    fs.cpSync(source, destination, { recursive: true, preserveTimestamps: true });
  };
  const runner = (command, args) => {
    commands.push([command, ...args]);
    if (command === 'ditto' && args[0] === '-c') {
      const source = args.at(-2);
      const destination = args.at(-1);
      const manifest = buildTreeManifest(source);
      const bytes = Buffer.from(`${JSON.stringify(manifest)}\n`);
      fs.writeFileSync(destination, bytes);
      const snapshot = path.join(store, crypto.createHash('sha256').update(bytes).digest('hex'));
      copyTree(source, snapshot);
      archives.set(sha256File(destination), { snapshot, rootName: path.basename(source) });
      return { stdout: '', stderr: '' };
    }
    if (command === 'ditto' && args[0] === '-x') {
      const archive = args[2];
      const destination = args[3];
      const source = archives.get(sha256File(archive));
      assert.ok(source, 'fixture archive was not created by the command runner');
      copyTree(source.snapshot, path.join(destination, source.rootName));
      return { stdout: '', stderr: '' };
    }
    if (command === 'ditto') {
      copyTree(args[0], args[1]);
      return { stdout: '', stderr: '' };
    }
    assert.equal(command, 'xcrun');
    assert.equal(args[0], 'notarytool');
    if (args[1] === 'submit') {
      acceptedName = path.basename(args[2]);
      acceptedHash = sha256File(args[2]);
      return {
        stdout: JSON.stringify({ id: jobId, status: 'Accepted', name: acceptedName }),
        stderr: '',
      };
    }
    if (args[1] === 'info') {
      return {
        stdout: JSON.stringify({ id: jobId, status: 'Accepted', name: acceptedName }),
        stderr: '',
      };
    }
    if (args[1] === 'log') {
      fs.writeFileSync(args[3], `${JSON.stringify({
        archiveFilename: acceptedName,
        issues: null,
        jobId,
        logFormatVersion: 1,
        sha256: onlineHashOverride || acceptedHash,
        status: 'Accepted',
      })}\n`);
      return { stdout: '', stderr: '' };
    }
    throw new Error(`unexpected fixture command: ${command} ${args.join(' ')}`);
  };
  return {
    commands,
    runner,
    setOnlineHash(value) { onlineHashOverride = value; },
  };
}

test('AAX submit, receipt, offline/online verification, and package inputs share exact bytes', {
  skip: process.platform !== 'darwin',
}, (context) => {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-aax-flow-'));
  context.after(() => fs.rmSync(fixture, { recursive: true, force: true }));
  const root = path.join(fixture, 'repo');
  const artifacts = path.join(root, 'build-aax-universal');
  const runnerStore = path.join(fixture, 'runner-store');
  fs.mkdirSync(path.join(root, 'config'), { recursive: true });
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.mkdirSync(runnerStore, { recursive: true });
  fs.copyFileSync(
    path.join(repoRoot, 'config/hypha_macos_aax_bundles.json'),
    path.join(root, 'config/hypha_macos_aax_bundles.json'),
  );
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), '[package]\nversion = "1.1.50"\n');
  for (const role of ['PRE', 'POST']) {
    const name = `Kirin Hypha ${role}`;
    const bundle = path.join(
      artifacts,
      `KirinHypha${role}_artefacts/Release/AAX/${name}.aaxplugin`,
    );
    fs.mkdirSync(path.join(bundle, 'Contents/MacOS'), { recursive: true });
    fs.mkdirSync(path.join(bundle, 'Contents/Resources'), { recursive: true });
    fs.writeFileSync(path.join(bundle, 'Contents/MacOS', name), `${role} signed fixture\n`);
    fs.writeFileSync(path.join(bundle, 'Contents/Resources/Kimera.ttf'), `${role} Kimera fixture\n`);
  }
  git(root, 'init', '-q');
  git(root, 'add', '.');
  git(root, 'commit', '-qm', '[B-990] AAX flow fixture');

  const commands = createCommandRunner(runnerStore);
  const injected = {
    root,
    artifactDir: artifacts,
    commandRunner: commands.runner,
    bundleVerifier: fixtureBundleVerifier,
    bundleCopyVerifier: fixtureBundleCopyVerifier,
    sourceVerifier: () => ({
      commit: '0'.repeat(40),
      shortCommit: '0'.repeat(12),
      bNumber: 'B-990',
      sourceState: 'clean source',
      dirtyEntries: [],
    }),
  };
  const submitted = submitMacAaxForNotarization({
    ...injected,
    keychainProfile: 'fixture-profile',
  });
  assert.equal(path.basename(submitted.receiptPath), AAX_NOTARIZATION_RECEIPT_NAME);
  assert.deepEqual(
    commands.commands.filter((entry) => entry[0] === 'xcrun').map((entry) => entry[2]),
    ['submit', 'info', 'log'],
  );

  const pkgInput = path.join(fixture, 'pkg-payload');
  const zipInput = path.join(fixture, 'zip-payload');
  fs.mkdirSync(pkgInput);
  for (const role of ['PRE', 'POST']) {
    const name = `Kirin Hypha ${role}.aaxplugin`;
    fs.cpSync(
      path.join(artifacts, `KirinHypha${role}_artefacts/Release/AAX`, name),
      path.join(pkgInput, name),
      { recursive: true, preserveTimestamps: true },
    );
  }
  assert.doesNotThrow(() => verifyMacAaxNotarizationReceipt({
    ...injected,
    materializeDir: zipInput,
    payloadDir: pkgInput,
  }));
  for (const role of ['PRE', 'POST']) {
    const name = `Kirin Hypha ${role}.aaxplugin`;
    assert.equal(treeHash(path.join(pkgInput, name)), treeHash(path.join(zipInput, name)));
  }
  assert.doesNotThrow(() => verifyMacAaxNotarizationReceipt({
    ...injected,
    online: true,
    keychainProfile: 'fixture-profile',
  }));

  const receipt = JSON.parse(fs.readFileSync(submitted.receiptPath, 'utf8'));
  const archive = path.join(artifacts, ...receipt.archive.relative_path.split('/'));
  const originalArchive = fs.readFileSync(archive);
  fs.writeFileSync(archive, 'same name, different ZIP bytes\n');
  const tamperedOutput = path.join(fixture, 'tampered-output');
  assert.throws(
    () => verifyMacAaxNotarizationReceipt({ ...injected, materializeDir: tamperedOutput }),
    /archive bytes do not match/,
  );
  assert.equal(fs.existsSync(tamperedOutput), false);
  fs.writeFileSync(archive, originalArchive);

  commands.setOnlineHash('f'.repeat(64));
  const mismatchedOutput = path.join(fixture, 'online-mismatch-output');
  assert.throws(
    () => verifyMacAaxNotarizationReceipt({
      ...injected,
      online: true,
      keychainProfile: 'fixture-profile',
      materializeDir: mismatchedOutput,
    }),
    /online AAX notary log does not match/,
  );
  assert.equal(fs.existsSync(mismatchedOutput), false);
});
