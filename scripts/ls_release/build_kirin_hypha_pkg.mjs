#!/usr/bin/env node
import childProcess from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { verifyAaxBundle, verifyAaxBundleCopy } from './aax_bundle_verify.mjs';
import {
  parseNotarytoolAccepted,
  verifyMacAaxNotarizationReceipt,
} from './aax_notarization_receipt.mjs';
import { loadMacAaxBundleManifest } from './kirin_hypha_aax_bundles.mjs';
import { loadMacShipBundleManifest } from './kirin_hypha_ship_bundles.mjs';
import {
  readReleaseSourceIdentity,
  requireCleanReleaseSource,
} from './release_source_identity.mjs';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(SCRIPT_DIR, '..', '..');
const TEAM_ID = process.env.KIRIN_TEAM_ID || '7N8BSMA684';
const NOTARY_PROFILE = process.env.KIRIN_NOTARY_PROFILE || 'kirin-notarize';
const SKIP_SIGN = process.env.KIRIN_SKIP_PKG_SIGN === '1';
const SKIP_NOTARIZE = process.env.KIRIN_SKIP_PKG_NOTARIZE === '1' || SKIP_SIGN;
const RELEASE_DIR = path.resolve(
  ROOT,
  process.env.KIRIN_RELEASE_DIR || (SKIP_SIGN ? '/tmp/kirin_hypha_pkg_smoke' : 'dist/LS_UPLOAD'),
);
const VERSION = readCargoVersion();
const PACKAGE_BASE = `Kirin-Hypha-${VERSION}-macOS-Universal`;
const PACKAGE_NAME = SKIP_SIGN ? `${PACKAGE_BASE}-UNSIGNED-DO-NOT-UPLOAD.pkg` : `${PACKAGE_BASE}.pkg`;
const PACKAGE_PATH = path.join(RELEASE_DIR, PACKAGE_NAME);
const COMPONENT_ID = 'com.kirinmastering.hypha.plugins';
const PRODUCT_ID = 'com.kirinmastering.hypha.installer';
const WITH_AAX = process.argv.includes('--with-aax');
const shipManifest = loadMacShipBundleManifest({ root: ROOT });
const aaxManifest = WITH_AAX ? loadMacAaxBundleManifest({ root: ROOT }) : null;
const bundles = [...shipManifest.bundles, ...(aaxManifest?.bundles || [])];

function usage() {
  return `Usage:
  node scripts/ls_release/build_kirin_hypha_pkg.mjs [--with-aax]

Options:
  --with-aax                     Include verified PRE/POST AAX bundles

Environment:
  KIRIN_RELEASE_DIR=<dir>       Output dir. Default: dist/LS_UPLOAD; unsigned default: /tmp/kirin_hypha_pkg_smoke
  KIRIN_INSTALLER_IDENTITY=<id> Developer ID Installer common name
  KIRIN_TEAM_ID=<team>          Developer team id. Default: ${TEAM_ID}
  KIRIN_NOTARY_PROFILE=<name>   notarytool keychain profile. Default: ${NOTARY_PROFILE}
  KIRIN_SKIP_PKG_SIGN=1         Build an UNSIGNED-DO-NOT-UPLOAD smoke package
  KIRIN_SKIP_PKG_NOTARIZE=1     Sign but do not notarize/staple
`;
}

function log(message) {
  console.log(`[build-kirin-hypha-pkg] ${message}`);
}

function run(command, args, options = {}) {
  log(`${command} ${args.map((arg) => (/\s/.test(arg) ? JSON.stringify(arg) : arg)).join(' ')}`);
  const result = childProcess.spawnSync(command, args, {
    cwd: ROOT,
    encoding: 'utf8',
    stdio: options.capture ? ['ignore', 'pipe', 'pipe'] : 'inherit',
  });
  if (result.status !== 0) {
    const details = options.capture ? `${result.stdout || ''}${result.stderr || ''}`.trim() : '';
    throw new Error(`${command} exited with ${result.status}${details ? `\n${details}` : ''}`);
  }
  return result.stdout || '';
}

