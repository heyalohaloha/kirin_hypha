#!/usr/bin/env node
import childProcess from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const THIS_FILE = fileURLToPath(import.meta.url);
const SCRIPT_DIR = path.dirname(THIS_FILE);
const ROOT = path.resolve(SCRIPT_DIR, '..', '..');

function usage() {
  return `Usage:
  node scripts/ls_release/build_kirin_hypha_release_set.mjs [options]

Builds the release artifact set:
  1. Windows readiness/preflight gates
  2. Verified signed Windows installer from a windows-latest artifact
  3. macOS signed/notarized installer pkg
  4. macOS HP zip

Options:
  --windows-installer-dir <dir>         Downloaded kirin-hypha-windows-installer artifact.
  --windows-artifact-dir <dir>          Deprecated alias for --windows-installer-dir.
  --skip-windows-package                Only for diagnostics; release output is not complete.
  --skip-macos-pkg                      Only for diagnostics; release output is not complete.
  --skip-hp-zip                         Only for diagnostics; release output is not complete.
  --help                                Show this help.
`;
}

export function parseArgs(argv) {
  const opts = {
    windowsInstallerDir: process.env.KIRIN_WINDOWS_INSTALLER_DIR || '',
    skipWindowsPackage: false,
    skipMacosPkg: false,
    skipHpZip: false,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--windows-installer-dir' || arg === '--windows-artifact-dir') {
      opts.windowsInstallerDir = requireValue(argv, ++i, arg);
    } else if (arg === '--skip-windows-package') {
      opts.skipWindowsPackage = true;
    } else if (arg === '--skip-macos-pkg') {
      opts.skipMacosPkg = true;
    } else if (arg === '--skip-hp-zip') {
      opts.skipHpZip = true;
    } else if (arg === '--help' || arg === '-h') {
      opts.help = true;
    } else {
      throw new Error(`unknown option: ${arg}`);
    }
  }
  return opts;
}

function requireValue(argv, index, name) {
  const value = argv[index];
  if (!value) throw new Error(`${name} requires a value`);
  return value;
}

function log(message) {
  console.log(`[build-kirin-hypha-release-set] ${message}`);
}

function run(command, args) {
  log(`${command} ${args.map((arg) => (/\s/.test(arg) ? JSON.stringify(arg) : arg)).join(' ')}`);
  const result = childProcess.spawnSync(command, args, {
    cwd: ROOT,
    encoding: 'utf8',
    stdio: 'inherit',
  });
  if (result.status !== 0) throw new Error(`${command} exited with ${result.status}`);
}

function sha256(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

function currentReleaseIdentity() {
  const versionSource = fs.readFileSync(path.join(ROOT, 'crates', 'hypha_pre', 'Cargo.toml'), 'utf8');
  const version = versionSource.match(/^version\s*=\s*"([^"]+)"/m)?.[1];
  if (!version) throw new Error('Hypha release version is missing');
  const commitResult = childProcess.spawnSync('git', ['rev-parse', 'HEAD'], {
    cwd: ROOT,
    encoding: 'utf8',
  });
  if (commitResult.status !== 0) throw new Error('Unable to resolve the Hypha release commit');
  const subjectResult = childProcess.spawnSync('git', ['log', '-1', '--pretty=%s'], {
    cwd: ROOT,
    encoding: 'utf8',
  });
  if (subjectResult.status !== 0) throw new Error('Unable to resolve the Hypha release B number');
  const bNumber = subjectResult.stdout.match(/\bB-\d+\b/)?.[0];
  if (!bNumber) throw new Error('The Hypha release commit subject has no B number');
  return { version, commit: commitResult.stdout.trim(), bNumber };
}

function requireValidSigningTargets(manifest) {
  const targets = manifest.signing?.verification?.targets;
  if (!Array.isArray(targets) || targets.length !== 4) {
    throw new Error('Windows installer must contain four Authenticode verification records');
  }
  const roles = [
    'installer',
    'installed PRE VST3 binary',
    'installed POST VST3 binary',
    'installed uninstaller',
  ];
  for (const role of roles) {
    const matches = targets.filter((target) => target.role === role);
    if (matches.length !== 1 || matches[0].status !== 'Valid') {
      throw new Error(`Windows Authenticode verification is incomplete for ${role}`);
    }
    if (!/^[0-9a-f]{64}$/.test(matches[0].sha256 || '')) {
      throw new Error(`Windows Authenticode record has no valid hash for ${role}`);
    }
    if (!matches[0].signer_subject || !matches[0].signer_thumbprint
        || !matches[0].timestamp_subject || !matches[0].timestamp_thumbprint) {
      throw new Error(`Windows Authenticode signer or timestamp evidence is missing for ${role}`);
    }
  }
  return targets;
}

