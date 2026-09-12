import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  AAX_APPLE_AUTHORITY,
  AAX_APPLE_TEAM_ID,
  AAX_PACE_PUBLISHER_ID,
  AAX_PACE_SIGNER_NAME,
  parseAaxAppleSignature,
  parseAaxPaceSignature,
  validateAaxBuildIdentity,
  verifyAaxBundle,
} from './aax_bundle_verify.mjs';
import {
  AAX_NOTARIZATION_SCHEMA,
  parseNotarytoolAccepted,
  validateAaxNotarizationReceipt,
} from './aax_notarization_receipt.mjs';
import { loadMacAaxBundleManifest } from './kirin_hypha_aax_bundles.mjs';
import { readReleaseSourceIdentity } from './release_source_identity.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');

test('AAX manifest resolves exactly PRE and POST outside the ordinary ship set', () => {
  const manifest = loadMacAaxBundleManifest({ root: repoRoot });
  assert.equal(manifest.bundles.length, 2);
  assert.deepEqual(manifest.bundles.map((bundle) => bundle.role), ['PRE', 'POST']);
  assert.ok(manifest.bundles.every((bundle) => bundle.kind === 'aax'));
  assert.ok(manifest.bundles.every((bundle) => bundle.sourcePath.startsWith(manifest.defaultBuildRoot)));
  assert.ok(manifest.bundles.every((bundle) => (
    path.dirname(bundle.install_relative) === 'Library/Application Support/Avid/Audio/Plug-Ins'
  )));
});

test('AAX manifest rejects an install path outside the Avid directory', (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-aax-manifest-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const source = JSON.parse(fs.readFileSync(
    path.join(repoRoot, 'config/hypha_macos_aax_bundles.json'),
    'utf8',
  ));
  source.bundles[0].install_relative = 'Library/Audio/Plug-Ins/VST3/Kirin Hypha PRE.aaxplugin';
  fs.mkdirSync(path.join(root, 'config'), { recursive: true });
  fs.writeFileSync(
    path.join(root, 'config/hypha_macos_aax_bundles.json'),
    `${JSON.stringify(source)}\n`,
  );
  assert.throws(
    () => loadMacAaxBundleManifest({ root }),
    /outside the Avid plug-in directory/,
  );
});

test('AAX verifier fails closed when the requested bundle is missing', () => {
  assert.throws(
    () => verifyAaxBundle({
      bundlePath: '/tmp/kirin-hypha-aax-missing.aaxplugin',
      executableName: 'Kirin Hypha PRE',
      bundleIdentifier: 'com.kirinmastering.hypha.pre',
      version: '1.1.49',
    }),
    /AAX bundle missing/,
  );
});

test('AAX distribution identity requires the exact commit, clean source, Kimera, and Native-only surface', () => {
  const expected = {
    sourceId: '0123456789abcdef0123456789abcdef01234567',
    sourceState: 'clean source',
    requireKimera: true,
    requireNativeOnly: true,
  };
  const actual = {
    sourceId: expected.sourceId,
    sourceState: expected.sourceState,
    kimeraEmbedded: 'true',
    audioSuiteEnabled: 'false',
    aaxBuildMode: 'release',
  };
  assert.doesNotThrow(() => validateAaxBuildIdentity(actual, expected));
  assert.throws(
    () => validateAaxBuildIdentity({
      ...actual,
      sourceId: 'abcdefabcdefabcdefabcdefabcdefabcdefabcd',
    }, expected),
    /source id/,
  );
  assert.throws(
    () => validateAaxBuildIdentity({ ...actual, sourceState: 'modified source' }, expected),
    /source state/,
  );
  assert.throws(
    () => validateAaxBuildIdentity({ ...actual, kimeraEmbedded: 'false' }, expected),
    /Kimera/,
  );
  assert.throws(
    () => validateAaxBuildIdentity({ ...actual, audioSuiteEnabled: 'true' }, expected),
    /AudioSuite/,
  );
  assert.throws(
    () => validateAaxBuildIdentity({ ...actual, aaxBuildMode: 'diagnostic' }, expected),
    /build mode/,
  );
});

test('AAX diagnostic identity requires an explicit diagnostic build marker', () => {
  const expected = {
    sourceId: '0123456789abcdef0123456789abcdef01234567',
    sourceState: 'modified source',
    requireDiagnostic: true,
    requireNativeOnly: true,
  };
  const actual = {
    sourceId: expected.sourceId,
    sourceState: expected.sourceState,
    kimeraEmbedded: 'false',
    audioSuiteEnabled: 'false',
    aaxBuildMode: 'diagnostic',
  };
  assert.doesNotThrow(() => validateAaxBuildIdentity(actual, expected));
  assert.throws(
    () => validateAaxBuildIdentity({ ...actual, aaxBuildMode: 'release' }, expected),
    /diagnostic bundle build mode/,
  );
});

