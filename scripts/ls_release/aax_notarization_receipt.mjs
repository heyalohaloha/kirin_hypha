#!/usr/bin/env node
import childProcess from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { verifyAaxBundle } from './aax_bundle_verify.mjs';
import { loadMacAaxBundleManifest, PROJECT_ROOT } from './kirin_hypha_aax_bundles.mjs';
import { requireCleanReleaseSource } from './release_source_identity.mjs';

const MODULE_PATH = fileURLToPath(import.meta.url);
export const AAX_NOTARIZATION_RECEIPT_NAME = 'kirin-hypha-macos-aax-notarization.json';
export const AAX_NOTARIZATION_SCHEMA = 'kirin-hypha-macos-aax-notarization-v1';

function run(command, args, { cwd = PROJECT_ROOT } = {}) {
  const result = childProcess.spawnSync(command, args, {
    cwd,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  if (result.error) throw new Error(`${command} could not start: ${result.error.message}`);
  if (result.status !== 0) {
    throw new Error(`${command} failed with status ${result.status}`);
  }
  return { stdout: result.stdout || '', stderr: result.stderr || '' };
}

function sha256(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

function readVersion(root) {
  const source = fs.readFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'utf8');
  const version = source.match(/^version\s*=\s*"([^"]+)"/m)?.[1];
  if (!version) throw new Error('Hypha version is missing');
  return version;
}

export function parseNotarytoolAccepted(output, label = 'AAX notarization') {
  let response;
  try {
    response = JSON.parse(String(output));
  } catch (error) {
    throw new Error(`${label} did not return valid JSON: ${error.message}`);
  }
  if (response.status !== 'Accepted') {
    throw new Error(`${label} status is ${response.status || 'missing'}, expected Accepted`);
  }
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(response.id || '')) {
    throw new Error(`${label} submission id is missing or invalid`);
  }
  return { id: response.id.toLowerCase(), status: response.status };
}

function inspectReleaseBundles(root, artifactRoot, source, version) {
  const manifest = loadMacAaxBundleManifest({ root, buildRoot: artifactRoot });
  return manifest.bundles.map((bundle) => {
    const verified = verifyAaxBundle({
      bundlePath: bundle.sourcePath,
      executableName: bundle.executable_name,
      bundleIdentifier: bundle.bundle_identifier,
      version,
      sourceId: source.commit,
      sourceState: 'clean source',
      requireKimera: true,
      requireNativeOnly: true,
    });
    return {
      role: bundle.role,
      bundle: path.basename(bundle.sourcePath),
      binary_sha256: verified.binarySha256,
      apple_cdhash: verified.apple.cdhash,
      apple_authority: verified.apple.authority,
      apple_team_id: verified.apple.teamId,
      pace_signer: verified.pace.signerName,
      pace_signer_guid: verified.pace.signerGuid,
      pace_publisher_id: verified.pace.publisherId,
      source_path: bundle.sourcePath,
      executable_name: bundle.executable_name,
      bundle_identifier: bundle.bundle_identifier,
    };
  });
}

function publicBundleRecord(record) {
  const publicRecord = { ...record };
  delete publicRecord.source_path;
  delete publicRecord.executable_name;
  delete publicRecord.bundle_identifier;
  return publicRecord;
}

const NOTARIZED_BUNDLE_FIELDS = [
  'role',
  'bundle',
  'binary_sha256',
  'apple_cdhash',
  'apple_authority',
  'apple_team_id',
  'pace_signer',
  'pace_signer_guid',
  'pace_publisher_id',
];

