#!/usr/bin/env node
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { verifyAaxBundle, verifyAaxBundleCopy } from './aax_bundle_verify.mjs';
import {
  buildTreeManifest,
  evidenceRecord,
  extractAndVerifyArchive,
  manifestBytes,
  materializeVerifiedTree,
  parseNotarytoolLog,
  persistImmutableFile,
  requireBundleRoots,
  runEvidenceCommand,
  sha256File,
  validateNotaryEvidenceLinks,
  validateTreeManifest,
  verifyEvidenceFile,
} from './aax_submission_archive.mjs';
import { loadMacAaxBundleManifest, PROJECT_ROOT } from './kirin_hypha_aax_bundles.mjs';
import { requireCleanReleaseSource } from './release_source_identity.mjs';

const MODULE_PATH = fileURLToPath(import.meta.url);
const LEGACY_SCHEMA = 'kirin-hypha-macos-aax-notarization-v1';
const HASH_UNBOUND_SCHEMA = 'kirin-hypha-macos-aax-notarization-v2';
const EVIDENCE_DIRECTORY = 'aax-notarization';
const CONTENT_MANIFEST_NAME = 'submission-contents.json';
const NOTARY_LOG_NAME = 'notary-log.json';
export const AAX_NOTARIZATION_RECEIPT_NAME = 'kirin-hypha-macos-aax-notarization.json';
export const AAX_NOTARIZATION_SCHEMA = 'kirin-hypha-macos-aax-notarization-v3';

function run(command, args, { cwd = PROJECT_ROOT, runner = runEvidenceCommand } = {}) {
  return runner(command, args, { cwd });
}

function readVersion(root) {
  const source = fs.readFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'utf8');
  const version = source.match(/^version\s*=\s*"([^"]+)"/m)?.[1];
  if (!version) throw new Error('Hypha version is missing');
  return version;
}

function writeJsonAtomic(filePath, value) {
  const temporary = `${filePath}.tmp-${process.pid}-${Date.now()}`;
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  try {
    fs.writeFileSync(temporary, `${JSON.stringify(value, null, 2)}\n`, { flag: 'wx' });
    fs.renameSync(temporary, filePath);
  } finally {
    fs.rmSync(temporary, { force: true });
  }
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
  const accepted = { id: response.id.toLowerCase(), status: response.status };
  if (typeof response.name === 'string' && response.name.length > 0) accepted.name = response.name;
  if (Number.isFinite(Date.parse(response.createdDate || ''))) accepted.created_at = response.createdDate;
  return accepted;
}

function releaseIdentityContext({ root = PROJECT_ROOT, artifactDir = 'build-aax-universal' } = {}) {
  const resolvedRoot = path.resolve(root);
  const artifactRoot = path.resolve(resolvedRoot, artifactDir);
  return {
    root: resolvedRoot,
    artifactRoot,
    source: requireCleanReleaseSource({ root: resolvedRoot }),
    version: readVersion(resolvedRoot),
    manifest: loadMacAaxBundleManifest({ root: resolvedRoot, buildRoot: artifactRoot }),
  };
}

function inspectBundle(context, bundle, bundlePath) {
  const verified = verifyAaxBundle({
    bundlePath,
    executableName: bundle.executable_name,
    bundleIdentifier: bundle.bundle_identifier,
    version: context.version,
    sourceId: context.source.commit,
    sourceState: 'clean source',
    requireKimera: true,
    requireNativeOnly: true,
  });
  return {
    role: bundle.role,
    bundle: path.basename(bundlePath),
    binary_sha256: verified.binarySha256,
    bundle_manifest_sha256: verified.treeManifestSha256,
    apple_cdhash: verified.apple.cdhash,
    apple_authority: verified.apple.authority,
    apple_team_id: verified.apple.teamId,
    pace_signer: verified.pace.signerName,
    pace_signer_guid: verified.pace.signerGuid,
    pace_publisher_id: verified.pace.publisherId,
  };
}

function inspectBundles(context, sourceFor) {
  return context.manifest.bundles.map((bundle) => inspectBundle(context, bundle, sourceFor(bundle)));
}

const NOTARIZED_BUNDLE_FIELDS = [
  'role',
  'bundle',
  'binary_sha256',
  'bundle_manifest_sha256',
  'apple_cdhash',
  'apple_authority',
  'apple_team_id',
  'pace_signer',
  'pace_signer_guid',
  'pace_publisher_id',
];

function validateEvidenceIdentity(record, label) {
  if (!record
      || typeof record.file_name !== 'string'
      || typeof record.relative_path !== 'string'
      || path.posix.basename(record.relative_path) !== record.file_name
      || path.posix.isAbsolute(record.relative_path)
      || record.relative_path.includes('\\')
      || record.relative_path.split('/').some((part) => part === '' || part === '.' || part === '..')
      || !Number.isSafeInteger(record.size_bytes)
      || record.size_bytes <= 0
      || !/^[0-9a-f]{64}$/.test(record.sha256 || '')) {
    throw new Error(`${label} identity is invalid`);
  }
}

