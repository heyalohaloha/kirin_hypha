import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { parseArgs as parseDryRunArgs } from './kirin_hypha_ls_dry_run.mjs';
import {
  artifactManifestFor,
  localReleaseStateFor,
  parseArgs as parseWindowsArgs,
} from './build_kirin_hypha_windows_vst3_zip.mjs';

test('LS dry run requires an explicit local state path', () => {
  assert.throws(() => parseDryRunArgs([]), /--state is required/);
  assert.equal(parseDryRunArgs(['--help']).help, true);
  assert.equal(parseDryRunArgs(['--state', 'release_state/local.state.json']).state, 'release_state/local.state.json');
});

test('Windows public manifest excludes operator workflow state', (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-release-metadata-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));

  const packageRoot = path.join(root, 'package');
  const zipPath = path.join(root, 'artifact.zip');
  const bundles = ['PRE', 'POST'].map((role) => {
    const fileName = `Kirin Hypha ${role}.vst3`;
    const binary = path.join(packageRoot, fileName, 'Contents', 'x86_64-win', fileName);
    fs.mkdirSync(path.dirname(binary), { recursive: true });
    fs.writeFileSync(binary, `${role} fixture`);
    return { label: `${role} VST3`, fileName };
  });
  fs.writeFileSync(zipPath, 'zip fixture');

  const opts = {
    releaseKind: 'ls',
    externalValidation: 'complete',
    artifactName: 'kirin-hypha-windows-vst3',
    runUrl: 'https://github.com/example/project/actions/runs/1',
    commit: '0123456789abcdef',
    bNumber: 'B-000',
  };
  const manifest = artifactManifestFor(opts, 'Kirin-Hypha-test', zipPath, packageRoot, bundles);
  const publicJson = JSON.stringify(manifest);

  assert.equal(manifest.schema, 'kirin-hypha-windows-vst3-artifact-v2');
  assert.equal(manifest.schema_version, 2);
  assert.equal(manifest.external_validation.status, 'complete');
  assert.equal(manifest.package.name, 'Kirin-Hypha-test.zip');
  assert.equal(manifest.binary_artifact.commit, opts.commit);
  assert.doesNotMatch(publicJson, /Daisuke|lsUpload|ls_upload|releaseStatus|release_status|productId|variantId/);
  assert.equal(manifest.contents.length, 2);

  const localState = localReleaseStateFor(opts, manifest);
  assert.equal(localState.lsUpload, 'ready_manual_zip_after_external_validation');
  assert.equal(localState.artifact, manifest);

  const pendingOpts = { ...opts, externalValidation: 'pending' };
  const pendingManifest = artifactManifestFor(
    pendingOpts,
    'Kirin-Hypha-test-pending',
    zipPath,
    packageRoot,
    bundles,
  );
  const pendingState = localReleaseStateFor(pendingOpts, pendingManifest);
  assert.equal(pendingState.lsUpload, 'blocker_pending_external_validation');
  assert.equal(pendingState.artifact.external_validation.status, 'pending');
  assert.throws(
    () => localReleaseStateFor(opts, pendingManifest),
    /artifact validation does not match release state/,
  );
  assert.throws(
    () => localReleaseStateFor({ ...opts, releaseKind: 'beta' }, manifest),
    /artifact purpose does not match release kind/,
  );
  assert.throws(
    () => localReleaseStateFor(
      { ...opts, externalValidation: 'unknown' },
      { ...manifest, external_validation: { status: 'unknown' } },
    ),
    /unsupported artifact validation state/,
  );
});

test('Windows package arguments reject unsupported validation states', () => {
  assert.throws(
    () => parseWindowsArgs(['--external-validation', 'reported_complete_by_daisuke']),
    /must be complete or pending/,
  );
});
