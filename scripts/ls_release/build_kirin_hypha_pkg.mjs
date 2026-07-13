#!/usr/bin/env node
import childProcess from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

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

const bundles = [
  {
    label: 'PRE AU',
    src: 'juce_shell/build-universal/KirinHyphaPRE_artefacts/Release/AU/Kirin Hypha PRE.component',
    payload: 'Library/Audio/Plug-Ins/Components/Kirin Hypha PRE.component',
    binary: 'Kirin Hypha PRE',
    displayName: 'PRE Kirin Hypha',
    kind: 'component',
  },
  {
    label: 'POST AU',
    src: 'juce_shell/build-universal/KirinHyphaPOST_artefacts/Release/AU/Kirin Hypha POST.component',
    payload: 'Library/Audio/Plug-Ins/Components/Kirin Hypha POST.component',
    binary: 'Kirin Hypha POST',
    displayName: 'POST Kirin Hypha',
    kind: 'component',
  },
  {
    label: 'PRE VST3',
    src: 'target/bundled/PRE Kirin Hypha.vst3',
    payload: 'Library/Audio/Plug-Ins/VST3/PRE Kirin Hypha.vst3',
    binary: 'PRE Kirin Hypha',
    displayName: 'PRE Kirin Hypha',
    kind: 'vst3',
  },
  {
    label: 'POST VST3',
    src: 'target/bundled/POST Kirin Hypha.vst3',
    payload: 'Library/Audio/Plug-Ins/VST3/POST Kirin Hypha.vst3',
    binary: 'POST Kirin Hypha',
    displayName: 'POST Kirin Hypha',
    kind: 'vst3',
  },
];

function usage() {
  return `Usage:
  node scripts/ls_release/build_kirin_hypha_pkg.mjs

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

function verifySourceBundle(bundle) {
  const source = path.join(ROOT, bundle.src);
  if (!fs.existsSync(source)) throw new Error(`${bundle.label} missing: ${bundle.src}`);
  const binary = path.join(source, 'Contents/MacOS', bundle.binary);
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
  verifyDisplayMetadata(bundle, plist, binary);
  if (bundle.kind === 'component') {
    const usage = run('plutil', ['-p', plist], { capture: true });
    if (!usage.includes('temporary-exception.files.all.read-write')) {
      throw new Error(`${bundle.label} AU resourceUsage missing files.all`);
    }
    if (usage.includes('network.client')) throw new Error(`${bundle.label} AU resourceUsage has network.client`);
  }
  run('codesign', ['--verify', '--deep', '--strict', '--verbose=2', source]);
  run('codesign', ['--verify', '--deep', '--strict', '--check-notarization', '--verbose=2', source]);
}

function verifyDisplayMetadata(bundle, plist, binary) {
  if (bundle.kind === 'component') {
    const actual = plistValue(plist, 'AudioComponents:0:name');
    const expected = `Kirin: ${bundle.displayName}`;
    if (actual !== expected) {
      throw new Error(`${bundle.label} AudioComponents:0:name=${actual}, expected ${expected}`);
    }
    return;
  }

  for (const key of ['CFBundleDisplayName', 'CFBundleName']) {
    const actual = plistValue(plist, key);
    if (actual !== bundle.displayName) {
      throw new Error(`${bundle.label} ${key}=${actual}, expected ${bundle.displayName}. Run cargo run -p xtask -- stamp-egui-version after bundling.`);
    }
  }
  const binaryStrings = run('strings', [binary], { capture: true });
  if (!binaryStrings.includes(bundle.displayName)) {
    throw new Error(`${bundle.label} binary does not contain display name ${bundle.displayName}`);
  }
}

function writePreinstall(filePath) {
  const script = `#!/bin/sh
set -eu

remove_bundle() {
  if [ -e "$1" ]; then
    rm -rf "$1"
  fi
}

remove_bundle "/Library/Audio/Plug-Ins/Components/Kirin Hypha PRE.component"
remove_bundle "/Library/Audio/Plug-Ins/Components/Kirin Hypha POST.component"
remove_bundle "/Library/Audio/Plug-Ins/VST3/Kirin Hypha PRE.vst3"
remove_bundle "/Library/Audio/Plug-Ins/VST3/Kirin Hypha POST.vst3"
remove_bundle "/Library/Audio/Plug-Ins/VST3/PRE Kirin Hypha.vst3"
remove_bundle "/Library/Audio/Plug-Ins/VST3/POST Kirin Hypha.vst3"

for home in /Users/*; do
  [ -d "$home" ] || continue
  remove_bundle "$home/Library/Audio/Plug-Ins/Components/Kirin Hypha PRE.component"
  remove_bundle "$home/Library/Audio/Plug-Ins/Components/Kirin Hypha POST.component"
  remove_bundle "$home/Library/Audio/Plug-Ins/VST3/Kirin Hypha PRE.vst3"
  remove_bundle "$home/Library/Audio/Plug-Ins/VST3/Kirin Hypha POST.vst3"
  remove_bundle "$home/Library/Audio/Plug-Ins/VST3/PRE Kirin Hypha.vst3"
  remove_bundle "$home/Library/Audio/Plug-Ins/VST3/POST Kirin Hypha.vst3"
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

function buildPackage() {
  if (process.argv.includes('--help') || process.argv.includes('-h')) {
    console.log(usage());
    return;
  }

  for (const bundle of bundles) verifySourceBundle(bundle);
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
    const destination = path.join(payloadRoot, bundle.payload);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    run('ditto', [path.join(ROOT, bundle.src), destination]);
  }

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

  if (!SKIP_NOTARIZE) {
    run('xcrun', ['notarytool', 'submit', PACKAGE_PATH, '--keychain-profile', NOTARY_PROFILE, '--wait']);
    run('xcrun', ['stapler', 'staple', PACKAGE_PATH]);
    run('xcrun', ['stapler', 'validate', PACKAGE_PATH]);
  }

  run('pkgutil', ['--payload-files', PACKAGE_PATH], { capture: true });
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
