import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { parseArgs as parseDryRunArgs } from './kirin_hypha_ls_dry_run.mjs';
import {
  artifactManifestFor,
  localReleaseStateFor,
  parseArgs as parseWindowsArgs,
} from './build_kirin_hypha_windows_vst3_zip.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');

test('published release commit rewrites retain an explicit immutable provenance map', () => {
  const mapPath = path.join(repoRoot, 'docs', 'release_commit_map.json');
  const provenance = JSON.parse(fs.readFileSync(mapPath, 'utf8'));

  assert.equal(provenance.schema, 'kirin-hypha-release-commit-map-v1');
  assert.equal(provenance.schema_version, 1);
  assert.ok(Array.isArray(provenance.releases));
  assert.ok(provenance.releases.length > 0);

  const releaseKeys = new Set();
  for (const release of provenance.releases) {
    assert.match(release.tag, /^v\d+\.\d+\.\d+$/);
    assert.match(release.build, /^B-\d+$/);
    assert.match(release.published_artifact_commit, /^[0-9a-f]{40}$/);
    assert.match(release.current_public_commit, /^[0-9a-f]{40}$/);
    assert.notEqual(release.published_artifact_commit, release.current_public_commit);
    assert.equal(release.rewrite.removed_path, 'release_state/');
    assert.equal(release.verification.result, 'tree-identical');
    assert.match(release.verification.scope, /except release_state\//);

    const key = `${release.tag}:${release.build}`;
    assert.equal(releaseKeys.has(key), false, `duplicate release provenance entry: ${key}`);
    releaseKeys.add(key);
  }

  assert.deepEqual(
    provenance.releases.find((release) => release.tag === 'v1.1.34'),
    {
      tag: 'v1.1.34',
      build: 'B-444',
      published_artifact_commit: 'ec2121e97789bf3859f72623a1053a0f506251ee',
      current_public_commit: 'eb99f1ad17e54dc32b8ad959e1798193b6f39d1b',
      rewrite: {
        date: '2026-07-23',
        reason: 'Removed release-operator state from public Git history.',
        removed_path: 'release_state/',
      },
      verification: {
        scope: 'Every tracked path except release_state/',
        result: 'tree-identical',
        verified_at: '2026-07-23',
      },
    },
  );
});

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