export function validateAaxNotarizationReceipt(receipt, expected) {
  if (receipt?.schema === LEGACY_SCHEMA) {
    throw new Error('legacy AAX notarization receipt is diagnostic-only; submit the preserved archive again');
  }
  if (receipt?.schema === HASH_UNBOUND_SCHEMA) {
    throw new Error('hash-unbound AAX notarization receipt is diagnostic-only; submit the preserved archive again');
  }
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
  const submission = parseNotarytoolAccepted(
    JSON.stringify(receipt.submission),
    'recorded AAX notarization',
  );
  const expectedArchiveName = `Kirin-Hypha-${expected.version}-macOS-AAX.zip`;
  validateEvidenceIdentity(receipt.archive, 'AAX notarization receipt archive');
  if (receipt.archive.file_name !== expectedArchiveName
      || typeof receipt.archive.root_name !== 'string'
      || submission.name !== expectedArchiveName) {
    throw new Error('AAX notarization receipt archive identity is invalid');
  }
  validateEvidenceIdentity(receipt.content_manifest, 'AAX content manifest');
  validateEvidenceIdentity(receipt.notary_log, 'AAX notary log');
  const evidenceParent = `${EVIDENCE_DIRECTORY}/${receipt.archive.sha256}`;
  if (path.posix.dirname(receipt.archive.relative_path) !== evidenceParent
      || path.posix.dirname(receipt.content_manifest.relative_path) !== evidenceParent
      || path.posix.dirname(receipt.notary_log.relative_path) !== evidenceParent) {
    throw new Error('AAX notarization evidence is not co-located under the archive hash');
  }
  if (receipt.notary_log.job_id !== submission.id
      || receipt.notary_log.status !== 'Accepted'
      || receipt.notary_log.archive_filename !== expectedArchiveName
      || receipt.notary_log.archive_sha256 !== receipt.archive.sha256
      || (receipt.notary_log.issues !== null && !Array.isArray(receipt.notary_log.issues))
      || !Number.isSafeInteger(receipt.notary_log.log_format_version)) {
    throw new Error('AAX notary log identity does not match the accepted submission');
  }
  if (!Number.isFinite(Date.parse(receipt.generated_at || ''))) {
    throw new Error('AAX notarization receipt generation time is invalid');
  }
  if (!Array.isArray(receipt.bundles)
      || receipt.bundles.length !== 2
      || new Set(receipt.bundles.map((bundle) => bundle?.role)).size !== 2) {
    throw new Error('AAX notarization receipt must contain exactly PRE and POST');
  }
  for (const role of ['PRE', 'POST']) {
    const actual = receipt.bundles.find((bundle) => bundle?.role === role);
    const wanted = expected.bundles?.find((bundle) => bundle.role === role);
    if (!actual || !wanted || NOTARIZED_BUNDLE_FIELDS.some((field) => actual[field] !== wanted[field])) {
      throw new Error(`${role} AAX no longer matches its accepted notarization submission`);
    }
  }
  return receipt;
}

function readJson(filePath, label) {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch (error) {
    throw new Error(`${label} is not valid JSON: ${error.message}`);
  }
}

function verifyLocalEvidence(context, receipt, temporary, runner) {
  const archivePath = verifyEvidenceFile(receipt.archive, context.artifactRoot, 'AAX submission archive');
  const manifestPath = verifyEvidenceFile(
    receipt.content_manifest,
    context.artifactRoot,
    'AAX content manifest',
  );
  const logPath = verifyEvidenceFile(receipt.notary_log, context.artifactRoot, 'AAX notary log');
  const contentManifest = validateTreeManifest(readJson(manifestPath, 'AAX content manifest'));
  const parsedLog = parseNotarytoolLog(fs.readFileSync(logPath, 'utf8'));
  validateNotaryEvidenceLinks(receipt, contentManifest, parsedLog);
  for (const field of [
    'job_id', 'status', 'archive_filename', 'archive_sha256', 'log_format_version',
  ]) {
    if (receipt.notary_log[field] !== parsedLog[field]) {
      throw new Error(`AAX notary log ${field} does not match its preserved bytes`);
    }
  }
  if (JSON.stringify(receipt.notary_log.issues) !== JSON.stringify(parsedLog.issues)) {
    throw new Error('AAX notary log issues do not match its preserved bytes');
  }
  const bundleNames = context.manifest.bundles.map((bundle) => path.basename(bundle.sourcePath));
  requireBundleRoots(contentManifest, bundleNames);
  const extractedRoot = extractAndVerifyArchive({
    archivePath,
    manifest: contentManifest,
    destinationParent: path.join(temporary, 'extracted'),
    runner,
  });
  const bundles = inspectBundles(
    context,
    (bundle) => path.join(extractedRoot, path.basename(bundle.sourcePath)),
  );
  validateAaxNotarizationReceipt(receipt, { ...context, bundles });
  return { contentManifest, extractedRoot, bundles };
}