test('AAX signature parsers require the exact Apple and PACE release identities', () => {
  const apple = [
    `Authority=${AAX_APPLE_AUTHORITY}`,
    'Authority=Developer ID Certification Authority',
    'Authority=Apple Root CA',
    'Timestamp=Sep 10, 2026 at 0:26:24',
    `TeamIdentifier=${AAX_APPLE_TEAM_ID}`,
    'CDHash=0123456789abcdef0123456789abcdef01234567',
  ].join('\n');
  assert.equal(parseAaxAppleSignature(apple).teamId, AAX_APPLE_TEAM_ID);
  assert.throws(
    () => parseAaxAppleSignature(apple.replace(AAX_APPLE_AUTHORITY, 'Apple Development: Someone')),
    /Apple authority/,
  );
  assert.throws(
    () => parseAaxAppleSignature(apple.replace(/^Timestamp=.*$/m, '')),
    /timestamp is missing/,
  );

  const pace = [
    `Signer name:            ${AAX_PACE_SIGNER_NAME}`,
    'Signer GUID:            991BD2C1-1D2D-A27D-819E-E60035AB5695',
    `Signer PublisherId:     ${AAX_PACE_PUBLISHER_ID}`,
  ].join('\n');
  assert.equal(parseAaxPaceSignature(pace).publisherId, AAX_PACE_PUBLISHER_ID);
  assert.throws(
    () => parseAaxPaceSignature(pace.replace(AAX_PACE_PUBLISHER_ID, '0x00000000')),
    /PublisherId/,
  );
});

test('AAX notarization receipt requires an Accepted submission bound to current signed hashes', () => {
  const source = {
    commit: '0123456789abcdef0123456789abcdef01234567',
    bNumber: 'B-830',
  };
  const bundle = (role) => ({
    role,
    bundle: `Kirin Hypha ${role}.aaxplugin`,
    binary_sha256: role === 'PRE' ? '1'.repeat(64) : '2'.repeat(64),
    apple_cdhash: role === 'PRE' ? '3'.repeat(40) : '4'.repeat(40),
    apple_authority: AAX_APPLE_AUTHORITY,
    apple_team_id: AAX_APPLE_TEAM_ID,
    pace_signer: AAX_PACE_SIGNER_NAME,
    pace_signer_guid: '991BD2C1-1D2D-A27D-819E-E60035AB5695',
    pace_publisher_id: AAX_PACE_PUBLISHER_ID,
  });
  const receipt = {
    schema: AAX_NOTARIZATION_SCHEMA,
    generated_at: '2026-09-12T00:00:00.000Z',
    source: { commit: source.commit, b_number: source.bNumber, state: 'clean source' },
    product: { name: 'Kirin Hypha', version: '1.1.49', platform: 'macos-universal', format: 'AAX' },
    submission: { id: '12345678-1234-1234-1234-123456789abc', status: 'Accepted' },
    archive: { file_name: 'Kirin-Hypha-1.1.49-macOS-AAX.zip', size_bytes: 123, sha256: '5'.repeat(64) },
    bundles: [bundle('PRE'), bundle('POST')],
  };
  const expected = { source, version: '1.1.49', bundles: receipt.bundles };
  assert.equal(parseNotarytoolAccepted(JSON.stringify(receipt.submission)).status, 'Accepted');
  assert.doesNotThrow(() => validateAaxNotarizationReceipt(receipt, expected));
  assert.throws(
    () => validateAaxNotarizationReceipt({
      ...receipt,
      submission: { ...receipt.submission, status: 'Invalid' },
    }, expected),
    /expected Accepted/,
  );
  assert.throws(
    () => validateAaxNotarizationReceipt({
      ...receipt,
      bundles: receipt.bundles.map((record) => (
        record.role === 'POST' ? { ...record, apple_cdhash: '6'.repeat(40) } : record
      )),
    }, expected),
    /POST AAX no longer matches/,
  );
  assert.throws(
    () => validateAaxNotarizationReceipt({
      ...receipt,
      archive: { ...receipt.archive, file_name: 'unrelated.zip' },
    }, expected),
    /archive identity is invalid/,
  );
});

