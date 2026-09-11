import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  VERSION,
  bindAaxSourceIdentity,
  bundleRecord,
  innoCompilerArgs,
  parseArgs as parseBuildArgs,
} from './build-installer.mjs';
import {
  TOTP_WINDOW_MS,
  delayForFreshWindow,
  parseArgs as parseSignArgs,
  readWindowState,
  signingEnvironment,
  waitForFreshWindow,
} from './sign-codesigntool.mjs';
import {
  inspectWindowsAaxRecord,
  loadWindowsAaxBundleManifest,
  readPeMachine,
} from './windows-aax-bundles.mjs';
import {
  SIGNED_MANIFEST_NAME,
  loadWindowsAaxBuildProvenance,
  loadWindowsAaxSignedProvenance,
  writeWindowsAaxBuildProvenance,
} from './windows-aax-provenance.mjs';

const scriptDir = path.dirname(fileURLToPath(import.meta.url));

function createBundle(root, role) {
  const name = `Kirin Hypha ${role}.vst3`;
  const bundle = path.join(root, name);
  const binary = path.join(bundle, 'Contents', 'x86_64-win', name);
  const metadata = path.join(bundle, 'Contents', 'Resources', 'moduleinfo.json');
  fs.mkdirSync(path.dirname(binary), { recursive: true });
  fs.mkdirSync(path.dirname(metadata), { recursive: true });
  fs.writeFileSync(binary, `${role} PE fixture`);
  fs.writeFileSync(metadata, JSON.stringify({ Version: VERSION }, null, 2));
  return bundle;
}

function createPeFixture(filePath, machine = 0x8664) {
  const data = Buffer.alloc(256);
  data.write('MZ', 0, 'ascii');
  data.writeUInt32LE(0x80, 0x3c);
  data.write('PE\0\0', 0x80, 'ascii');
  data.writeUInt16LE(machine, 0x84);
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, data);
}

function createAaxBuildConfiguration(root, kimeraEmbedded) {
  fs.writeFileSync(path.join(root, 'CMakeCache.txt'), [
    `KIRIN_HYPHA_KIMERA_FONT_FILE:FILEPATH=${kimeraEmbedded ? 'C:/licensed/Kimera.otf' : ''}`,
    `KIRIN_HYPHA_KIMERA_APP_LICENSE_CONFIRMED:BOOL=${kimeraEmbedded ? 'ON' : 'OFF'}`,
    `KIRIN_HYPHA_REQUIRE_KIMERA_FONT:BOOL=${kimeraEmbedded ? 'ON' : 'OFF'}`,
  ].join('\n'));
  for (const role of ['PRE', 'POST']) {
    fs.writeFileSync(
      path.join(root, `KirinHypha${role}_AAX.vcxproj`),
      '<PreprocessorDefinitions>JucePlugin_AAXDisableAudioSuite=1</PreprocessorDefinitions>',
    );
  }
}

test('Windows installer arguments default to unsigned fail-safe CI mode', () => {
  assert.deepEqual(
    parseBuildArgs([]),
    {
      artifactDir: 'juce_shell/build-windows',
      aaxArtifactDir: '',
      outputDir: 'dist/WINDOWS_CI',
      signing: 'unsigned',
      externalValidation: 'pending',
      bNumber: process.env.KIRIN_B_NUMBER || '',
      commit: process.env.KIRIN_COMMIT || '',
      runUrl: process.env.KIRIN_GITHUB_RUN_URL || '',
      help: false,
    },
  );
  assert.equal(parseBuildArgs(['--signing', 'signed']).signing, 'signed');
  assert.equal(parseBuildArgs(['--aax-artifact-dir', 'build-aax']).aaxArtifactDir, 'build-aax');
  assert.throws(() => parseBuildArgs(['--aax-artifact-dir']), /requires a value/);
  assert.throws(() => parseBuildArgs(['--signing', 'targeted']), /unsigned or signed/);
  assert.throws(() => parseBuildArgs(['--external-validation', 'reported']), /pending or complete/);
});

