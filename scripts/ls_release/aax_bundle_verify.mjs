#!/usr/bin/env node
import childProcess from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const MODULE_PATH = fileURLToPath(import.meta.url);
export const AAX_APPLE_AUTHORITY = 'Developer ID Application: daisuke nishio (7N8BSMA684)';
export const AAX_APPLE_TEAM_ID = '7N8BSMA684';
export const AAX_PACE_SIGNER_NAME = 'Kirin Mastering';
export const AAX_PACE_PUBLISHER_ID = '0x488b4292';
const DEFAULT_WRAPTOOL_PATHS = [
  '/Applications/PACEAntiPiracy/Eden/Fusion/Versions/6/bin/wraptool',
  '/Applications/PACEAntiPiracy/Eden/Fusion/Current/bin/wraptool',
];
const PACE_DSIG_LINK = 'Contents/Resources/__Pace_Eden/Signatures/codesign.dsig';

function run(command, args, label) {
  const result = childProcess.spawnSync(command, args, {
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  if (result.error) throw new Error(`${label} could not start: ${result.error.message}`);
  if (result.status !== 0) {
    throw new Error(`${label} failed with status ${result.status}`);
  }
  return { stdout: result.stdout || '', stderr: result.stderr || '' };
}

export function parseAaxAppleSignature(output) {
  const lines = String(output).split(/\r?\n/).map((line) => line.trim());
  const authorities = lines
    .filter((line) => line.startsWith('Authority='))
    .map((line) => line.slice('Authority='.length));
  const teamId = lines.find((line) => line.startsWith('TeamIdentifier='))
    ?.slice('TeamIdentifier='.length);
  const timestamp = lines.find((line) => line.startsWith('Timestamp='))
    ?.slice('Timestamp='.length);
  const cdhash = lines.find((line) => line.startsWith('CDHash='))?.slice('CDHash='.length);
  if (authorities[0] !== AAX_APPLE_AUTHORITY) {
    throw new Error(`AAX Apple authority is not ${AAX_APPLE_AUTHORITY}`);
  }
  if (teamId !== AAX_APPLE_TEAM_ID) {
    throw new Error(`AAX bundle is not signed by required Apple team ${AAX_APPLE_TEAM_ID}`);
  }
  if (!timestamp) throw new Error('AAX secure timestamp is missing');
  if (!/^[0-9a-f]+$/i.test(cdhash || '')) throw new Error('AAX Apple CDHash is missing');
  return { authority: authorities[0], teamId, timestamp, cdhash: cdhash.toLowerCase() };
}

export function parseAaxPaceSignature(output) {
  const text = String(output);
  const signerName = text.match(/^\s*Signer name:\s*(.+?)\s*$/m)?.[1];
  const signerGuid = text.match(/^\s*Signer GUID:\s*(.+?)\s*$/m)?.[1];
  const publisherId = text.match(/^\s*Signer PublisherId:\s*(.+?)\s*$/m)?.[1];
  if (signerName !== AAX_PACE_SIGNER_NAME) {
    throw new Error(`AAX PACE signer is not ${AAX_PACE_SIGNER_NAME}`);
  }
  if (publisherId?.toLowerCase() !== AAX_PACE_PUBLISHER_ID.toLowerCase()) {
    throw new Error(`AAX PACE PublisherId is not ${AAX_PACE_PUBLISHER_ID}`);
  }
  if (!signerGuid) throw new Error('AAX PACE signer GUID is missing');
  return { signerName, signerGuid, publisherId: AAX_PACE_PUBLISHER_ID };
}

function plistValue(plist, key) {
  return run('/usr/libexec/PlistBuddy', ['-c', `Print :${key}`, plist], `read ${key}`).stdout.trim();
}

export function validateAaxBuildIdentity(actual, expected) {
  if (!/^[0-9a-f]{40}$/.test(expected.sourceId || '')) {
    throw new Error(`invalid expected AAX source commit: ${expected.sourceId || ''}`);
  }
  if (actual.sourceId !== expected.sourceId) {
    throw new Error(`AAX source id ${actual.sourceId} does not match ${expected.sourceId}`);
  }
  if (actual.sourceState !== expected.sourceState) {
    throw new Error(`AAX source state ${actual.sourceState} does not match ${expected.sourceState}`);
  }
  if (expected.requireKimera && actual.aaxBuildMode !== 'release') {
    throw new Error(`AAX release bundle build mode is ${actual.aaxBuildMode}`);
  }
  if (expected.requireDiagnostic && actual.aaxBuildMode !== 'diagnostic') {
    throw new Error(`AAX diagnostic bundle build mode is ${actual.aaxBuildMode}`);
  }
  if (expected.requireKimera && actual.kimeraEmbedded !== 'true') {
    throw new Error('AAX distribution bundle does not embed the licensed Kimera font');
  }
  if (expected.requireNativeOnly && actual.audioSuiteEnabled !== 'false') {
    throw new Error('AAX distribution bundle still exposes AudioSuite');
  }
}

function resolveWraptool() {
  const candidates = [process.env.KIRIN_AAX_WRAPTOOL, ...DEFAULT_WRAPTOOL_PATHS].filter(Boolean);
  const match = candidates.find((candidate) => {
    try {
      fs.accessSync(candidate, fs.constants.X_OK);
      return true;
    } catch {
      return false;
    }
  });
  if (!match) {
    throw new Error('PACE wraptool is required to verify an AAX distribution bundle');
  }
  return match;
}

function sha256(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

function symlinkInventory(bundlePath) {
  const out = [];
  function visit(directory) {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const absolute = path.join(directory, entry.name);
      if (entry.isSymbolicLink()) {
        const relative = path.relative(bundlePath, absolute);
        const target = fs.readlinkSync(absolute);
        const resolved = path.resolve(path.dirname(absolute), target);
        const rootWithSeparator = `${path.resolve(bundlePath)}${path.sep}`;
        if (resolved !== path.resolve(bundlePath) && !resolved.startsWith(rootWithSeparator)) {
          throw new Error(`AAX symlink escapes its bundle: ${relative}`);
        }
        if (!fs.existsSync(resolved)) throw new Error(`AAX symlink target is missing: ${relative}`);
        out.push(`${relative}\t${target}`);
      } else if (entry.isDirectory()) {
        visit(absolute);
      }
    }
  }
  visit(bundlePath);
  return out.sort();
}

export function verifyAaxBundle({
  bundlePath,
  executableName,
  bundleIdentifier,
  version,
  sourceId,
  sourceState,
  requireKimera = false,
  requireNativeOnly = false,
  requireDiagnostic = false,
}) {
  if (!fs.statSync(bundlePath, { throwIfNoEntry: false })?.isDirectory()) {
    throw new Error(`AAX bundle missing: ${bundlePath}`);
  }
  const plist = path.join(bundlePath, 'Contents/Info.plist');
  const binary = path.join(bundlePath, 'Contents/MacOS', executableName);
  if (!fs.statSync(binary, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`AAX executable missing: ${binary}`);
  }
  const expected = {
    CFBundleExecutable: executableName,
    CFBundleIdentifier: bundleIdentifier,
    CFBundleDisplayName: executableName,
    CFBundleName: executableName,
    CFBundleShortVersionString: version,
    CFBundleVersion: version,
    CFBundlePackageType: 'TDMw',
  };
  for (const [key, value] of Object.entries(expected)) {
    const actual = plistValue(plist, key);
    if (actual !== value) throw new Error(`AAX ${key}=${actual}, expected ${value}`);
  }
  if (sourceId || sourceState || requireKimera || requireNativeOnly || requireDiagnostic) {
    validateAaxBuildIdentity({
      sourceId: plistValue(plist, 'KirinHyphaSourceID'),
      sourceState: plistValue(plist, 'KirinHyphaSourceState'),
      kimeraEmbedded: plistValue(plist, 'KirinHyphaKimeraEmbedded'),
      audioSuiteEnabled: plistValue(plist, 'KirinHyphaAudioSuiteEnabled'),
      aaxBuildMode: plistValue(plist, 'KirinHyphaAaxBuildMode'),
    }, {
      sourceId,
      sourceState,
      requireKimera,
      requireNativeOnly,
      requireDiagnostic,
    });
  }

  const archs = run('lipo', ['-archs', binary], 'AAX lipo verification').stdout.trim().split(/\s+/);
  if (!archs.includes('x86_64') || !archs.includes('arm64')) {
    throw new Error(`AAX bundle is not universal: ${archs.join(' ')}`);
  }
  run('codesign', ['--verify', '--deep', '--strict', '--verbose=2', bundlePath], 'AAX codesign verification');
  const appleOutput = run('codesign', ['-dvvv', bundlePath], 'AAX signing identity inspection').stderr;
  const apple = parseAaxAppleSignature(appleOutput);
  const links = symlinkInventory(bundlePath);
  if (!links.some((entry) => entry.startsWith(`${PACE_DSIG_LINK}\t`))) {
    throw new Error(`AAX PACE compatibility signature link missing: ${PACE_DSIG_LINK}`);
  }
  const paceResult = run(resolveWraptool(), ['verify', '--in', bundlePath], 'AAX PACE signature verification');
  const pace = parseAaxPaceSignature(`${paceResult.stdout}\n${paceResult.stderr}`);
  return { binary, binarySha256: sha256(binary), symlinks: links, apple, pace };
}

export function verifyAaxBundleCopy({
  sourcePath,
  destinationPath,
  spec,
  version,
  sourceId,
  sourceState,
  requireKimera = false,
  requireNativeOnly = false,
  requireDiagnostic = false,
}) {
  const options = {
    executableName: spec.executable_name,
    bundleIdentifier: spec.bundle_identifier,
    version,
  };
  const source = verifyAaxBundle({
    bundlePath: sourcePath,
    ...options,
    sourceId,
    sourceState,
    requireKimera,
    requireNativeOnly,
    requireDiagnostic,
  });
  const destination = verifyAaxBundle({
    bundlePath: destinationPath,
    ...options,
    sourceId,
    sourceState,
    requireKimera,
    requireNativeOnly,
    requireDiagnostic,
  });
  if (source.binarySha256 !== destination.binarySha256) {
    throw new Error(`AAX executable changed during copy: ${spec.role}`);
  }
  if (JSON.stringify(source.symlinks) !== JSON.stringify(destination.symlinks)) {
    throw new Error(`AAX symlink inventory changed during copy: ${spec.role}`);
  }
  if (source.apple.cdhash !== destination.apple.cdhash) {
    throw new Error(`AAX Apple CDHash changed during copy: ${spec.role}`);
  }
}

function parseArgs(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === '--bundle') options.bundlePath = argv[++index];
    else if (arg === '--source') options.sourcePath = argv[++index];
    else if (arg === '--executable') options.executableName = argv[++index];
    else if (arg === '--identifier') options.bundleIdentifier = argv[++index];
    else if (arg === '--version') options.version = argv[++index];
    else if (arg === '--source-id') options.sourceId = argv[++index];
    else if (arg === '--source-state') options.sourceState = argv[++index];
    else if (arg === '--require-kimera') options.requireKimera = true;
    else if (arg === '--require-native-only') options.requireNativeOnly = true;
    else if (arg === '--require-diagnostic') options.requireDiagnostic = true;
    else if (arg === '--help' || arg === '-h') options.help = true;
    else throw new Error(`unknown argument: ${arg}`);
  }
  return options;
}

