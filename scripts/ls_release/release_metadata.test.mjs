import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { parseArgs as parseDryRunArgs } from './kirin_hypha_ls_dry_run.mjs';
import {
  parseArgs as parseReleaseSetArgs,
  requireWindowsInstaller,
} from './build_kirin_hypha_release_set.mjs';
import {
  artifactManifestFor,
  commitReceiptFor,
  localReleaseStateFor,
  parseArgs as parseWindowsArgs,
} from './build_kirin_hypha_windows_vst3_zip.mjs';
import { loadMacShipBundleManifest } from './kirin_hypha_ship_bundles.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');

function git(args, options = {}) {
  return execFileSync('git', args, { cwd: repoRoot, ...options });
}

function assertExactKeys(value, expected, label) {
  assert.deepEqual(Object.keys(value).sort(), [...expected].sort(), `${label} keys`);
}

function sha256File(filePath) {
  return createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

function filteredTreeDigest(commit, excludedPathPrefix) {
  const raw = git(['ls-tree', '-r', '-z', '--full-tree', commit]);
  const hash = createHash('sha256');
  let entryCount = 0;
  let recordStart = 0;

  for (let cursor = 0; cursor < raw.length; cursor += 1) {
    if (raw[cursor] !== 0) continue;
    const record = raw.subarray(recordStart, cursor);
    recordStart = cursor + 1;
    const tab = record.indexOf(0x09);
    assert.notEqual(tab, -1, 'git ls-tree record must contain a tab-delimited path');
    const recordPath = record.subarray(tab + 1).toString('utf8');
    if (recordPath.startsWith(excludedPathPrefix)) continue;
    hash.update(record);
    hash.update(Buffer.from([0]));
    entryCount += 1;
  }

  assert.equal(recordStart, raw.length, 'git ls-tree output must end with NUL');
  return { entryCount, value: hash.digest('hex') };
}

function verifyReleaseStateBoundary({ trackedPathBytes, ignoreProbePasses, publicHistory }) {
  if (trackedPathBytes !== 0) {
    throw new Error('release_state is tracked in the current source tree');
  }
  if (!ignoreProbePasses) {
    throw new Error('release_state ignore rule is missing');
  }
  if (publicHistory.trim() !== '') {
    throw new Error('release_state is reachable from public source history');
  }
}

test('release_state stays ignored and absent from public source history', () => {
  const trackedPathBytes = git(['ls-files', '-z', '--', 'release_state']).length;
  let ignoreProbePasses = true;
  try {
    git(['check-ignore', '-q', 'release_state/.contract-probe']);
  } catch {
    ignoreProbePasses = false;
  }

  // Deliberately exclude local refs/codex/* checkpoint trees and refs/stash.
  // HEAD, fetched origin branches, and tags are the source surfaces published by GitHub.
  const publicHistory = git(
    [
      'rev-list',
      '--full-history',
      'HEAD',
      '--tags',
      '--remotes=origin',
      '--',
      'release_state/',
    ],
    { encoding: 'utf8' },
  );
  verifyReleaseStateBoundary({ trackedPathBytes, ignoreProbePasses, publicHistory });
});

test('macOS ship manifest separates installed VST3 names from declared executables', () => {
  const manifest = loadMacShipBundleManifest({ root: repoRoot });
  assert.equal(manifest.bundles.length, 4);
  const vst3 = manifest.bundles.filter((bundle) => bundle.kind === 'vst3');
  assert.deepEqual(
    vst3.map((bundle) => path.basename(bundle.install_relative)),
    ['PRE Kirin Hypha.vst3', 'POST Kirin Hypha.vst3'],
  );
  assert.deepEqual(
    vst3.map((bundle) => bundle.executable_name),
    ['Kirin Hypha PRE', 'Kirin Hypha POST'],
  );
  assert.ok(vst3.every((bundle) => bundle.sourcePath.startsWith(manifest.defaultBuildRoot)));
});

test('macOS ship manifest rejects a preinstall path outside approved plug-in folders', (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-ship-manifest-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const source = JSON.parse(fs.readFileSync(
    path.join(repoRoot, 'config/hypha_macos_ship_bundles.json'),
    'utf8',
  ));
  source.bundles[2].install_relative = '../../Library/unsafe/PRE Kirin Hypha.vst3';
  fs.mkdirSync(path.join(root, 'config'), { recursive: true });
  fs.writeFileSync(
    path.join(root, 'config/hypha_macos_ship_bundles.json'),
    `${JSON.stringify(source)}\n`,
  );

  assert.throws(
    () => loadMacShipBundleManifest({ root }),
    /must be a safe POSIX relative path/,
  );
});

test('release_state boundary rejects each reinsertion path', () => {
  const clean = { trackedPathBytes: 0, ignoreProbePasses: true, publicHistory: '' };
  assert.throws(
    () => verifyReleaseStateBoundary({ ...clean, trackedPathBytes: 1 }),
    /tracked in the current source tree/,
  );
  assert.throws(
    () => verifyReleaseStateBoundary({ ...clean, ignoreProbePasses: false }),
    /ignore rule is missing/,
  );
  assert.throws(
    () => verifyReleaseStateBoundary({ ...clean, publicHistory: '0123456789abcdef\n' }),
    /reachable from public source history/,
  );
});

test('README uses the verified current Hypha UI media', () => {
  const readme = fs.readFileSync(path.join(repoRoot, 'README.md'), 'utf8');
  const media = [
    {
      relativePath: 'docs/media/kirin-hypha-freq.jpg',
      sha256: '07ead3f12501b601e3e098d48f81bee44faf1a55d8681548ac63ac93cefb7c60',
      signature: Buffer.from([0xff, 0xd8, 0xff]),
    },
    {
      relativePath: 'docs/media/kirin-hypha-freq-demo.mp4',
      sha256: 'e149441b4c8bbbcd908c9bda53fc56c0ef106962abadab0531f39d5d9b77194d',
      signature: Buffer.from('ftyp'),
      signatureOffset: 4,
    },
    {
      relativePath: 'docs/media/kirin-hypha-sharp.jpg',
      sha256: '5ffa99bc02eca335b95643c11df07e84e369abb54024f32a8108857d5a12c2d2',
      signature: Buffer.from([0xff, 0xd8, 0xff]),
    },
    {
      relativePath: 'docs/media/kirin-hypha-live.jpg',
      sha256: '9b200ff12454b176e213f7ea5dfa31d75c23a7ce59a29fe5d94cf776311d5cb6',
      signature: Buffer.from([0xff, 0xd8, 0xff]),
    },
    {
      relativePath: 'docs/media/kirin-hypha-pre-post.jpg',
      sha256: 'c38f068db726a4172850a0cd4099a7dcf116fe1ef29f123eda647612217c5416',
      signature: Buffer.from([0xff, 0xd8, 0xff]),
    },
    {
      relativePath: 'docs/media/kirin-hypha-pre-post-demo.mp4',
      sha256: '7d1ce8ad1f8fab4e6245c08fb25a1b5b733dd40c5e1f876d7a1527b5fef0bc13',
      signature: Buffer.from('ftyp'),
      signatureOffset: 4,
    },
    {
      relativePath: 'docs/media/kirin-hypha-record-keep-demo.mp4',
      sha256: '24db4c35289ce82678d476017fb509f63d52435d95e79fdacaadfd3c559b77eb',
      signature: Buffer.from('ftyp'),
      signatureOffset: 4,
    },
  ];

  for (const asset of media) {
    const assetPath = path.join(repoRoot, asset.relativePath);
    assert.match(readme, new RegExp(asset.relativePath.replaceAll('.', '\\.')));
    assert.ok(fs.statSync(assetPath).size > 10_000, `${asset.relativePath} must not be empty`);
    assert.equal(sha256File(assetPath), asset.sha256, `${asset.relativePath} digest`);
    const contents = fs.readFileSync(assetPath);
    const offset = asset.signatureOffset ?? 0;
    assert.deepEqual(
      contents.subarray(offset, offset + asset.signature.length),
      asset.signature,
      `${asset.relativePath} signature`,
    );
  }

  for (const retired of [
    'docs/images/hypha_record_mode.jpg',
    'docs/images/hypha_watch_mode.jpg',
  ]) {
    assert.equal(fs.existsSync(path.join(repoRoot, retired)), false, `${retired} must stay retired`);
    assert.doesNotMatch(readme, new RegExp(retired.replaceAll('.', '\\.')));
  }
});

test('README opens with current analysis, exact pairing, and supported Windows facts', () => {
  const readme = fs.readFileSync(path.join(repoRoot, 'README.md'), 'utf8');
  const designIndex = readme.indexOf('\n## Design\n');
  const entrance = readme.slice(0, designIndex);

  assert.ok(designIndex > 0, 'README must keep the technical Design contract after the entrance');
  assert.match(entrance, /docs\/media\/kirin-hypha-freq\.jpg/);
  assert.match(entrance, /docs\/media\/kirin-hypha-freq-demo\.mp4/);
  assert.match(entrance, /docs\/media\/kirin-hypha-sharp\.jpg/);
  assert.match(entrance, /docs\/media\/kirin-hypha-live\.jpg/);
  assert.ok(
    entrance.indexOf('docs/media/kirin-hypha-freq.jpg')
      < readme.indexOf('docs/media/kirin-hypha-pre-post.jpg'),
    'FREQ must be the first public product image',
  );
  assert.match(entrance, /choose that exact PRE under \*\*Pair choices\*\*/);
  assert.match(entrance, /Names are optional labels/);
  assert.match(readme, /current v1\.1\.48 Windows 10\/11 64-bit VST3 release is a supported manual PRE\/POST ZIP/);
  assert.match(readme, /The next validated release will use a single installer\s+EXE/);
  assert.match(readme, /signed installer is required for the next validated release/);
  assert.doesNotMatch(readme, /supported release is currently macOS-only/);
  assert.doesNotMatch(readme, /Windows validation candidate/);
  assert.doesNotMatch(readme, /External validation is pending/);
  assert.doesNotMatch(readme, /Pairing is by \*\*name\*\*/);
});

test('published release commit rewrites retain verifiable immutable provenance', () => {
  const mapPath = path.join(repoRoot, 'docs', 'release_commit_map.json');
  const provenance = JSON.parse(fs.readFileSync(mapPath, 'utf8'));

  assertExactKeys(provenance, ['schema', 'schema_version', 'releases'], 'provenance');
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
    assertExactKeys(
      release,
      [
        'tag',
        'build',
        'published_artifact_commit',
        'current_public_commit',
        'rewrite',
        'verification',
      ],
      `${release.tag} release`,
    );
    assertExactKeys(release.rewrite, ['date', 'reason', 'removed_path'], `${release.tag} rewrite`);
    assertExactKeys(
      release.verification,
      ['scope', 'result', 'verified_at', 'tree_digest'],
      `${release.tag} verification`,
    );
    assertExactKeys(
      release.verification.tree_digest,
      ['algorithm', 'record_format', 'excluded_path_prefix', 'entry_count', 'value'],
      `${release.tag} tree digest`,
    );
    assert.equal(release.verification.tree_digest.algorithm, 'sha256');
    assert.equal(release.verification.tree_digest.record_format, 'git-ls-tree-r-z-v1');
    assert.equal(
      release.verification.tree_digest.excluded_path_prefix,
      release.rewrite.removed_path,
    );
    assert.match(release.verification.tree_digest.value, /^[0-9a-f]{64}$/);

    const key = `${release.tag}:${release.build}`;
    assert.equal(releaseKeys.has(key), false, `duplicate release provenance entry: ${key}`);
    releaseKeys.add(key);

    const taggedCommit = git(['rev-list', '-n', '1', release.tag], { encoding: 'utf8' }).trim();
    assert.equal(taggedCommit, release.current_public_commit, `${release.tag} commit`);
    const actualDigest = filteredTreeDigest(
      release.current_public_commit,
      release.verification.tree_digest.excluded_path_prefix,
    );
    assert.deepEqual(
      actualDigest,
      {
        entryCount: release.verification.tree_digest.entry_count,
        value: release.verification.tree_digest.value,
      },
      `${release.tag} filtered tree digest`,
    );

    // The original artifact commit may be intentionally unreachable after a
    // public-history rewrite. When the object is present in an audit clone,
    // verify both sides of the recorded equivalence instead of only the tag side.
    let publishedCommitAvailable = true;
    try {
      git(['cat-file', '-e', `${release.published_artifact_commit}^{commit}`]);
    } catch {
      publishedCommitAvailable = false;
    }
    if (publishedCommitAvailable) {
      assert.deepEqual(
        filteredTreeDigest(
          release.published_artifact_commit,
          release.verification.tree_digest.excluded_path_prefix,
        ),
        actualDigest,
        `${release.tag} published/current filtered trees`,
      );
    }
  }

  const release = provenance.releases.find((entry) => entry.tag === 'v1.1.34');
  assert.ok(release, 'v1.1.34 provenance entry');
  assert.equal(release.build, 'B-444');
  assert.equal(
    release.published_artifact_commit,
    'ec2121e97789bf3859f72623a1053a0f506251ee',
  );
  assert.equal(release.current_public_commit, 'eb99f1ad17e54dc32b8ad959e1798193b6f39d1b');
});

