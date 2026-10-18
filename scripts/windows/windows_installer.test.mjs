import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { SCRIPT_PATH as cleanupScriptPath, cleanupScript, embeddedBlock as embeddedCleanupBlock,
  generatedBlock as generatedCleanupBlock } from './installer_user_cleanup.mjs';

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
  resolveInvocation,
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
  fs.appendFileSync(filePath, 'KirinHyphaUpdateKeySha256=disabled;');
}

function createAaxBuildConfiguration(root, { kimeraEmbedded, distributionBuild }) {
  fs.writeFileSync(path.join(root, 'CMakeCache.txt'), [
    `KIRIN_HYPHA_KIMERA_FONT_FILE:FILEPATH=${kimeraEmbedded ? 'C:/licensed/Kimera.otf' : ''}`,
    `KIRIN_HYPHA_KIMERA_APP_LICENSE_CONFIRMED:BOOL=${kimeraEmbedded ? 'ON' : 'OFF'}`,
    `KIRIN_HYPHA_AAX_DISTRIBUTION_BUILD:BOOL=${distributionBuild ? 'ON' : 'OFF'}`,
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
      updatePublicKey: '',
      legalDir: '',
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

test('Windows AAX build provenance pins source, optional typography, surface, and binaries', (context) => {
  const artifactRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-aax-provenance-'));
  context.after(() => fs.rmSync(artifactRoot, { recursive: true, force: true }));
  const records = loadWindowsAaxBundleManifest({ artifactRoot }).bundles;
  for (const record of records) {
    createPeFixture(path.join(record.bundle, ...record.binaryRelative.split('/')));
  }
  createAaxBuildConfiguration(artifactRoot, {
    kimeraEmbedded: false,
    distributionBuild: true,
  });
  const source = { commit: 'a'.repeat(40), b_number: 'B-823', state: 'clean source' };
  writeWindowsAaxBuildProvenance({ artifactRoot, version: VERSION, source });
  const verified = loadWindowsAaxBuildProvenance({
    artifactRoot,
    version: VERSION,
    requireReleaseReady: true,
  });
  assert.equal(verified.manifest.source.commit, source.commit);
  assert.equal(verified.manifest.release.mode, 'release');
  assert.equal(verified.manifest.release.kimera_embedded, false);
  assert.equal(verified.manifest.release.native_only, true);
  assert.equal(verified.manifest.release.audio_suite_enabled, false);
  const signedManifest = {
    schema: 'kirin-hypha-windows-aax-signed-v1',
    source: verified.manifest.source,
    product: verified.manifest.product,
    release: verified.manifest.release,
    updateCheck: verified.manifest.updateCheck,
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
  createAaxBuildConfiguration(artifactRoot, {
    kimeraEmbedded: true,
    distributionBuild: false,
  });
  writeWindowsAaxBuildProvenance({ artifactRoot, version: VERSION, source });
  assert.equal(
    loadWindowsAaxBuildProvenance({ artifactRoot, version: VERSION }).manifest.release.mode,
    'diagnostic',
  );
  assert.equal(
    loadWindowsAaxBuildProvenance({ artifactRoot, version: VERSION }).manifest.release.kimera_embedded,
    true,
  );
  assert.throws(
    () => loadWindowsAaxBuildProvenance({ artifactRoot, version: VERSION, requireReleaseReady: true }),
    /explicit distribution build/,
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
  assert.equal(signed.find((arg) => arg.startsWith('/Skirin_esigner=')),
    `/Skirin_esigner=$q${process.execPath}$q $q${path.join(scriptDir, 'inno-sign-codesigntool.mjs')}$q --input-file $f`);

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
  assert.match(source, /SignToolRetryCount=0/);
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

test('installer verifier gates fresh install, reinstall, signatures, upgrades and unrelated payload preservation', () => {
  const read = (name) => fs.readFileSync(path.join(scriptDir, name), 'utf8');
  const main = read('verify-installer.ps1');
  const writer = read('installer_qualification.ps1');
  const payload = read('installer_payload.ps1');
  const upgrade = read('installer_upgrade.ps1');
  for (const name of ['installer_payload', 'installer_environment', 'installer_upgrade', 'installer_qualification']) {
    assert.match(main, new RegExp(`\\. "\\$PSScriptRoot/${name}\\.ps1"`));
  }
  assert.match(main, /Write-HyphaInstallerQualification \$manifestData \$manifestPath \$Signing \$records \$withAax \$upgradeMatrix/);
  // Fresh install runs first on a verified clean machine; upgrades are a separate later phase.
  const order = ['Assert-HyphaInstallerEnvironment | Out-Null', 'foreach ($installPass in 1..2)',
    'Invoke-HyphaUpgradeMatrix', 'Write-HyphaInstallerQualification'].map((text) => main.indexOf(text));
  assert.ok(order.every((index, i) => index > 0 && (i === 0 || index > order[i - 1])), order.join(','));
  assert.match(main, /Signed release verification requires -PreviousInstallerDir/);
  assert.match(writer, /Signed installer qualification requires the previous public release upgrade matrix/);
  const source = [main, writer, payload, upgrade].join('\n');
  for (const text of ['Resolve-HyphaUninstaller', 'prior_public_upgrade', 'installed uninstaller', 'Get-AuthenticodeSignature',
    'Uninstaller removed an unrelated VST3 file', 'Uninstaller removed an unrelated AAX file', 'Assert-AaxPayload',
    'PACE verification failed', 'Find-HyphaUninstallEntries', 'distribution.public_ready', 'Assert-HyphaResolvedPayload',
    'Assert-HyphaRegistration', 'Assert-HyphaSignatureRecords']) assert.ok(source.includes(text), text);
  // The matrix reports the fixture bytes it ran; the writer records them where the release reader
  // compares them with the tracked fixture, and the Windows fixture runs that reader on the output.
  assert.match(upgrade, /return \[ordered\]@\{ fixture_sha256 = \$fixtureSha256; scenarios = @\(\$results\) \}/);
  assert.match(writer, /fixture_sha256 = \[string\]\$upgradeMatrix\.fixture_sha256/);
  const reader = fs.readFileSync(path.join(scriptDir, '..', 'ls_release', 'build_kirin_hypha_release_set.mjs'), 'utf8');
  assert.match(reader, /upgrade\.fixture_sha256 !== sha256\(fixtureFile\)/);
  assert.match(read('installer_payload_fixture.ps1'), /requirePreviousReleaseUpgrades\(manifest\)/);
  // PACE stdout is captured and logged; no other wraptool call can join the returned records.
  const pace = payload.split('\n').filter((line) => line.includes('$Wraptool verify'));
  assert.deepEqual(pace.map((line) => line.trim()), ['$output = @(& $Wraptool verify --localonly --in $Binary)']);
  assert.match(payload, /foreach \(\$line in \$output\) \{ Write-Host/);
});

test('Inno recipe removes the starting user\'s exact older copy as that user before an all-users install', () => {
  const source = fs.readFileSync(path.join(scriptDir, 'kirin-hypha-installer.iss'), 'utf8');
  const code = source.slice(source.indexOf('[Code]'));
  const appId = source.match(/^AppId=\{(\{[0-9A-F-]{36}\})$/m)?.[1];
  assert.ok(appId);
  assert.ok(code.includes(`HyphaUninstallKey = 'Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${appId}_is1';`));
  const payload = fs.readFileSync(path.join(scriptDir, 'installer_payload.ps1'), 'utf8');
  assert.ok(payload.includes(`$HyphaAppUninstallKey = '${appId}_is1'`));
  const cleanup = fs.readFileSync(cleanupScriptPath, 'utf8');
  assert.ok(cleanup.includes(`'HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${appId}_is1'`));
  assert.match(code, /function PrepareToInstall\(var NeedsRestart: Boolean\): String;/);
  assert.match(code, /if IsAdminInstallMode then[\s\S]*HyphaUserCleanupRemoveCommand[\s\S]*else if HyphaAllUsersCopyFound then/);
  assert.match(code, /function UpdateReadyMemo\(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,\s+MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String\): String;/);
  // The embedded command is exactly the reviewed script, for both modes.
  assert.equal(embeddedCleanupBlock(source), generatedCleanupBlock());
  // The only process the recipe starts: Windows PowerShell from System32, as the user who started Setup.
  assert.deepEqual(code.match(/\b(Exec|ExecAsOriginalUser|ShellExec|ShellExecAsOriginalUser|ExecAndLogOutput|ExecAndCaptureOutput)\(/g), ['ExecAsOriginalUser(']);
  assert.match(code, /ExecAsOriginalUser\(ExpandConstant\('\{sys\}\\WindowsPowerShell\\v1\.0\\powershell\.exe'\),\s+'-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ' \+ Command,/);
  // The elevated recipe itself deletes nothing in a user profile: no file, folder or registry removal.
  assert.doesNotMatch(code, /DelTree\(|DeleteFile\(|RemoveDir\(|RenameFile\(|RegDelete|\{localappdata\}|\{userappdata\}|HKEY_CURRENT_USER|Sleep\(/);
  // In use: nothing changed. Could not finish, or never checked: nothing new installed. All stop with exit code 7.
  assert.match(code, /if Code = HyphaUserCopyInUse then\s+Result := CustomMessage\('HyphaMigrationInUse'\)\s+else if \(Code = 21\) or \(Code = 22\) or \(Code = 30\) then\s+Result := CustomMessage\('HyphaMigrationFailed'\)\s+else if Code <> 0 then\s+Result := CustomMessage\('HyphaMigrationUnchecked'\);/);
  const cleanupCodes = [...fs.readFileSync(cleanupScriptPath, 'utf8').matchAll(/\bexit (\d+)/g)].map((m) => Number(m[1]));
  assert.deepEqual([...new Set(cleanupCodes)].sort((a, b) => a - b), [0, 10, 20, 21, 22, 30, 31]);
  for (const name of ['HyphaMigrationReadyMemo', 'HyphaMigrationInUse', 'HyphaMigrationFailed', 'HyphaMigrationUnchecked', 'HyphaAllUsersPresent']) {
    for (const language of ['english', 'japanese']) assert.match(source, new RegExp(`^${language}\\.${name}=\\S`, 'm'));
  }
});

test('Inno reads the recipe as UTF-8: valid UTF-8 without a BOM and a compiler from 6.3.0 on', () => {
  // Inno Setup 6.3.0 added UTF-8 without a BOM for .iss files and no longer recommends a BOM
  // (https://jrsoftware.org/files/is6-whatsnew.htm). Japanese messages depend on this reading.
  const bytes = fs.readFileSync(path.join(scriptDir, 'kirin-hypha-installer.iss'));
  assert.notDeepEqual([...bytes.subarray(0, 3)], [0xef, 0xbb, 0xbf]);
  assert.doesNotThrow(() => new TextDecoder('utf-8', { fatal: true }).decode(bytes));
  assert.match(bytes.toString('utf8'), /^japanese\.HyphaMigrationInUse=.*[\u3040-\u30ff]/m);
  const workflow = fs.readFileSync(path.join(scriptDir, '..', '..', '.github', 'workflows', 'ci.yml'), 'utf8');
  const versions = [...workflow.matchAll(/gh release download is-(\d+)_(\d+)_(\d+) --repo jrsoftware\/issrc/g)].map((m) => m.slice(1).map(Number));
  assert.ok(versions.length > 0);
  for (const [major, minor] of versions) assert.ok(major > 6 || (major === 6 && minor >= 3), `${major}.${minor}`);
});

test('the per-user cleanup script renames both bundles before deleting and touches only exact names', () => {
  const script = fs.readFileSync(cleanupScriptPath, 'utf8');
  assert.doesNotMatch(script, /[^\x09\x0a\x0d\x20-\x7e]/);
  assert.equal(cleanupScript('check', script).split('\r\n')[0], "$Mode = 'check'");
  assert.throws(() => cleanupScript('other', script), /unknown cleanup mode/);
  // Windows PowerShell 5.1 only: no PowerShell 7 syntax.
  assert.doesNotMatch(script, /\?\?|\?\.|\s\?\s[^:]*\s:\s|&&|\|\||-Parallel|ForEach-Object -Parallel/);
  assert.match(script, /Join-Path \$env:LOCALAPPDATA 'Programs\\Common\\VST3'/);
  assert.match(script, /'Kirin Hypha ' \+ \$Role \+ '\.vst3'/);
  assert.doesNotMatch(script, /\*|Get-ChildItem|Remove-Item -LiteralPath \$(bundle|root)|-Recurse -Force -LiteralPath \$root/);
  // Rename both, roll back on failure, then delete with rd (which never follows links).
  const order = ['Rename-Item -LiteralPath $bundle', 'Rename-Item -LiteralPath (Get-HyphaUserStaged $done)', 'Remove-HyphaUserTree (Get-HyphaUserStaged $_)', 'Remove-Item -LiteralPath $key'].map((text) => script.indexOf(text));
  assert.ok(order.every((index, i) => index > 0 && (i === 0 || index > order[i - 1])), order.join(','));
  assert.match(script, /& \$cmd \/d \/c rd \/s \/q \$Path/);
  assert.match(script, /Join-Path \$env:SystemRoot 'System32\\cmd\.exe'/);
});

test('previous public installer pins drive a separate upgrade and scope-migration matrix', () => {
  const fixture = JSON.parse(fs.readFileSync(path.join(scriptDir, 'fixtures', 'previous-public-installers.json'), 'utf8'));
  assert.equal(fixture.schema, 'kirin-hypha-previous-windows-installers-v1');
  assert.equal(fixture.release_download_base, 'https://github.com/heyalohaloha/kirin_hypha/releases/download');
  const newer = (a, b) => a.split('.').map(Number).reduce((r, n, i) => r || n - b.split('.').map(Number)[i], 0);
  for (const entry of fixture.installers) {
    assert.equal(entry.file_name, `Kirin-Hypha-${entry.version}-Windows-x64-Setup.exe`);
    assert.match(entry.sha256, /^[0-9a-f]{64}$/);
    assert.ok(Number.isSafeInteger(entry.bytes) && entry.bytes > 0);
    assert.ok(['current-user', 'all-users'].includes(entry.default_scope));
    // Signed verification requires strictly older pins; before the release bump CI may be equal.
    assert.ok(newer(VERSION, entry.version) >= 0, `${entry.version} must not be newer than ${VERSION}`);
  }
  const ids = fixture.scenarios.map((scenario) => scenario.id);
  assert.equal(new Set(ids).size, ids.length);
  const versions = new Set(fixture.installers.map((entry) => entry.version));
  for (const scenario of fixture.scenarios) {
    assert.ok(scenario.previous.length > 0 && scenario.previous.every((step) => versions.has(step.version)));
    assert.ok(['replaced', 'refused', 'refused-while-locked-then-replaced'].includes(scenario.expect));
  }
  assert.ok(fixture.required_for_release.every((id) => ids.includes(id)));
  // The defaults users actually had: the per-user default VST3 release, then the all-users AAX release.
  const required = fixture.scenarios.filter((scenario) => fixture.required_for_release.includes(scenario.id));
  const defaultCurrentUser = fixture.installers.filter((entry) => entry.default_scope === 'current-user').map((e) => e.version);
  assert.ok(required.some((s) => s.previous.length === 1 && defaultCurrentUser.includes(s.previous[0].version) && s.previous[0].scope === 'default'));
  assert.ok(required.some((s) => s.previous.length === 2 && s.previous.every((step) => step.scope === 'default')));
  // A held PRE and a held POST each stop Setup with the older copy intact, then the retry completes.
  const locked = required.filter((s) => s.expect === 'refused-while-locked-then-replaced').map((s) => s.lock_role).sort();
  assert.deepEqual(locked, ['POST', 'PRE']);
  for (const scenario of fixture.scenarios) {
    assert.equal(scenario.expect === 'refused-while-locked-then-replaced', ['PRE', 'POST'].includes(scenario.lock_role), scenario.id);
  }
  assert.ok(required.every((s) => s.new_scope === 'all-users' && !s.vst3_only_installer));
  const download = fs.readFileSync(path.join(scriptDir, 'download_previous_installers.ps1'), 'utf8');
  assert.match(download, /\$item\.Length -ne \[long\]\$entry\.bytes -or \$hash -cne \$entry\.sha256/);
  assert.match(download, /Get-AuthenticodeSignature/);
});

test('public Windows CI checks installer sources before the build and verifies previous-release upgrades', () => {
  const workflow = fs.readFileSync(path.join(scriptDir, '..', '..', '.github', 'workflows', 'ci.yml'), 'utf8');
  const at = (text) => { const index = workflow.indexOf(text); assert.ok(index > 0, text); return index; };
  assert.ok(at('Install verified Inno Setup compiler') < at('scripts/windows/check_installer_sources.ps1'));
  assert.ok(at('scripts/windows/check_installer_sources.ps1') < at('Build kirin_hypha_ffi staticlib (MSVC)'));
  assert.ok(at('scripts/windows/download_previous_installers.ps1') < at('scripts/windows/verify-installer.ps1'));
  assert.match(workflow, /-PreviousInstallerDir "\$env:RUNNER_TEMP\/hypha-previous-installers"/);
  const check = fs.readFileSync(path.join(scriptDir, 'check_installer_sources.ps1'), 'utf8');
  assert.match(check, /Language\.Parser\]::ParseFile/);
  assert.match(check, /installer_payload_fixture\.ps1/);
  assert.match(check, /foreach \(\$variant in @\("vst3", "aax"\)\)/);
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

test('Windows eSigner invokes current Java without cmd argument parsing', (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-codesigntool-java-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const command = path.join(root, 'temurin-17', 'bin', 'java.exe');
  const jar = path.join(root, 'jar', 'code_sign_tool-1.3.2.jar');
  for (const filePath of [command, jar]) {
    fs.mkdirSync(path.dirname(filePath), { recursive: true });
    fs.writeFileSync(filePath, '');
  }
  const env = {
    ESIGNER_USERNAME: 'user',
    ESIGNER_PASSWORD: 'password with & shell chars',
    ESIGNER_CREDENTIAL_ID: 'credential',
    ESIGNER_TOTP_SECRET: 'totp',
    CODE_SIGN_TOOL_PATH: root,
    CODE_SIGN_TOOL_JAVA: command,
  };
  assert.deepEqual(resolveInvocation(env, 'win32'), {
    command,
    cwd: root,
    prefixArgs: ['-jar', jar],
  });
});

test('Windows eSigner rejects a relative explicit Java runtime', () => {
  const env = {
    ESIGNER_USERNAME: 'user', ESIGNER_PASSWORD: 'password',
    ESIGNER_CREDENTIAL_ID: 'credential', ESIGNER_TOTP_SECRET: 'totp',
    CODE_SIGN_TOOL_PATH: 'C:\\tool', CODE_SIGN_TOOL_JAVA: 'java.exe',
  };
  assert.throws(() => resolveInvocation(env, 'win32'), /absolute path/);
});

test('public Windows CI builds unsigned candidates without signing credentials', () => {
  const workflow = fs.readFileSync(path.join(scriptDir, '..', '..', '.github', 'workflows', 'ci.yml'), 'utf8');
  assert.doesNotMatch(workflow, /secrets[.\[]|ESIGNER_|setup-java|CodeSignTool/);
  assert.doesNotMatch(workflow, /inputs\.windows_signing|inputs\.windows_external_validation/);
  assert.match(workflow, /name: Build Windows installer and sign all executable surfaces/);
  assert.match(workflow, /WINDOWS_SIGNING: unsigned/);
  assert.match(workflow, /WINDOWS_EXTERNAL_VALIDATION: pending/);
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