test('AAX bundle identity stamp is idempotent on macOS', {
  skip: process.platform !== 'darwin',
}, (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-aax-stamp-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const bundle = path.join(root, 'Fixture.aaxplugin');
  const plist = path.join(bundle, 'Contents', 'Info.plist');
  const identity = path.join(root, 'HyphaBuildIdentity.h');
  fs.mkdirSync(path.dirname(plist), { recursive: true });
  fs.writeFileSync(plist, [
    '<?xml version="1.0" encoding="UTF-8"?>',
    '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">',
    '<plist version="1.0"><dict></dict></plist>',
    '',
  ].join('\n'));
  fs.writeFileSync(identity, [
    '#define HYPHA_SOURCE_COMMIT "0123456789abcdef0123456789abcdef01234567"',
    '#define HYPHA_SOURCE_STATE "clean source"',
    '',
  ].join('\n'));
  const stamp = path.join(repoRoot, 'scripts/stamp_aax_bundle_identity.sh');
  execFileSync('bash', [stamp, bundle, identity, '0']);
  execFileSync('bash', [stamp, bundle, identity, '0']);
  const mode = execFileSync(
    '/usr/libexec/PlistBuddy',
    ['-c', 'Print :KirinHyphaAaxBuildMode', plist],
    { encoding: 'utf8' },
  ).trim();
  assert.equal(mode, 'diagnostic');
});

