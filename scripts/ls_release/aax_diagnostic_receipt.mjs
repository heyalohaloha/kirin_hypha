#!/usr/bin/env node
import crypto from 'node:crypto';
import childProcess from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { loadMacAaxBundleManifest, PROJECT_ROOT } from './kirin_hypha_aax_bundles.mjs';
import { verifyAaxBundle } from './aax_bundle_verify.mjs';
import { readReleaseSourceIdentity } from './release_source_identity.mjs';

const MODULE_PATH = fileURLToPath(import.meta.url);
const RECEIPT_NAME = 'kirin-hypha-macos-aax-diagnostic.json';

function run(command, args, label) {
  const result = childProcess.spawnSync(command, args, {
    cwd: PROJECT_ROOT,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  if (result.status !== 0) {
    const detail = `${result.stdout || ''}${result.stderr || ''}`.trim();
    throw new Error(`${label} failed${detail ? `: ${detail}` : ''}`);
  }
  return (result.stdout || '').trim();
}

function sha256(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

function plistValue(plist, key) {
  return run('/usr/libexec/PlistBuddy', ['-c', `Print :${key}`, plist], `read ${key}`);
}

function inspectBundle(bundle, source, { version, signed }) {
  const plist = path.join(bundle.sourcePath, 'Contents', 'Info.plist');
  const binary = path.join(bundle.sourcePath, 'Contents', 'MacOS', bundle.executable_name);
  if (!fs.statSync(bundle.sourcePath, { throwIfNoEntry: false })?.isDirectory()) {
    throw new Error(`${bundle.role} diagnostic AAX bundle is missing: ${bundle.sourcePath}`);
  }
  if (!fs.statSync(binary, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`${bundle.role} diagnostic AAX executable is missing: ${binary}`);
  }
  if (plistValue(plist, 'KirinHyphaSourceID') !== source.commit) {
    throw new Error(`${bundle.role} diagnostic AAX source commit does not match the receipt`);
  }
  if (plistValue(plist, 'KirinHyphaSourceState') !== source.sourceState) {
    throw new Error(`${bundle.role} diagnostic AAX source state does not match the receipt`);
  }
  if (plistValue(plist, 'KirinHyphaKimeraEmbedded') !== 'false') {
    throw new Error(`${bundle.role} diagnostic AAX unexpectedly embeds Kimera`);
  }
  if (plistValue(plist, 'KirinHyphaAudioSuiteEnabled') !== 'false') {
    throw new Error(`${bundle.role} diagnostic AAX exposes AudioSuite`);
  }
  if (plistValue(plist, 'KirinHyphaAaxBuildMode') !== 'diagnostic') {
    throw new Error(`${bundle.role} diagnostic AAX mode marker is missing`);
  }
  let binarySha256;
  if (signed) {
    const verified = verifyAaxBundle({
      bundlePath: bundle.sourcePath,
      executableName: bundle.executable_name,
      bundleIdentifier: bundle.bundle_identifier,
      version,
      sourceId: source.commit,
      sourceState: source.sourceState,
      requireNativeOnly: true,
      requireDiagnostic: true,
    });
    binarySha256 = verified.binarySha256;
  }
  const architectures = run('lipo', ['-archs', binary], `${bundle.role} architecture inspection`)
    .split(/\s+/)
    .filter(Boolean)
    .sort();
  if (architectures.join(' ') !== 'arm64 x86_64') {
    throw new Error(`${bundle.role} diagnostic AAX must be Universal: ${architectures.join(' ')}`);
  }
  return {
    role: bundle.role,
    bundle: path.basename(bundle.sourcePath),
    executable: bundle.executable_name,
    size_bytes: fs.statSync(binary).size,
    sha256: binarySha256 || sha256(binary),
    architectures,
  };
}

export function writeMacAaxDiagnosticReceipt({
  artifactDir,
  signed = false,
  root = PROJECT_ROOT,
} = {}) {
  if (process.platform !== 'darwin') {
    throw new Error('macOS AAX diagnostic receipts must be written on macOS');
  }
  const resolvedRoot = path.resolve(root);
  const artifactRoot = path.resolve(resolvedRoot, artifactDir || 'build-aax-universal');
  const source = readReleaseSourceIdentity({ root: resolvedRoot });
  const manifest = loadMacAaxBundleManifest({ root: resolvedRoot, buildRoot: artifactRoot });
  const version = fs.readFileSync(path.join(resolvedRoot, 'crates/hypha_pre/Cargo.toml'), 'utf8')
    .match(/^version\s*=\s*"([^"]+)"/m)?.[1];
  if (!version) throw new Error('Hypha version is missing');
  const bundles = manifest.bundles.map((bundle) => inspectBundle(bundle, source, { version, signed }));
  const receipt = {
    schema: 'kirin-hypha-macos-aax-diagnostic-v1',
    generated_at: new Date().toISOString(),
    source: {
      commit: source.commit,
      b_number: source.bNumber,
      state: source.sourceState,
    },
    product: {
      name: 'Kirin Hypha',
      platform: 'macos-universal',
      format: 'AAX',
      version,
    },
    diagnostic: {
      kimera_embedded: false,
      native_only: true,
      audio_suite_enabled: false,
      signed,
      pace_verified: signed,
      apple_signed: signed,
      notarized: false,
      distribution_ready: false,
      not_for_distribution: true,
      host_validation_target: signed
        ? 'Pro Tools Ultimate or other explicitly permitted local diagnostic host'
        : 'Pro Tools Developer or other explicitly permitted diagnostic host',
    },
    bundles,
  };
  if (!receipt.product.version) throw new Error('Hypha version is missing');
  const receiptPath = path.join(artifactRoot, RECEIPT_NAME);
  fs.writeFileSync(receiptPath, `${JSON.stringify(receipt, null, 2)}\n`);
  return { receipt, receiptPath };
}

function main(argv) {
  let artifactDir = 'build-aax-universal';
  let signed = false;
  for (let index = 0; index < argv.length; index += 1) {
    if (argv[index] === '--artifact-dir') {
      artifactDir = argv[++index];
      if (!artifactDir) throw new Error('--artifact-dir requires a value');
    } else if (argv[index] === '--signed') {
      signed = true;
    } else if (argv[index] === '--help' || argv[index] === '-h') {
      console.log('Usage: node aax_diagnostic_receipt.mjs [--artifact-dir DIR] [--signed]');
      return;
    } else {
      throw new Error(`unknown argument: ${argv[index]}`);
    }
  }
  const result = writeMacAaxDiagnosticReceipt({ artifactDir, signed });
  console.log(`[aax-diagnostic] wrote ${result.receiptPath}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === MODULE_PATH) {
  try {
    main(process.argv.slice(2));
  } catch (error) {
    console.error(`[aax-diagnostic] ERROR: ${error.message}`);
    process.exitCode = 1;
  }
}