function readCargoVersion() {
  const toml = fs.readFileSync(path.join(ROOT, 'crates/hypha_pre/Cargo.toml'), 'utf8');
  const match = toml.match(/^version\s*=\s*"([^"]+)"/m);
  if (!match) throw new Error('version not found in crates/hypha_pre/Cargo.toml');
  return match[1];
}

function findInstallerIdentity() {
  if (process.env.KIRIN_INSTALLER_IDENTITY) return process.env.KIRIN_INSTALLER_IDENTITY;
  const output = run('security', ['find-identity', '-v', '-p', 'basic'], { capture: true });
  const matches = [...output.matchAll(/^\s*\d+\)\s+[A-F0-9]+\s+"(Developer ID Installer:[^"]+\(([^)]+)\))"/gm)];
  const match = matches.find((item) => item[2] === TEAM_ID) || matches[0];
  if (!match) {
    throw new Error(`Developer ID Installer identity not found for team ${TEAM_ID}. Install the Apple Developer ID Installer certificate, then rerun.`);
  }
  return match[1];
}

function plistValue(plist, key) {
  return run('/usr/libexec/PlistBuddy', ['-c', `Print :${key}`, plist], { capture: true }).trim();
}

function verifySourceBundle(bundle, releaseIdentity) {
  const source = bundle.sourcePath;
  if (!fs.existsSync(source)) {
    throw new Error(`${bundle.label} missing: ${path.relative(ROOT, source)}`);
  }
  const binary = path.join(source, 'Contents/MacOS', bundle.executable_name);
  const archs = run('lipo', ['-archs', binary], { capture: true }).trim().split(/\s+/);
  if (!archs.includes('x86_64') || !archs.includes('arm64')) {
    throw new Error(`${bundle.label} is not universal: ${archs.join(' ')}`);
  }
  const libs = run('otool', ['-L', binary], { capture: true });
  for (const forbidden of ['WebKit.framework', 'DiscRecording.framework']) {
    if (libs.includes(forbidden)) throw new Error(`${bundle.label} links forbidden ${forbidden}`);
  }
  const plist = path.join(source, 'Contents/Info.plist');
  for (const key of ['CFBundleShortVersionString', 'CFBundleVersion']) {
    const actual = plistValue(plist, key);
    if (actual !== VERSION) throw new Error(`${bundle.label} ${key}=${actual}, expected ${VERSION}`);
  }
  if (bundle.kind === 'au') {
    const usage = run('plutil', ['-p', plist], { capture: true });
    if (!usage.includes('temporary-exception.files.all.read-write')) {
      throw new Error(`${bundle.label} AU resourceUsage missing files.all`);
    }
    if (usage.includes('network.client')) throw new Error(`${bundle.label} AU resourceUsage has network.client`);
  }
  if (bundle.kind === 'aax') {
    verifyAaxBundle({
      bundlePath: source,
      executableName: bundle.executable_name,
      bundleIdentifier: bundle.bundle_identifier,
      version: VERSION,
      sourceId: releaseIdentity.commit,
      sourceState: 'clean source',
      requireKimera: true,
      requireNativeOnly: true,
    });
  } else {
    run('codesign', ['--verify', '--deep', '--strict', '--verbose=2', source]);
    run('codesign', ['--verify', '--deep', '--strict', '--check-notarization', '--verbose=2', source]);
  }
}

function verifyShipBundleContract(installedRoot) {
  const args = [
    'run', '--quiet', '--package', 'xtask', '--',
    'ship-bundle-verify',
    '--build-root', shipManifest.defaultBuildRoot,
  ];
  if (installedRoot) args.push('--installed-root', installedRoot);
  run('cargo', args);
}

function shellSingleQuote(value) {
  return `'${value.replaceAll("'", "'\"'\"'")}'`;
}

function writePreinstall(filePath) {
  const removalPaths = [
    ...new Set(
      bundles.flatMap((bundle) => [
        bundle.install_relative,
        ...bundle.legacy_install_relative,
      ]),
    ),
  ];
  const systemRemovals = removalPaths
    .map((relative) => `remove_bundle ${shellSingleQuote(`/${relative}`)}`)
    .join('\n');
  const userRemovals = removalPaths
    .map((relative) => `  remove_bundle "$home"/${shellSingleQuote(relative)}`)
    .join('\n');
  const script = `#!/bin/sh
set -eu

remove_bundle() {
  if [ -e "$1" ]; then
    rm -rf "$1"
  fi
}

${systemRemovals}

for home in /Users/*; do
  [ -d "$home" ] || continue
${userRemovals}
done

exit 0
`;
  fs.writeFileSync(filePath, script);
  fs.chmodSync(filePath, 0o755);
}