test('release source identity uses exact fixture commits and distinguishes patches from gitlink changes', (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-source-identity-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const git = (cwd, ...args) => execFileSync('git', [
    '-c', 'user.name=Hypha Test', '-c', 'user.email=test@example.invalid',
    '-c', 'commit.gpgsign=false', '-C', cwd, ...args,
  ], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
  git(root, 'init');
  const juce = path.join(root, 'juce_shell/JUCE');
  fs.mkdirSync(juce, { recursive: true });
  git(juce, 'init');
  fs.writeFileSync(path.join(juce, 'source.txt'), 'upstream\n');
  git(juce, 'add', 'source.txt');
  git(juce, 'commit', '-m', 'upstream fixture');
  fs.writeFileSync(path.join(root, '.gitmodules'), [
    '[submodule "juce_shell/JUCE"]', '\tpath = juce_shell/JUCE', '\turl = ./JUCE', '',
  ].join('\n'));
  fs.writeFileSync(path.join(root, 'owned.txt'), 'owned source\n');
  git(root, 'add', '.gitmodules', 'owned.txt', 'juce_shell/JUCE');
  git(root, 'commit', '-m', '[B-123] release fixture');
  const commit = git(root, 'rev-parse', 'HEAD');
  const expected = {
    commit, shortCommit: commit.slice(0, 12), bNumber: 'B-123',
    sourceState: 'clean source', dirtyEntries: [],
  };
  assert.deepEqual(readReleaseSourceIdentity({ root }), expected);

  fs.appendFileSync(path.join(juce, 'source.txt'), 'tracked build-time patch\n');
  assert.deepEqual(readReleaseSourceIdentity({ root }), expected);
  fs.appendFileSync(path.join(root, 'owned.txt'), 'owned edit\n');
  const modified = readReleaseSourceIdentity({ root });
  assert.equal(modified.sourceState, 'modified source');
  assert.ok(modified.dirtyEntries.some((entry) => entry.endsWith('owned.txt')));
  git(root, 'restore', 'owned.txt');

  git(juce, 'add', 'source.txt');
  git(juce, 'commit', '-m', 'different upstream fixture');
  const changedRevision = readReleaseSourceIdentity({ root });
  assert.equal(changedRevision.sourceState, 'modified source');
  assert.ok(changedRevision.dirtyEntries.some((entry) => entry.endsWith('juce_shell/JUCE')));

  git(root, 'commit', '--allow-empty', '-m', 'Merge temporary CI candidate');
  assert.throws(() => readReleaseSourceIdentity({ root }), /release commit subject has no B number/);
});

test('AAX target is Native-only and stamps signed build identity before distribution', () => {
  const cmake = fs.readFileSync(path.join(repoRoot, 'juce_shell/CMakeLists.txt'), 'utf8');
  const buildScript = fs.readFileSync(path.join(repoRoot, 'scripts/build_aax_universal.sh'), 'utf8');
  const stamp = fs.readFileSync(path.join(repoRoot, 'scripts/stamp_aax_bundle_identity.sh'), 'utf8');
  const verifier = fs.readFileSync(path.join(repoRoot, 'scripts/ls_release/aax_bundle_verify.mjs'), 'utf8');
  const notarization = fs.readFileSync(
    path.join(repoRoot, 'scripts/ls_release/aax_notarization_receipt.mjs'),
    'utf8',
  );
  const diagnosticReceipt = fs.readFileSync(
    path.join(repoRoot, 'scripts/ls_release/aax_diagnostic_receipt.mjs'),
    'utf8',
  );
  const packageBuild = fs.readFileSync(
    path.join(repoRoot, 'scripts/ls_release/build_kirin_hypha_pkg.mjs'),
    'utf8',
  );
  const releasePackage = fs.readFileSync(
    path.join(repoRoot, 'xtask/src/release_package.rs'),
    'utf8',
  );
  assert.match(cmake, /target_compile_definitions\(\$\{TARGET\}_AAX PRIVATE JucePlugin_AAXDisableAudioSuite=1\)/);
  assert.match(cmake, /stamp_aax_bundle_identity\.sh/);
  assert.match(buildScript, /cmake -E remove_directory/);
  assert.match(stamp, /KirinHyphaSourceID/);
  assert.match(stamp, /HYPHA_SOURCE_COMMIT/);
  assert.match(stamp, /KirinHyphaKimeraEmbedded/);
  assert.match(stamp, /KirinHyphaAudioSuiteEnabled/);
  assert.match(stamp, /KirinHyphaAaxBuildMode/);
  assert.match(buildScript, /--diagnostic/);
  assert.match(buildScript, /--diagnostic-sign/);
  assert.match(buildScript, /--dry-run/);
  assert.match(buildScript, /KIRIN_AAX_PACE_ACCOUNT/);
  assert.match(buildScript, /Fusion\/Versions\/6\/bin\/wraptool/);
  assert.match(buildScript, /--account/);
  assert.match(buildScript, /mutually exclusive/);
  assert.match(buildScript, /diagnostic modes cannot include Kimera/);
  assert.match(diagnosticReceipt, /not_for_distribution: true/);
  assert.match(diagnosticReceipt, /host_validation_target/);
  assert.match(diagnosticReceipt, /verifyAaxBundle/);
  assert.match(diagnosticReceipt, /--signed/);
  assert.match(diagnosticReceipt, /pace_verified: signed/);
  assert.match(buildScript, /unnotarized and never use for distribution/);
  assert.match(verifier, /AAX_APPLE_AUTHORITY/);
  assert.match(verifier, /AAX_PACE_PUBLISHER_ID/);
  assert.doesNotMatch(verifier, /--check-notarization/);
  assert.match(notarization, /notarytool', 'submit/);
  assert.match(notarization, /notarytool', 'info/);
  assert.match(notarization, /status !== 'Accepted'/);
  assert.match(packageBuild, /verifyMacAaxNotarizationReceipt/);
  assert.match(packageBuild, /installer pkg notarytool submit/);
  assert.match(packageBuild, /installer pkg notarytool info/);
  assert.match(releasePackage, /verify_notarization_receipt/);
});

test('self-hosted macOS AAX CI uses the Universal build entry point', () => {
  const workflow = fs.readFileSync(path.join(repoRoot, '.github/workflows/aax-phase-a.yml'), 'utf8');
  assert.match(workflow, /scripts\/build_aax_universal\.sh/);
  assert.match(workflow, /x86_64-apple-darwin aarch64-apple-darwin/);
  assert.doesNotMatch(workflow, /Build macOS Universal AAX[\s\S]*--sign/);
});

test('self-hosted Windows AAX CI uses the x64 build entry point without signing', () => {
  const workflow = fs.readFileSync(path.join(repoRoot, '.github/workflows/aax-phase-a.yml'), 'utf8');
  assert.match(workflow, /scripts\/build_aax_windows\.ps1/);
  assert.match(workflow, /-LicenseConfirmed/);
  assert.doesNotMatch(workflow, /Build Windows x64 AAX without signing[\s\S]*wraptool/);
});

test('Windows AAX provenance survives build, combined signing, installer, and release-set gates', () => {
  const build = fs.readFileSync(path.join(repoRoot, 'scripts/build_aax_windows.ps1'), 'utf8');
  const sign = fs.readFileSync(path.join(repoRoot, 'scripts/windows/sign-aax-wraptool.ps1'), 'utf8');
  const installer = fs.readFileSync(path.join(repoRoot, 'scripts/windows/build-installer.mjs'), 'utf8');
  const releaseSet = fs.readFileSync(
    path.join(repoRoot, 'scripts/ls_release/build_kirin_hypha_release_set.mjs'),
    'utf8',
  );
  assert.match(build, /windows-aax-provenance\.mjs write-build/);
  assert.match(build, /KIRIN_HYPHA_REQUIRE_KIMERA_FONT=ON/);
  assert.match(sign, /windows-aax-provenance\.mjs verify-build/);
  assert.match(sign, /--require-release-ready/);
  assert.match(sign, /windows-aax-provenance\.mjs write-signed/);
  assert.match(installer, /loadWindowsAaxSignedProvenance/);
  assert.match(installer, /bindAaxSourceIdentity/);
  assert.match(releaseSet, /Windows AAX signed provenance sidecar is missing or changed/);
});