export function validateAaxNotarizationReceipt(receipt, expected) {
  if (receipt?.schema !== AAX_NOTARIZATION_SCHEMA) {
    throw new Error(`unsupported AAX notarization receipt: ${receipt?.schema || 'missing'}`);
  }
  if (receipt.source?.commit !== expected.source.commit
      || receipt.source?.b_number !== expected.source.bNumber
      || receipt.source?.state !== 'clean source') {
    throw new Error('AAX notarization receipt source does not match the clean release source');
  }
  if (receipt.product?.name !== 'Kirin Hypha'
      || receipt.product?.platform !== 'macos-universal'
      || receipt.product?.format !== 'AAX'
      || receipt.product?.version !== expected.version) {
    throw new Error('AAX notarization receipt product identity does not match');
  }
  parseNotarytoolAccepted(JSON.stringify(receipt.submission), 'recorded AAX notarization');
  const expectedArchiveName = `Kirin-Hypha-${expected.version}-macOS-AAX.zip`;
  if (receipt.archive?.file_name !== expectedArchiveName
      || !/^[0-9a-f]{64}$/.test(receipt.archive?.sha256 || '')
      || !Number.isSafeInteger(receipt.archive?.size_bytes)
      || receipt.archive.size_bytes <= 0) {
    throw new Error('AAX notarization receipt archive identity is invalid');
  }
  if (!Number.isFinite(Date.parse(receipt.generated_at || ''))) {
    throw new Error('AAX notarization receipt generation time is invalid');
  }
  if (!Array.isArray(receipt.bundles) || receipt.bundles.length !== 2) {
    throw new Error('AAX notarization receipt must contain exactly PRE and POST');
  }
  for (const role of ['PRE', 'POST']) {
    const actual = receipt.bundles.find((bundle) => bundle?.role === role);
    const wanted = expected.bundles.find((bundle) => bundle.role === role);
    if (!actual || !wanted || NOTARIZED_BUNDLE_FIELDS.some((field) => actual[field] !== wanted[field])) {
      throw new Error(`${role} AAX no longer matches its accepted notarization submission`);
    }
  }
  return receipt;
}

function releaseContext({ root = PROJECT_ROOT, artifactDir = 'build-aax-universal' } = {}) {
  const resolvedRoot = path.resolve(root);
  const artifactRoot = path.resolve(resolvedRoot, artifactDir);
  const source = requireCleanReleaseSource({ root: resolvedRoot });
  const version = readVersion(resolvedRoot);
  const bundles = inspectReleaseBundles(resolvedRoot, artifactRoot, source, version);
  return { root: resolvedRoot, artifactRoot, source, version, bundles };
}

export function verifyMacAaxNotarizationReceipt(options = {}) {
  const context = releaseContext(options);
  const receiptPath = path.join(context.artifactRoot, AAX_NOTARIZATION_RECEIPT_NAME);
  if (!fs.statSync(receiptPath, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`AAX notarization receipt is missing: ${receiptPath}`);
  }
  const receipt = JSON.parse(fs.readFileSync(receiptPath, 'utf8'));
  validateAaxNotarizationReceipt(receipt, context);
  if (options.online) {
    if (process.platform !== 'darwin') throw new Error('online AAX notarization verification requires macOS');
    const keychainProfile = options.keychainProfile
      || process.env.KIRIN_NOTARY_PROFILE
      || 'kirin-notarize';
    const confirmed = run('xcrun', [
      'notarytool', 'info', receipt.submission.id,
      '--keychain-profile', keychainProfile,
      '--output-format', 'json',
    ], { cwd: context.root });
    const online = parseNotarytoolAccepted(confirmed.stdout, 'online AAX notarytool info');
    if (online.id !== receipt.submission.id) {
      throw new Error('online AAX notarization submission id does not match the receipt');
    }
  }
  return { receipt, receiptPath };
}