test('Windows AAX manifest and PE gate require exact PRE/POST x64 bundles', (context) => {
  const artifactRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-aax-bundles-'));
  context.after(() => fs.rmSync(artifactRoot, { recursive: true, force: true }));
  const manifest = loadWindowsAaxBundleManifest({ artifactRoot });
  assert.deepEqual(manifest.bundles.map((record) => record.role), ['PRE', 'POST']);
  for (const record of manifest.bundles) {
    const binary = path.join(record.bundle, ...record.binaryRelative.split('/'));
    createPeFixture(binary);
    assert.equal(readPeMachine(binary), 0x8664);
    assert.equal(inspectWindowsAaxRecord(record).role, record.role);
  }
  const post = manifest.bundles[1];
  createPeFixture(path.join(post.bundle, ...post.binaryRelative.split('/')), 0x14c);
  assert.throws(() => inspectWindowsAaxRecord(post), /expected x64/);
});

test('Windows AAX build provenance pins source, licensed typography, surface, and binaries', (context) => {
  const artifactRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-aax-provenance-'));
  context.after(() => fs.rmSync(artifactRoot, { recursive: true, force: true }));
  const records = loadWindowsAaxBundleManifest({ artifactRoot }).bundles;
  for (const record of records) {
    createPeFixture(path.join(record.bundle, ...record.binaryRelative.split('/')));
  }
  createAaxBuildConfiguration(artifactRoot, true);
  const source = { commit: 'a'.repeat(40), b_number: 'B-823', state: 'clean source' };
  writeWindowsAaxBuildProvenance({ artifactRoot, version: VERSION, source });
  const verified = loadWindowsAaxBuildProvenance({
    artifactRoot,
    version: VERSION,
    requireReleaseReady: true,
  });
  assert.equal(verified.manifest.source.commit, source.commit);
  assert.equal(verified.manifest.release.mode, 'release');
  assert.equal(verified.manifest.release.native_only, true);
  assert.equal(verified.manifest.release.audio_suite_enabled, false);
  const signedManifest = {
    schema: 'kirin-hypha-windows-aax-signed-v1',
    source: verified.manifest.source,
    product: verified.manifest.product,
    release: verified.manifest.release,
    unsigned_build: {
      manifest_sha256: verified.manifestSha256,
      bundles: verified.manifest.bundles,
    },
    bundles: verified.manifest.bundles.map((record) => ({
      ...record,
      pace_verified: true,
      authenticode_verified: true,
    })),
    signing: { pace_verified: true, authenticode_verified: true },
  };
  fs.writeFileSync(
    path.join(artifactRoot, SIGNED_MANIFEST_NAME),
    JSON.stringify(signedManifest),
  );
  assert.equal(
    loadWindowsAaxSignedProvenance({ artifactRoot, version: VERSION }).manifest.source.commit,
    source.commit,
  );
  signedManifest.bundles[0].pace_verified = false;
  fs.writeFileSync(
    path.join(artifactRoot, SIGNED_MANIFEST_NAME),
    JSON.stringify(signedManifest),
  );
  assert.throws(
    () => loadWindowsAaxSignedProvenance({ artifactRoot, version: VERSION }),
    /PRE Windows AAX signed provenance is incomplete/,
  );

  const postBinary = path.join(records[1].bundle, ...records[1].binaryRelative.split('/'));
  fs.appendFileSync(postBinary, 'changed');
  assert.throws(
    () => loadWindowsAaxBuildProvenance({ artifactRoot, version: VERSION }),
    /POST Windows AAX no longer matches/,
  );

  createPeFixture(postBinary);
  createAaxBuildConfiguration(artifactRoot, false);
  writeWindowsAaxBuildProvenance({ artifactRoot, version: VERSION, source });
  assert.equal(
    loadWindowsAaxBuildProvenance({ artifactRoot, version: VERSION }).manifest.release.mode,
    'diagnostic',
  );
  assert.throws(
    () => loadWindowsAaxBuildProvenance({ artifactRoot, version: VERSION, requireReleaseReady: true }),
    /licensed Kimera App font/,
  );
});