test('LS dry run requires an explicit local state path', () => {
  assert.throws(() => parseDryRunArgs([]), /--state is required/);
  assert.equal(parseDryRunArgs(['--help']).help, true);
  assert.equal(parseDryRunArgs(['--state', 'release_state/local.state.json']).state, 'release_state/local.state.json');
});

test('full release set accepts only a signed, verified, externally validated Windows installer', (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-windows-primary-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const installer = path.join(root, 'Kirin-Hypha-1.2.3-Windows-x64-Setup.exe');
  fs.writeFileSync(installer, 'signed installer fixture');
  const digest = sha256File(installer);
  const preDigest = '1'.repeat(64);
  const postDigest = '2'.repeat(64);
  const uninstallerDigest = '3'.repeat(64);
  const identity = { version: '1.2.3', commit: '0'.repeat(40), bNumber: 'B-123' };
  fs.writeFileSync(`${installer}.sha256`, `${digest}  ${path.basename(installer)}\n`);
  const signatureTarget = (role, targetDigest) => ({
    role,
    file_name: `${role}.exe`,
    status: 'Valid',
    sha256: targetDigest,
    signer_subject: 'CN=Kirin fixture',
    signer_thumbprint: 'A'.repeat(40),
    timestamp_subject: 'CN=Timestamp fixture',
    timestamp_thumbprint: 'B'.repeat(40),
  });
  const manifest = {
    schema: 'kirin-hypha-windows-installer-v1',
    product: { name: 'Kirin Hypha', version: identity.version, platform: 'windows-x64', format: 'VST3' },
    source: {
      commit: identity.commit,
      b_number: identity.bNumber,
      github_actions_run: 'https://github.com/heyalohaloha/kirin_hypha/actions/runs/123',
    },
    installer: {
      sha256: digest,
      payload: [
        { role: 'PRE', binary_sha256: preDigest },
        { role: 'POST', binary_sha256: postDigest },
      ],
    },
    signing: {
      status: 'valid',
      workflow_run: 'https://github.com/heyalohaloha/kirin_sense_lens/actions/runs/456',
      verification: {
        targets: [
          signatureTarget('installer', digest),
          signatureTarget('installed PRE VST3 binary', preDigest),
          signatureTarget('installed POST VST3 binary', postDigest),
          signatureTarget('installed uninstaller', uninstallerDigest),
        ],
      },
    },
    ci_validation: { status: 'passed' },
    external_validation: { status: 'complete' },
    distribution: { primary: true, public_ready: true },
  };
  fs.writeFileSync(`${installer}.json`, JSON.stringify(manifest));

  assert.equal(requireWindowsInstaller(root, identity), installer);
  assert.equal(
    parseReleaseSetArgs(['--windows-artifact-dir', root]).windowsInstallerDir,
    root,
  );
  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...manifest,
    signing: { status: 'verified_unsigned_ci_candidate' },
  }));
  assert.throws(() => requireWindowsInstaller(root, identity), /not Authenticode-ready/);
  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...manifest,
    source: { ...manifest.source, commit: 'f'.repeat(40) },
  }));
  assert.throws(() => requireWindowsInstaller(root, identity), /source commit or B number/);
  fs.writeFileSync(`${installer}.json`, JSON.stringify({
    ...manifest,
    signing: {
      ...manifest.signing,
      verification: {
        targets: manifest.signing.verification.targets.map((target) => (
          target.role === 'installed uninstaller' ? { ...target, status: 'NotSigned' } : target
        )),
      },
    },
  }));
  assert.throws(() => requireWindowsInstaller(root, identity), /installed uninstaller/);
});

