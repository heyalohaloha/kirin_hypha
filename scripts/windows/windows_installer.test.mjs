import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  VERSION,
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

test('Windows installer arguments default to unsigned fail-safe CI mode', () => {
  assert.deepEqual(
    parseBuildArgs([]),
    {
      artifactDir: 'juce_shell/build-windows',
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
  assert.throws(() => parseBuildArgs(['--signing', 'targeted']), /unsigned or signed/);
  assert.throws(() => parseBuildArgs(['--external-validation', 'reported']), /pending or complete/);
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
});

test('Inno recipe owns only Kirin bundle paths and signs generated uninstall surfaces', () => {
  const source = fs.readFileSync(path.join(scriptDir, 'kirin-hypha-installer.iss'), 'utf8');
  assert.match(source, /PrivilegesRequired=lowest/);
  assert.match(source, /PrivilegesRequiredOverridesAllowed=commandline dialog/);
  assert.match(source, /DefaultDirName=\{autocf\}\\VST3/);
  assert.match(source, /UninstallFilesDir=\{autopf\}\\Kirin Mastering\\Kirin Hypha/);
  assert.match(source, /SignedUninstaller=yes/);
  assert.match(source, /SignTool=kirin_esigner/);
  assert.match(source, /CloseApplications=yes/);
  assert.match(source, /RestartApplications=no/);
  assert.equal((source.match(/Type: filesandordirs/g) || []).length, 2);
  assert.doesNotMatch(source, /Type:\s*filesandordirs;\s*Name:\s*"\{autocf\}\\VST3"/);
});

test('installer verifier gates repeat install, signed uninstaller, and unrelated VST3 preservation', () => {
  const source = fs.readFileSync(path.join(scriptDir, 'verify-installer.ps1'), 'utf8');
  assert.match(source, /foreach \(\$installPass in 1\.\.2\)/);
  assert.match(source, /installed uninstaller/);
  assert.match(source, /Get-AuthenticodeSignature/);
  assert.match(source, /Uninstaller removed an unrelated VST3 file/);
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