function runCli(argv) {
  const options = parseArgs(argv);
  if (options.help) {
    console.log('Usage: node aax_bundle_verify.mjs --bundle PATH --executable NAME --identifier ID --version VERSION [--source PATH] [--source-id ID --source-state STATE --require-kimera --require-native-only --require-diagnostic]');
    return;
  }
  for (const key of ['bundlePath', 'executableName', 'bundleIdentifier', 'version']) {
    if (!options[key]) throw new Error(`missing required option: ${key}`);
  }
  const spec = {
    role: options.executableName,
    executable_name: options.executableName,
    bundle_identifier: options.bundleIdentifier,
  };
  if (options.sourcePath) {
    verifyAaxBundleCopy({
      sourcePath: options.sourcePath,
      destinationPath: options.bundlePath,
      spec,
      version: options.version,
      sourceId: options.sourceId,
      sourceState: options.sourceState,
      requireKimera: options.requireKimera,
      requireNativeOnly: options.requireNativeOnly,
      requireDiagnostic: options.requireDiagnostic,
    });
  } else {
    verifyAaxBundle({
      bundlePath: options.bundlePath,
      executableName: options.executableName,
      bundleIdentifier: options.bundleIdentifier,
      version: options.version,
      sourceId: options.sourceId,
      sourceState: options.sourceState,
      requireKimera: options.requireKimera,
      requireNativeOnly: options.requireNativeOnly,
      requireDiagnostic: options.requireDiagnostic,
    });
  }
  console.log(`[aax-bundle-verify] OK: ${options.bundlePath}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === MODULE_PATH) {
  try {
    runCli(process.argv.slice(2));
  } catch (error) {
    console.error(`[aax-bundle-verify] ERROR: ${error.message}`);
    process.exitCode = 1;
  }
}