export function requireWindowsInstaller(value, expectedIdentity = currentReleaseIdentity()) {
  const candidates = [
    value,
    'dist/WINDOWS_CI/KirinHypha-Windows-signed-full',
    'dist/WINDOWS_LS',
  ].filter(Boolean);
  const directory = candidates
    .map((candidate) => path.resolve(ROOT, candidate))
    .find((candidate) => fs.statSync(candidate, { throwIfNoEntry: false })?.isDirectory());
  if (!directory) {
    throw new Error(
      'Signed Windows installer is required. Download `KirinHypha-Windows-signed-full` to ' +
      'dist/WINDOWS_CI/KirinHypha-Windows-signed-full or pass --windows-installer-dir.',
    );
  }
  const expectedName = `Kirin-Hypha-${expectedIdentity.version}-Windows-x64-Setup.exe`;
  const installers = fs.readdirSync(directory)
    .filter((name) => /^Kirin-Hypha-.*-Windows-x64-Setup\.exe$/.test(name));
  if (installers.length !== 1) {
    throw new Error(`expected one Windows Setup.exe in ${directory}, found ${installers.length}`);
  }
  if (installers[0] !== expectedName) {
    throw new Error(`Windows installer version mismatch: ${installers[0]} != ${expectedName}`);
  }
  const installer = path.join(directory, installers[0]);
  const hashFile = `${installer}.sha256`;
  const manifestFile = `${installer}.json`;
  if (!fs.existsSync(hashFile) || !fs.existsSync(manifestFile)) {
    throw new Error('Windows installer SHA-256 or JSON sidecar is missing');
  }
  const manifest = JSON.parse(fs.readFileSync(manifestFile, 'utf8').replace(/^\uFEFF/, ''));
  const hashLine = fs.readFileSync(hashFile, 'utf8').trim();
  const hashMatch = hashLine.match(/^([0-9a-f]{64}) {2}(.+)$/);
  if (!hashMatch || hashMatch[2] !== expectedName) {
    throw new Error('Windows installer SHA-256 sidecar has an invalid name or format');
  }
  const expectedHash = hashMatch[1];
  if (sha256(installer) !== expectedHash || manifest.installer?.sha256 !== expectedHash) {
    throw new Error('Windows installer hash does not match both sidecars');
  }
  if (manifest.schema !== 'kirin-hypha-windows-installer-v1') {
    throw new Error(`unsupported Windows installer manifest: ${manifest.schema}`);
  }
  if (manifest.product?.name !== 'Kirin Hypha'
      || manifest.product?.version !== expectedIdentity.version
      || manifest.product?.platform !== 'windows-x64'
      || manifest.product?.format !== 'VST3') {
    throw new Error('Windows installer product identity does not match this release');
  }
  if (manifest.source?.commit !== expectedIdentity.commit
      || manifest.source?.b_number !== expectedIdentity.bNumber) {
    throw new Error('Windows installer source commit or B number does not match this release');
  }
  if (!/^https:\/\/github\.com\/heyalohaloha\/kirin_hypha\/actions\/runs\/\d+$/.test(
    manifest.source?.github_actions_run || '',
  )) {
    throw new Error('Windows installer has no valid Hypha source CI run URL');
  }
  if (manifest.signing?.status !== 'valid') {
    throw new Error(`Windows installer is not Authenticode-ready: ${manifest.signing?.status}`);
  }
  if (!/^https:\/\/github\.com\/heyalohaloha\/(kirin_sense_lens|kirin_hypha)\/actions\/runs\/\d+$/.test(
    manifest.signing?.workflow_run || '',
  )) {
    throw new Error('Windows installer has no valid signing workflow run URL');
  }
  const signingTargets = requireValidSigningTargets(manifest);
  const installerTarget = signingTargets.find((target) => target.role === 'installer');
  if (installerTarget.sha256 !== expectedHash) {
    throw new Error('Signed installer verification hash does not match the release artifact');
  }
  const payloads = manifest.installer?.payload;
  if (!Array.isArray(payloads) || payloads.length !== 2) {
    throw new Error('Windows installer manifest must contain PRE and POST payload records');
  }
  for (const role of ['PRE', 'POST']) {
    const payload = payloads.find((item) => item.role === role);
    const target = signingTargets.find((item) => item.role === `installed ${role} VST3 binary`);
    if (!payload || payload.binary_sha256 !== target.sha256) {
      throw new Error(`Windows ${role} payload hash does not match installed verification`);
    }
  }
  if (manifest.ci_validation?.status !== 'passed') {
    throw new Error(`Windows installer verification is incomplete: ${manifest.ci_validation?.status}`);
  }
  if (manifest.external_validation?.status !== 'complete') {
    throw new Error('Windows external DAW validation is incomplete');
  }
  if (manifest.distribution?.primary !== true || manifest.distribution?.public_ready !== true) {
    throw new Error('Windows installer manifest does not mark the primary artifact public-ready');
  }
  return installer;
}

function runMain() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    console.log(usage());
    return;
  }

  run('cargo', ['run', '--package', 'xtask', '--', 'windows-readiness']);
  run('cargo', ['run', '--package', 'xtask', '--', 'windows-preflight']);

  if (opts.skipWindowsPackage) {
    log('SKIP Windows installer; release set is incomplete.');
  } else {
    const installer = requireWindowsInstaller(opts.windowsInstallerDir);
    log(`Windows primary installer ready: ${path.relative(ROOT, installer)}`);
  }

  if (opts.skipMacosPkg) {
    log('SKIP macOS LS pkg; release set is incomplete.');
  } else {
    run('node', ['scripts/ls_release/build_kirin_hypha_pkg.mjs']);
  }

  if (opts.skipHpZip) {
    log('SKIP macOS HP zip; release set is incomplete.');
  } else {
    run('cargo', ['run', '--package', 'xtask', '--', 'release-package']);
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === THIS_FILE) {
  try {
    runMain();
  } catch (error) {
    console.error(`[build-kirin-hypha-release-set] ERROR: ${error.message}`);
    process.exit(1);
  }
}