function verifyOnlineEvidence(context, receipt, keychainProfile, temporary, runner) {
  const confirmed = run('xcrun', [
    'notarytool', 'info', receipt.submission.id,
    '--keychain-profile', keychainProfile,
    '--output-format', 'json',
  ], { cwd: context.root, runner });
  const online = parseNotarytoolAccepted(confirmed.stdout, 'online AAX notarytool info');
  if (online.id !== receipt.submission.id || online.name !== receipt.archive.file_name) {
    throw new Error('online AAX notarization submission does not match the receipt archive');
  }
  const onlineLogPath = path.join(temporary, 'online-notary-log.json');
  run('xcrun', [
    'notarytool', 'log', receipt.submission.id, onlineLogPath,
    '--keychain-profile', keychainProfile,
  ], { cwd: context.root, runner });
  const onlineLog = parseNotarytoolLog(
    fs.readFileSync(onlineLogPath, 'utf8'),
    'online AAX notarytool log',
  );
  if (onlineLog.job_id !== receipt.notary_log.job_id
      || onlineLog.status !== receipt.notary_log.status
      || onlineLog.archive_filename !== receipt.notary_log.archive_filename
      || onlineLog.archive_sha256 !== receipt.notary_log.archive_sha256
      || JSON.stringify(onlineLog.issues) !== JSON.stringify(receipt.notary_log.issues)
      || onlineLog.log_format_version !== receipt.notary_log.log_format_version) {
    throw new Error('online AAX notary log does not match the preserved submission evidence');
  }
}

function verifyPayloadCopies(context, extractedRoot, payloadDir) {
  for (const bundle of context.manifest.bundles) {
    const name = path.basename(bundle.sourcePath);
    verifyAaxBundleCopy({
      sourcePath: path.join(extractedRoot, name),
      destinationPath: path.join(path.resolve(payloadDir), name),
      spec: bundle,
      version: context.version,
      sourceId: context.source.commit,
      sourceState: 'clean source',
      requireKimera: true,
      requireNativeOnly: true,
    });
  }
}