function sha512Base64(filePath) {
  const hash = crypto.createHash('sha512');
  hash.update(fs.readFileSync(filePath));
  return hash.digest('base64');
}

function sha256Hex(filePath) {
  const hash = crypto.createHash('sha256');
  hash.update(fs.readFileSync(filePath));
  return hash.digest('hex');
}

function formatMiB(bytes) {
  return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
}

function findBundleDirectories(root, expectedNames) {
  const matches = new Map(expectedNames.map((name) => [name, []]));
  function visit(directory) {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const absolute = path.join(directory, entry.name);
      if (entry.isDirectory() && matches.has(entry.name)) {
        matches.get(entry.name).push(absolute);
      } else if (entry.isDirectory()) {
        visit(absolute);
      }
    }
  }
  visit(root);
  return matches;
}

function verifyPackagedAax(packagePath, releaseIdentity) {
  if (!WITH_AAX) return;
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-pkg-expand-'));
  const expanded = path.join(temporary, 'expanded');
  try {
    run('pkgutil', ['--expand-full', packagePath, expanded]);
    const expectedNames = aaxManifest.bundles.map((bundle) => path.basename(bundle.install_relative));
    const matches = findBundleDirectories(expanded, expectedNames);
    for (const bundle of aaxManifest.bundles) {
      const name = path.basename(bundle.install_relative);
      const candidates = matches.get(name) || [];
      if (candidates.length !== 1) {
        throw new Error(`expanded pkg must contain exactly one ${name}; found ${candidates.length}`);
      }
      verifyAaxBundleCopy({
        sourcePath: bundle.sourcePath,
        destinationPath: candidates[0],
        spec: bundle,
        version: VERSION,
        sourceId: releaseIdentity.commit,
        sourceState: 'clean source',
        requireKimera: true,
        requireNativeOnly: true,
      });
    }
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
}

function buildPackage() {
  if (process.argv.includes('--help') || process.argv.includes('-h')) {
    console.log(usage());
    return;
  }
  const unknown = process.argv.slice(2).filter((arg) => arg !== '--with-aax');
  if (unknown.length > 0) throw new Error(`unknown option: ${unknown[0]}`);

  const releaseIdentity = SKIP_SIGN
    ? readReleaseSourceIdentity({ root: ROOT })
    : requireCleanReleaseSource({ root: ROOT });
  const aaxNotarization = WITH_AAX
    ? verifyMacAaxNotarizationReceipt({
      root: ROOT,
      artifactDir: aaxManifest.defaultBuildRoot,
      keychainProfile: NOTARY_PROFILE,
      online: true,
    })
    : null;
  verifyShipBundleContract();
  for (const bundle of bundles) verifySourceBundle(bundle, releaseIdentity);
  const identity = SKIP_SIGN ? null : findInstallerIdentity();
  if (identity) log(`using Developer ID Installer identity ${identity}`);
  if (SKIP_SIGN) log('building unsigned smoke package; do not upload this file');

  fs.mkdirSync(RELEASE_DIR, { recursive: true });
  const workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-pkg-'));
  const payloadRoot = path.join(workDir, 'payload');
  const scriptsDir = path.join(workDir, 'scripts');
  fs.mkdirSync(payloadRoot, { recursive: true });
  fs.mkdirSync(scriptsDir, { recursive: true });
  writePreinstall(path.join(scriptsDir, 'preinstall'));

  for (const bundle of bundles) {
    const destination = path.join(payloadRoot, bundle.install_relative);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    run('ditto', [bundle.sourcePath, destination]);
    if (bundle.kind === 'aax') {
      verifyAaxBundleCopy({
        sourcePath: bundle.sourcePath,
        destinationPath: destination,
        spec: bundle,
        version: VERSION,
        sourceId: releaseIdentity.commit,
        sourceState: 'clean source',
        requireKimera: true,
        requireNativeOnly: true,
      });
    }
  }
  verifyShipBundleContract(payloadRoot);

  const componentPkg = path.join(workDir, 'KirinHyphaPlugins.pkg');
  run('pkgbuild', [
    '--root', payloadRoot,
    '--scripts', scriptsDir,
    '--identifier', COMPONENT_ID,
    '--version', VERSION,
    '--install-location', '/',
    componentPkg,
  ]);

  fs.rmSync(PACKAGE_PATH, { force: true });
  const productArgs = ['--identifier', PRODUCT_ID, '--version', VERSION];
  if (identity) productArgs.push('--sign', identity, '--timestamp');
  productArgs.push('--package', componentPkg, PACKAGE_PATH);
  run('productbuild', productArgs);

  let pkgNotarization = null;
  if (!SKIP_NOTARIZE) {
    const submitted = run('xcrun', [
      'notarytool', 'submit', PACKAGE_PATH,
      '--keychain-profile', NOTARY_PROFILE,
      '--wait', '--output-format', 'json',
    ], { capture: true });
    pkgNotarization = parseNotarytoolAccepted(submitted, 'installer pkg notarytool submit');
    const confirmed = run('xcrun', [
      'notarytool', 'info', pkgNotarization.id,
      '--keychain-profile', NOTARY_PROFILE,
      '--output-format', 'json',
    ], { capture: true });
    const pkgConfirmation = parseNotarytoolAccepted(confirmed, 'installer pkg notarytool info');
    if (pkgConfirmation.id !== pkgNotarization.id) {
      throw new Error('installer pkg notarytool confirmation id changed');
    }
    pkgNotarization = pkgConfirmation;
    run('xcrun', ['stapler', 'staple', PACKAGE_PATH]);
    run('xcrun', ['stapler', 'validate', PACKAGE_PATH]);
  }

  run('pkgutil', ['--payload-files', PACKAGE_PATH], { capture: true });
  verifyPackagedAax(PACKAGE_PATH, releaseIdentity);
  if (!SKIP_SIGN) {
    run('pkgutil', ['--check-signature', PACKAGE_PATH]);
    if (!SKIP_NOTARIZE) run('spctl', ['-a', '-vv', '-t', 'install', PACKAGE_PATH]);
  }

  const size = fs.statSync(PACKAGE_PATH).size;
  const sidecar = {
    schema: 'kirin-hypha-pkg-artifact-v1',
    product: 'Kirin Hypha',
    version: VERSION,
    fileName: path.basename(PACKAGE_PATH),
    path: path.relative(ROOT, PACKAGE_PATH),
    size,
    sha512: sha512Base64(PACKAGE_PATH),
    sha256: sha256Hex(PACKAGE_PATH),
    lsDisplaySize: formatMiB(size),
    signed: !SKIP_SIGN,
    notarized: !SKIP_NOTARIZE,
    notarization: pkgNotarization ? {
      submissionId: pkgNotarization.id,
      status: pkgNotarization.status,
    } : null,
    aaxIncluded: WITH_AAX,
    aaxNotarization: aaxNotarization ? {
      submissionId: aaxNotarization.receipt.submission.id,
      status: aaxNotarization.receipt.submission.status,
      receiptSha256: sha256Hex(aaxNotarization.receiptPath),
    } : null,
    source: {
      commit: releaseIdentity.commit,
      bNumber: releaseIdentity.bNumber,
    },
    generatedAt: new Date().toISOString(),
  };
  fs.writeFileSync(`${PACKAGE_PATH}.json`, `${JSON.stringify(sidecar, null, 2)}\n`);
  fs.writeFileSync(`${PACKAGE_PATH}.sha256`, `${sidecar.sha256}  ${sidecar.fileName}\n`);
  fs.rmSync(workDir, { recursive: true, force: true });

  log(`wrote ${PACKAGE_PATH}`);
  log(`size ${size} (${sidecar.lsDisplaySize})`);
  log(`sha512 ${sidecar.sha512}`);
  log(`sha256 ${sidecar.sha256}`);
}

try {
  buildPackage();
} catch (error) {
  console.error(`[build-kirin-hypha-pkg] ERROR: ${error.message}`);
  process.exit(1);
}
