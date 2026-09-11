import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { validateAaxBuildIdentity, verifyAaxBundle } from './aax_bundle_verify.mjs';
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

test('release source identity resolves the current full commit and B number without treating JUCE patches as owned source', () => {
  const identity = readReleaseSourceIdentity({ root: repoRoot });
  assert.match(identity.commit, /^[0-9a-f]{40}$/);
  assert.equal(identity.shortCommit, identity.commit.slice(0, 12));
  assert.match(identity.bNumber, /^B-\d+$/);
  assert.ok(['clean source', 'modified source'].includes(identity.sourceState));
  assert.ok(identity.dirtyEntries.every((entry) => !entry.endsWith('juce_shell/JUCE')));
});

test('AAX target is Native-only and stamps signed build identity before distribution', () => {
  const cmake = fs.readFileSync(path.join(repoRoot, 'juce_shell/CMakeLists.txt'), 'utf8');
  const buildScript = fs.readFileSync(path.join(repoRoot, 'scripts/build_aax_universal.sh'), 'utf8');
  const stamp = fs.readFileSync(path.join(repoRoot, 'scripts/stamp_aax_bundle_identity.sh'), 'utf8');
  const diagnosticReceipt = fs.readFileSync(
    path.join(repoRoot, 'scripts/ls_release/aax_diagnostic_receipt.mjs'),
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
  assert.match(buildScript, /mutually exclusive/);
  assert.match(buildScript, /cannot include Kimera/);
  assert.match(diagnosticReceipt, /not_for_distribution: true/);
  assert.match(diagnosticReceipt, /host_validation_target/);
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