export function submitMacAaxForNotarization({
  root = PROJECT_ROOT,
  artifactDir = 'build-aax-universal',
  keychainProfile = process.env.KIRIN_NOTARY_PROFILE || 'kirin-notarize',
} = {}) {
  if (process.platform !== 'darwin') throw new Error('macOS AAX notarization requires macOS');
  if (!keychainProfile) throw new Error('AAX notarization requires a notarytool keychain profile');
  const context = releaseContext({ root, artifactDir });
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-aax-notary-'));
  const archiveRoot = path.join(temporary, `Kirin Hypha ${context.version} AAX`);
  const archivePath = path.join(temporary, `Kirin-Hypha-${context.version}-macOS-AAX.zip`);
  try {
    fs.mkdirSync(archiveRoot, { recursive: true });
    for (const bundle of context.bundles) {
      const staged = path.join(archiveRoot, bundle.bundle);
      run('ditto', [bundle.source_path, staged], { cwd: context.root });
      const copied = verifyAaxBundle({
        bundlePath: staged,
        executableName: bundle.executable_name,
        bundleIdentifier: bundle.bundle_identifier,
        version: context.version,
        sourceId: context.source.commit,
        sourceState: 'clean source',
        requireKimera: true,
        requireNativeOnly: true,
      });
      if (copied.binarySha256 !== bundle.binary_sha256
          || copied.apple.cdhash !== bundle.apple_cdhash) {
        throw new Error(`${bundle.role} AAX changed while staging the notarization archive`);
      }
    }
    run('ditto', [
      '-c', '-k', '--norsrc', '--noqtn', '--keepParent', archiveRoot, archivePath,
    ], { cwd: context.root });
    const archive = {
      file_name: path.basename(archivePath),
      size_bytes: fs.statSync(archivePath).size,
      sha256: sha256(archivePath),
    };
    const submitted = run('xcrun', [
      'notarytool', 'submit', archivePath,
      '--keychain-profile', keychainProfile,
      '--wait', '--output-format', 'json',
    ], { cwd: context.root });
    const submission = parseNotarytoolAccepted(submitted.stdout, 'AAX notarytool submit');
    const confirmed = run('xcrun', [
      'notarytool', 'info', submission.id,
      '--keychain-profile', keychainProfile,
      '--output-format', 'json',
    ], { cwd: context.root });
    const confirmation = parseNotarytoolAccepted(confirmed.stdout, 'AAX notarytool info');
    if (confirmation.id !== submission.id) throw new Error('AAX notarytool confirmation id changed');
    const receipt = {
      schema: AAX_NOTARIZATION_SCHEMA,
      generated_at: new Date().toISOString(),
      source: {
        commit: context.source.commit,
        b_number: context.source.bNumber,
        state: 'clean source',
      },
      product: {
        name: 'Kirin Hypha',
        version: context.version,
        platform: 'macos-universal',
        format: 'AAX',
      },
      submission: confirmation,
      archive,
      bundles: context.bundles.map(publicBundleRecord),
    };
    validateAaxNotarizationReceipt(receipt, context);
    const receiptPath = path.join(context.artifactRoot, AAX_NOTARIZATION_RECEIPT_NAME);
    fs.writeFileSync(receiptPath, `${JSON.stringify(receipt, null, 2)}\n`);
    return { receipt, receiptPath };
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
}

function parseArgs(argv) {
  const options = {
    mode: argv[0], artifactDir: 'build-aax-universal', keychainProfile: '', online: false,
  };
  for (let index = 1; index < argv.length; index += 1) {
    if (argv[index] === '--artifact-dir') options.artifactDir = argv[++index] || '';
    else if (argv[index] === '--keychain-profile') options.keychainProfile = argv[++index] || '';
    else if (argv[index] === '--online') options.online = true;
    else if (argv[index] === '--help' || argv[index] === '-h') options.help = true;
    else throw new Error(`unknown argument: ${argv[index]}`);
  }
  return options;
}

function main(argv) {
  const options = parseArgs(argv);
  if (options.help || !['submit', 'verify'].includes(options.mode)) {
    console.log('Usage: node aax_notarization_receipt.mjs submit|verify [--artifact-dir DIR] [--keychain-profile PROFILE] [--online]');
    if (options.help) return;
    throw new Error('mode must be submit or verify');
  }
  const result = options.mode === 'submit'
    ? submitMacAaxForNotarization({
      artifactDir: options.artifactDir,
      keychainProfile: options.keychainProfile || undefined,
    })
    : verifyMacAaxNotarizationReceipt({
      artifactDir: options.artifactDir,
      keychainProfile: options.keychainProfile || undefined,
      online: options.online,
    });
  console.log(`[aax-notarization] ${options.mode} passed: ${result.receiptPath}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === MODULE_PATH) {
  try {
    main(process.argv.slice(2));
  } catch (error) {
    console.error(`[aax-notarization] ERROR: ${error.message}`);
    process.exitCode = 1;
  }
}