export function verifyMacAaxNotarizationReceipt(options = {}) {
  const context = releaseIdentityContext(options);
  const receiptPath = path.join(context.artifactRoot, AAX_NOTARIZATION_RECEIPT_NAME);
  if (!fs.statSync(receiptPath, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`AAX notarization receipt is missing: ${receiptPath}`);
  }
  const receipt = readJson(receiptPath, 'AAX notarization receipt');
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-aax-verify-'));
  const commandRunner = options.commandRunner || runEvidenceCommand;
  try {
    const verified = verifyLocalEvidence(context, receipt, temporary, commandRunner);
    if (options.online) {
      if (process.platform !== 'darwin') throw new Error('online AAX notarization verification requires macOS');
      verifyOnlineEvidence(
        context,
        receipt,
        options.keychainProfile || process.env.KIRIN_NOTARY_PROFILE || 'kirin-notarize',
        temporary,
        commandRunner,
      );
    }
    if (options.materializeDir) {
      materializeVerifiedTree(
        verified.extractedRoot,
        path.resolve(options.materializeDir),
        verified.contentManifest,
        commandRunner,
      );
    }
    if (options.payloadDir) {
      verifyPayloadCopies(context, verified.extractedRoot, options.payloadDir);
    }
    return { receipt, receiptPath, bundles: verified.bundles };
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
}

export function submitMacAaxForNotarization({
  root = PROJECT_ROOT,
  artifactDir = 'build-aax-universal',
  keychainProfile = process.env.KIRIN_NOTARY_PROFILE || 'kirin-notarize',
  commandRunner = runEvidenceCommand,
} = {}) {
  if (process.platform !== 'darwin') throw new Error('macOS AAX notarization requires macOS');
  if (!keychainProfile) throw new Error('AAX notarization requires a notarytool keychain profile');
  const context = releaseIdentityContext({ root, artifactDir });
  const sourceRecords = inspectBundles(context, (bundle) => bundle.sourcePath);
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-aax-notary-'));
  const rootName = `Kirin Hypha ${context.version} AAX`;
  const archiveRoot = path.join(temporary, rootName);
  const archiveName = `Kirin-Hypha-${context.version}-macOS-AAX.zip`;
  const temporaryArchive = path.join(temporary, archiveName);
  const execute = (command, args) => run(command, args, { cwd: context.root, runner: commandRunner });
  try {
    fs.mkdirSync(archiveRoot, { recursive: true });
    for (const [index, bundle] of context.manifest.bundles.entries()) {
      const staged = path.join(archiveRoot, path.basename(bundle.sourcePath));
      execute('ditto', [bundle.sourcePath, staged]);
      const copied = inspectBundle(context, bundle, staged);
      if (copied.bundle_manifest_sha256 !== sourceRecords[index].bundle_manifest_sha256) {
        throw new Error(`${bundle.role} AAX changed while staging the notarization archive`);
      }
    }
    const contentManifest = buildTreeManifest(archiveRoot, rootName);
    requireBundleRoots(contentManifest, sourceRecords.map((bundle) => bundle.bundle));
    const temporaryManifest = path.join(temporary, CONTENT_MANIFEST_NAME);
    fs.writeFileSync(temporaryManifest, manifestBytes(contentManifest), { flag: 'wx' });
    execute('ditto', [
      '-c', '-k', '--norsrc', '--noqtn', '--keepParent', archiveRoot, temporaryArchive,
    ]);
    const archiveSha256 = sha256File(temporaryArchive);
    const evidenceRoot = path.join(context.artifactRoot, EVIDENCE_DIRECTORY, archiveSha256);
    const archivePath = persistImmutableFile(temporaryArchive, path.join(evidenceRoot, archiveName));
    const manifestPath = persistImmutableFile(
      temporaryManifest,
      path.join(evidenceRoot, CONTENT_MANIFEST_NAME),
    );
    const submitted = parseNotarytoolAccepted(execute('xcrun', [
      'notarytool', 'submit', archivePath,
      '--keychain-profile', keychainProfile,
      '--wait', '--output-format', 'json',
    ]).stdout, 'AAX notarytool submit');
    const confirmation = parseNotarytoolAccepted(execute('xcrun', [
      'notarytool', 'info', submitted.id,
      '--keychain-profile', keychainProfile,
      '--output-format', 'json',
    ]).stdout, 'AAX notarytool info');
    if (confirmation.id !== submitted.id || confirmation.name !== archiveName) {
      throw new Error('AAX notarytool confirmation does not identify the submitted archive');
    }
    const temporaryLog = path.join(temporary, NOTARY_LOG_NAME);
    execute('xcrun', [
      'notarytool', 'log', confirmation.id, temporaryLog,
      '--keychain-profile', keychainProfile,
    ]);
    const parsedLog = parseNotarytoolLog(fs.readFileSync(temporaryLog, 'utf8'));
    const logPath = persistImmutableFile(temporaryLog, path.join(evidenceRoot, NOTARY_LOG_NAME));
    const receipt = {
      schema: AAX_NOTARIZATION_SCHEMA,
      generated_at: new Date().toISOString(),
      source: { commit: context.source.commit, b_number: context.source.bNumber, state: 'clean source' },
      product: { name: 'Kirin Hypha', version: context.version, platform: 'macos-universal', format: 'AAX' },
      submission: confirmation,
      archive: { ...evidenceRecord(archivePath, context.artifactRoot), root_name: rootName },
      content_manifest: evidenceRecord(manifestPath, context.artifactRoot),
      notary_log: { ...evidenceRecord(logPath, context.artifactRoot), ...parsedLog },
      bundles: sourceRecords,
    };
    validateNotaryEvidenceLinks(receipt, contentManifest, parsedLog);
    validateAaxNotarizationReceipt(receipt, { ...context, bundles: sourceRecords });
    const receiptPath = path.join(context.artifactRoot, AAX_NOTARIZATION_RECEIPT_NAME);
    writeJsonAtomic(receiptPath, receipt);
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
    else if (argv[index] === '--materialize-dir') options.materializeDir = argv[++index] || '';
    else if (argv[index] === '--payload-dir') options.payloadDir = argv[++index] || '';
    else if (argv[index] === '--online') options.online = true;
    else if (argv[index] === '--help' || argv[index] === '-h') options.help = true;
    else throw new Error(`unknown argument: ${argv[index]}`);
  }
  return options;
}

function main(argv) {
  const options = parseArgs(argv);
  if (options.help || !['submit', 'verify'].includes(options.mode)) {
    console.log('Usage: node aax_notarization_receipt.mjs submit|verify [--artifact-dir DIR] [--keychain-profile PROFILE] [--online] [--materialize-dir DIR] [--payload-dir DIR]');
    if (options.help) return;
    throw new Error('mode must be submit or verify');
  }
  if (options.mode === 'submit' && (options.materializeDir || options.payloadDir)) {
    throw new Error('--materialize-dir and --payload-dir are only valid for verify');
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
      materializeDir: options.materializeDir || undefined,
      payloadDir: options.payloadDir || undefined,
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