test('Windows AAX installer identity is derived from clean source and signed provenance', () => {
  const opts = { commit: '', bNumber: '' };
  const current = { commit: 'a'.repeat(40), bNumber: 'B-823' };
  const aax = { manifest: { source: { commit: current.commit, b_number: current.bNumber } } };
  assert.deepEqual(bindAaxSourceIdentity(opts, current, aax), {
    commit: current.commit,
    bNumber: current.bNumber,
  });
  assert.throws(
    () => bindAaxSourceIdentity(opts, current, {
      manifest: { source: { commit: 'b'.repeat(40), b_number: current.bNumber } },
    }),
    /does not match the installer source/,
  );
});

test('Windows AAX signing is one combined wraptool operation through a store certificate', () => {
  const source = fs.readFileSync(path.join(scriptDir, 'sign-aax-wraptool.ps1'), 'utf8');
  assert.match(source, /"--signid", \$thumbprint/);
  assert.match(
    source,
    /"--explicitsigningoptions=sign \/sha1 \$thumbprint \/fd sha256 \/tr http:\/\/ts\.ssl\.com \/td sha256"/,
  );
  assert.doesNotMatch(source, /--extrasigningoptions/);
  assert.match(source, /windows-aax-provenance\.mjs verify-build/);
  assert.match(source, /windows-aax-provenance\.mjs write-signed/);
  assert.match(source, /OutputDir must not already exist/);
  assert.doesNotMatch(source, /--dsig(?:\s|",\s*)off/);
});

test('bundle discovery requires one complete PRE and POST Windows bundle', (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-installer-bundles-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  createBundle(root, 'PRE');
  createBundle(root, 'POST');
  assert.equal(bundleRecord(root, 'PRE').role, 'PRE');
  assert.ok(fs.statSync(bundleRecord(root, 'POST').binary).size > 0);
  createBundle(path.join(root, 'duplicate'), 'POST');
  assert.throws(() => bundleRecord(root, 'POST'), /expected exactly one/);
});

test('Inno compiler signed route uses the shared eSigner hook', () => {
  const unsigned = innoCompilerArgs({
    outputDir: 'C:\\out',
    payloadDir: 'C:\\payload',
    signing: 'unsigned',
  });
  assert.ok(unsigned.some((arg) => arg === `/DAppVersion=${VERSION}`));
  assert.ok(unsigned.every((arg) => !arg.startsWith('/Skirin_esigner=')));

  const signed = innoCompilerArgs({
    outputDir: 'C:\\out',
    payloadDir: 'C:\\payload',
    signing: 'signed',
  });
  assert.ok(signed.includes('/DSignedBuild=1'));
  assert.ok(signed.some((arg) => arg.startsWith('/Skirin_esigner=')));
  assert.ok(signed.some((arg) => arg.endsWith('--input-file $f')));

  const withAax = innoCompilerArgs({
    outputDir: 'C:\\out',
    payloadDir: 'C:\\payload',
    signing: 'signed',
    aaxRecords: [
      { role: 'PRE', bundle: 'C:\\payload\\Kirin Hypha PRE.aaxplugin' },
      { role: 'POST', bundle: 'C:\\payload\\Kirin Hypha POST.aaxplugin' },
    ],
  });
  assert.ok(withAax.includes('/DWithAax=1'));
  assert.ok(withAax.some((arg) => arg.startsWith('/DPreAaxBundle=')));
  assert.ok(withAax.some((arg) => arg.startsWith('/DPostAaxBundle=')));
});

test('Inno recipe owns only Kirin bundle paths and signs generated uninstall surfaces', () => {
  const source = fs.readFileSync(path.join(scriptDir, 'kirin-hypha-installer.iss'), 'utf8');
  assert.match(source, /PrivilegesRequired=lowest/);
  assert.match(source, /PrivilegesRequired=admin/);
  assert.match(source, /PrivilegesRequiredOverridesAllowed=commandline dialog/);
  assert.match(source, /DefaultDirName=\{autocf\}\\VST3/);
  assert.match(source, /UninstallFilesDir=\{autopf\}\\Kirin Mastering\\Kirin Hypha/);
  assert.match(source, /SignedUninstaller=yes/);
  assert.match(source, /SignTool=kirin_esigner/);
  assert.match(source, /CloseApplications=yes/);
  assert.match(source, /RestartApplications=no/);
  assert.match(source, /^ArchitecturesAllowed=x64os$/m);
  assert.match(source, /^ArchitecturesInstallIn64BitMode=x64os$/m);
  assert.doesNotMatch(source, /^Architectures(?:Allowed|InstallIn64BitMode)=x64$/m);
  assert.equal((source.match(/Type: filesandordirs/g) || []).length, 4);
  assert.doesNotMatch(source, /Type:\s*filesandordirs;\s*Name:\s*"\{autocf\}\\VST3"/);
  assert.match(source, /\{commoncf\}\\Avid\\Audio\\Plug-Ins\\Kirin Hypha PRE\.aaxplugin/);
  assert.match(source, /\{commoncf\}\\Avid\\Audio\\Plug-Ins\\Kirin Hypha POST\.aaxplugin/);
});

test('installer verifier gates same-version reinstall, signed uninstaller, and unrelated VST3 preservation', () => {
  const source = fs.readFileSync(path.join(scriptDir, 'verify-installer.ps1'), 'utf8');
  assert.match(source, /foreach \(\$installPass in 1\.\.2\)/);
  assert.match(source, /-PreviousInstaller is required for AAX upgrade verification/);
  assert.match(source, /Resolve-HyphaUninstaller/);
  assert.match(source, /prior_public_upgrade/);
  assert.match(source, /installed uninstaller/);
  assert.match(source, /Get-AuthenticodeSignature/);
  assert.match(source, /Uninstaller removed an unrelated VST3 file/);
  assert.match(source, /Assert-AaxPayload/);
  assert.match(source, /wraptool verify --localonly|\$Wraptool verify --localonly/);
  assert.match(source, /PACE verification failed/);
  assert.match(source, /Uninstaller removed an unrelated AAX file/);
  assert.match(source, /Find-HyphaUninstallEntries/);
  assert.match(source, /distribution\.public_ready/);
});

test('eSigner environment is fail-closed and never accepts partial credentials', () => {
  const complete = {
    ESIGNER_USERNAME: 'user',
    ESIGNER_PASSWORD: 'password',
    ESIGNER_CREDENTIAL_ID: 'credential',
    ESIGNER_TOTP_SECRET: 'totp',
    CODE_SIGN_TOOL_PATH: 'C:\\tool',
  };
  assert.equal(signingEnvironment(complete).credentialId, 'credential');
  for (const name of Object.keys(complete)) {
    assert.throws(() => signingEnvironment({ ...complete, [name]: '' }), new RegExp(name));
  }
  assert.deepEqual(parseSignArgs(['--input-file', 'setup.exe']).inputFile, 'setup.exe');
  assert.throws(() => parseSignArgs(['--secret', 'value']), /unknown option/);
});

test('eSigner requests never reuse the same TOTP authorization window', async (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-totp-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const stateFile = path.join(root, 'window.state');
  const start = 42 * TOTP_WINDOW_MS + 2_000;
  const delays = [];
  let now = start;
  await waitForFreshWindow({
    now: () => now,
    sleep: async (delay) => { delays.push(delay); now += delay; },
    stateFile,
    logger: false,
  });
  assert.equal(readWindowState(stateFile), 42);
  await waitForFreshWindow({
    now: () => now,
    sleep: async (delay) => { delays.push(delay); now += delay; },
    stateFile,
    logger: false,
  });
  assert.deepEqual(delays, [29_000]);
  assert.equal(readWindowState(stateFile), 43);
  assert.equal(delayForFreshWindow(43 * TOTP_WINDOW_MS + 1, 42), 0);
});