test('Windows fallback ZIP manifest excludes operator workflow state', (context) => {
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
    payloadSigning: 'signed',
    artifactName: 'kirin-hypha-windows-vst3',
    runUrl: 'https://github.com/example/project/actions/runs/1',
    commit: '0123456789abcdef',
    bNumber: 'B-000',
  };
  const manifest = artifactManifestFor(opts, 'Kirin-Hypha-test', zipPath, packageRoot, bundles);
  const publicJson = JSON.stringify(manifest);

  assertExactKeys(
    manifest,
    [
      'schema',
      'schema_version',
      'product',
      'generated_at',
      'purpose',
      'known_limitations',
      'package',
      'binary_artifact',
      'external_validation',
      'contents',
      'validation',
    ],
    'Windows public manifest',
  );
  assertExactKeys(manifest.product, ['name', 'version'], 'Windows public product');
  assertExactKeys(
    manifest.package,
    [
      'name',
      'path',
      'size_bytes',
      'sha256',
      'format',
      'contains_top_level_folder',
      'distribution_role',
      'payload_signing',
    ],
    'Windows public package',
  );
  assertExactKeys(
    manifest.binary_artifact,
    [
      'source',
      'artifact_name',
      'run_url',
      'commit',
      'b_number',
      'ci_result',
      'windows_job',
      'windows_job_gates',
    ],
    'Windows public binary artifact',
  );
  assertExactKeys(
    manifest.external_validation,
    ['status', 'note'],
    'Windows public external validation',
  );
  assertExactKeys(
    manifest.validation,
    ['local_zip_test', 'local_sha256_file', 'binaries'],
    'Windows public validation',
  );
  for (const content of manifest.contents) {
    assertExactKeys(content, ['path', 'sha256'], `Windows public content ${content.path}`);
  }
  for (const binary of manifest.validation.binaries) {
    assertExactKeys(binary, ['label', 'path', 'sha256'], `Windows public binary ${binary.label}`);
  }
  assert.equal(manifest.schema, 'kirin-hypha-windows-vst3-fallback-artifact-v3');
  assert.equal(manifest.schema_version, 3);
  assert.equal(manifest.external_validation.status, 'complete');
  assert.equal(manifest.package.name, 'Kirin-Hypha-test.zip');
  assert.equal(manifest.binary_artifact.commit, opts.commit);
  assert.deepEqual(manifest.binary_artifact.windows_job_gates, [
    'Build kirin_hypha_ffi staticlib (MSVC)',
    'Validate two-slot continuous Analysis contract',
    'Windows preflight gate',
    'Apply tracked JUCE patches',
    'Configure JUCE VST3 shell (Windows)',
    'Validate Windows UI render contract',
    'Build JUCE VST3 shell (Windows)',
    'Validate Windows VST3 audio transparency',
    'Verify Windows VST3 artifacts',
    'Upload Windows VST3 artifacts',
    'Validate Windows VST3 with pluginval',
    'Build Windows installer and sign all executable surfaces',
    'Verify Windows installer install, upgrade, signatures, and uninstall',
    'Upload primary Windows installer',
    'Package fallback Windows VST3 ZIP',
    'Upload fallback Windows VST3 ZIP',
  ]);
  const receipt = commitReceiptFor(opts);
  assert.match(receipt, /CI gates: Analysis contract, UI render, exact audio transparency,/);
  assert.match(receipt, /layout verify, pluginval strictness 5, artifact packaging/);
  assert.match(receipt, /External validation: complete/);
  assert.doesNotMatch(
    publicJson,
    /Daisuke|lsUpload|ls_upload|releaseStatus|release_status|productId|variantId|productAdminUrl|storeName|expectedFilesCount|operator/,
  );
  assert.equal(manifest.contents.length, 2);

  const localState = localReleaseStateFor(opts, manifest);
  assert.equal(localState.lsUpload, 'skip_primary_installer_required');
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
  assert.equal(pendingState.lsUpload, 'skip_primary_installer_required');
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
  assert.throws(
    () => parseWindowsArgs(['--payload-signing', 'targeted']),
    /must be signed or unsigned/,
  );
});
