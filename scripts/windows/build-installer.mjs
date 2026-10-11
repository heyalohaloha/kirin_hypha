#!/usr/bin/env node
import { spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { createSigningAttempt, finishSigningAttempt } from './signing_recovery.mjs';
import { batchSign, signingEnvironment } from './sign-codesigntool.mjs';
import {
  loadWindowsAaxBundleManifest,
  recordAtBundle,
  verifyWindowsAaxCopy,
} from './windows-aax-bundles.mjs';
import { loadWindowsAaxSignedProvenance } from './windows-aax-provenance.mjs';
import { requireCleanReleaseSource, readReleaseSourceIdentity } from '../ls_release/release_source_identity.mjs';
import { updateBinding, validatePublicKey, inspectUpdateBinary } from '../updates/update_key_binding.mjs';
import { stageLegalDelivery, verifyLegalDelivery } from '../provenance/legal_delivery.mjs';
import { verifyUnsignedWindowsHandoff } from './unsigned_vst3_provenance.mjs';

const THIS_FILE = fileURLToPath(import.meta.url);
const ROOT = path.resolve(path.dirname(THIS_FILE), '..', '..');
const ISS_FILE = path.join(ROOT, 'scripts', 'windows', 'kirin-hypha-installer.iss');
const SIGNER_FILE = path.join(ROOT, 'scripts', 'windows', 'inno-sign-codesigntool.mjs');
export const PRODUCT_NAME = 'Kirin Hypha';
export const PAYLOAD_DIR_NAME = 'installer-payload';

function readVersion() {
  const source = fs.readFileSync(path.join(ROOT, 'crates', 'hypha_pre', 'Cargo.toml'), 'utf8');
  const match = source.match(/^version\s*=\s*"([^"]+)"/m);
  if (!match) throw new Error('Hypha version is missing from crates/hypha_pre/Cargo.toml');
  return match[1];
}

export const VERSION = readVersion();

export function parseArgs(argv) {
  const opts = {
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
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === '--help' || arg === '-h') opts.help = true;
    else if (arg === '--artifact-dir') opts.artifactDir = argv[++index] || '';
    else if (arg === '--aax-artifact-dir') {
      opts.aaxArtifactDir = argv[++index] || '';
      if (!opts.aaxArtifactDir) throw new Error('--aax-artifact-dir requires a value');
    }
    else if (arg === '--output-dir') opts.outputDir = argv[++index] || '';
    else if (arg === '--signing') opts.signing = argv[++index] || '';
    else if (arg === '--external-validation') opts.externalValidation = argv[++index] || '';
    else if (arg === '--b-number') opts.bNumber = argv[++index] || '';
    else if (arg === '--commit') opts.commit = argv[++index] || '';
    else if (arg === '--run-url') opts.runUrl = argv[++index] || '';
    else if (arg === '--update-public-key') opts.updatePublicKey = argv[++index] || '';
    else if (arg === '--legal-dir') {
      opts.legalDir = argv[++index] || '';
      if (!opts.legalDir) throw new Error('--legal-dir requires a value');
    }
    else throw new Error(`unknown option: ${arg}`);
  }
  if (!['unsigned', 'signed'].includes(opts.signing)) {
    throw new Error(`--signing must be unsigned or signed: ${opts.signing}`);
  }
  if (!['pending', 'complete'].includes(opts.externalValidation)) {
    throw new Error(`--external-validation must be pending or complete: ${opts.externalValidation}`);
  }
  return opts;
}

function run(command, args, options = {}) {
  console.log(`[hypha-installer] ${path.basename(command)} ${args.join(' ')}`);
  const result = spawnSync(command, args, {
    cwd: ROOT,
    env: { ...process.env, ...(options.env || {}) },
    encoding: 'utf8',
    stdio: options.capture ? ['ignore', 'pipe', 'pipe'] : 'inherit',
    shell: process.platform === 'win32' && /\.(cmd|bat)$/i.test(command),
  });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    const output = options.capture ? `${result.stdout || ''}${result.stderr || ''}`.trim() : '';
    throw new Error(`${path.basename(command)} exited with ${result.status}${output ? `\n${output}` : ''}`);
  }
  return result.stdout || '';
}

function git(args, fallback) {
  try {
    return run('git', args, { capture: true }).trim() || fallback;
  } catch {
    return fallback;
  }
}

export function inferBNumber({ root = ROOT } = {}) {
  return readReleaseSourceIdentity({ root }).bNumber;
}

function inferRunUrl() {
  if (process.env.GITHUB_SERVER_URL && process.env.GITHUB_REPOSITORY && process.env.GITHUB_RUN_ID) {
    return `${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}`;
  }
  return '';
}

function visitDirectories(root, callback) {
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const full = path.join(root, entry.name);
    callback(full);
    visitDirectories(full, callback);
  }
}

export function findUniqueBundle(root, name) {
  const absolute = path.resolve(ROOT, root);
  if (!fs.statSync(absolute, { throwIfNoEntry: false })?.isDirectory()) {
    throw new Error(`Windows VST3 artifact directory is missing: ${absolute}`);
  }
  const matches = [];
  if (path.basename(absolute) === name) matches.push(absolute);
  visitDirectories(absolute, (candidate) => {
    if (path.basename(candidate) === name) matches.push(candidate);
  });
  if (matches.length !== 1) throw new Error(`expected exactly one ${name}, found ${matches.length}`);
  return matches[0];
}

export function bundleRecord(root, role) {
  const name = `Kirin Hypha ${role}.vst3`;
  const bundle = findUniqueBundle(root, name);
  const binary = path.join(bundle, 'Contents', 'x86_64-win', name);
  const moduleInfo = path.join(bundle, 'Contents', 'Resources', 'moduleinfo.json');
  if (!fs.statSync(binary, { throwIfNoEntry: false })?.isFile() || fs.statSync(binary).size <= 0) {
    throw new Error(`${role} VST3 binary is missing or empty: ${binary}`);
  }
  if (!fs.statSync(moduleInfo, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`${role} moduleinfo.json is missing: ${moduleInfo}`);
  }
  const metadata = fs.readFileSync(moduleInfo, 'utf8');
  if (!metadata.includes(`"Version": "${VERSION}"`)) {
    throw new Error(`${role} moduleinfo.json does not declare version ${VERSION}`);
  }
  return { role, name, bundle, binary, moduleInfo };
}

function sha256(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

function resolveIscc(env = process.env) {
  const candidates = [
    env.ISCC_PATH,
    'C:\\Program Files (x86)\\Inno Setup 6\\ISCC.exe',
    'C:\\Program Files\\Inno Setup 6\\ISCC.exe',
  ].filter(Boolean);
  const match = candidates.find((candidate) => fs.statSync(candidate, { throwIfNoEntry: false })?.isFile());
  if (!match) throw new Error('ISCC.exe not found; set ISCC_PATH to the verified Inno Setup 6 compiler');
  return match;
}

export function innoCompilerArgs({ outputDir, payloadDir, signing, aaxRecords = [], legalDir = '' }) {
  const args = [
    `/DAppVersion=${VERSION}`,
    `/DOutputDir=${outputDir}`,
    `/DPreBundle=${path.join(payloadDir, 'Kirin Hypha PRE.vst3')}`,
    `/DPostBundle=${path.join(payloadDir, 'Kirin Hypha POST.vst3')}`,
  ];
  if (legalDir) args.push(`/DLegalDir=${legalDir}`);
  if (signing === 'signed') {
    args.push('/DSignedBuild=1');
    args.push(`/Skirin_esigner=$q${process.execPath}$q $q${SIGNER_FILE}$q --input-file $f`);
  }
  if (aaxRecords.length > 0) {
    if (aaxRecords.length !== 2) throw new Error('AAX installer payload must contain PRE and POST');
    const pre = aaxRecords.find((record) => record.role === 'PRE');
    const post = aaxRecords.find((record) => record.role === 'POST');
    if (!pre || !post) throw new Error('AAX installer payload must contain unique PRE and POST');
    args.push('/DWithAax=1');
    args.push(`/DPreAaxBundle=${pre.bundle}`);
    args.push(`/DPostAaxBundle=${post.bundle}`);
  }
  args.push(ISS_FILE);
  return args;
}

async function signPayload(records, tempRoot, env) {
  const input = path.join(tempRoot, 'payload-sign-input');
  const output = path.join(tempRoot, 'payload-sign-output');
  fs.mkdirSync(input, { recursive: true });
  for (const record of records) {
    fs.copyFileSync(record.binary, path.join(input, `Kirin-Hypha-${record.role}.dll`));
  }
  await batchSign(input, output, { env });
  for (const record of records) {
    const signed = path.join(output, `Kirin-Hypha-${record.role}.dll`);
    if (!fs.statSync(signed, { throwIfNoEntry: false })?.isFile()) {
      throw new Error(`signed ${record.role} payload was not returned by CodeSignTool`);
    }
    fs.copyFileSync(signed, record.binary);
  }
}

export function bindAaxSourceIdentity(opts, currentSource, aaxProvenance) {
  const source = aaxProvenance.manifest.source;
  if (source.commit !== currentSource.commit || source.b_number !== currentSource.bNumber) {
    throw new Error('signed Windows AAX source does not match the installer source');
  }
  if ((opts.commit && opts.commit !== currentSource.commit)
      || (opts.bNumber && opts.bNumber !== currentSource.bNumber)) {
    throw new Error('requested Windows installer identity does not match clean source');
  }
  return { ...opts, commit: currentSource.commit, bNumber: currentSource.bNumber };
}

function manifestFor({ opts, installer, payloadRecords, aaxPayloadRecords, aaxProvenance }) {
  const aaxIncluded = aaxPayloadRecords.length === 2;
  return {
    schema: 'kirin-hypha-windows-installer-v1',
    schema_version: 1,
    generated_at: new Date().toISOString(),
    updateCheck: { ...updateBinding(opts.updatePublicKey), binaries: [
      ...payloadRecords.map(record => ({ role: record.role, format: 'VST3', ...inspectUpdateBinary(record.binary, opts.updatePublicKey) })),
      ...aaxPayloadRecords.map(record => ({ role: record.role, format: 'AAX', ...inspectUpdateBinary(record.binary, opts.updatePublicKey) })),
    ] },
    product: {
      name: PRODUCT_NAME,
      version: VERSION,
      platform: 'windows-x64',
      format: aaxIncluded ? 'VST3+AAX' : 'VST3',
      formats: aaxIncluded ? ['VST3', 'AAX'] : ['VST3'],
    },
    source: {
      commit: opts.commit,
      b_number: opts.bNumber,
      github_actions_run: opts.runUrl || null,
      job: 'windows VST3 preflight',
      ...(opts.unsignedBuild ? { producer: { kind: 'private-windows-latest',
        factory_commit: opts.unsignedBuild.factoryCommit, factory_run: opts.unsignedBuild.factoryRun,
        source_ci_run: opts.unsignedBuild.ciRun, unsigned_binaries: opts.unsignedBuild.binaries } } : {}),
    },
    installer: {
      name: path.basename(installer),
      size_bytes: fs.statSync(installer).size,
      sha256: sha256(installer),
      framework: 'Inno Setup 6',
      install_mode: aaxIncluded
        ? 'all-users; administrator required'
        : 'per-user by default; all-users selectable',
      payload: payloadRecords.map((record) => ({
        role: record.role,
        format: 'VST3',
        bundle: record.name,
        binary_sha256: sha256(record.binary),
      })),
      aax_payload: aaxPayloadRecords.map((record) => ({
        role: record.role,
        format: 'AAX',
        bundle: record.name,
        binary_sha256: sha256(record.binary),
        pace_verified: true,
        authenticode_verified: true,
      })),
    },
    signing: {
      requested: opts.signing,
      status: opts.signing === 'signed' ? 'pending_authenticode_verification' : 'unsigned_ci_candidate',
      tool: opts.signing === 'signed' ? 'SSL.com eSigner CodeSignTool' : null,
      workflow_run: process.env.KIRIN_SIGNING_RUN_URL || opts.runUrl || null,
    },
    ci_validation: {
      status: 'pending_installer_verification',
      required: [
        'silent install',
        'same-version reinstall',
        ...(opts.signing === 'signed' ? ['prior-public-version upgrade'] : []),
        'PRE/POST payload hash equality',
        ...(aaxIncluded ? ['PACE + Authenticode AAX payloads'] : []),
        'Authenticode surfaces when signed',
        'silent uninstall',
        'unrelated VST3 preservation',
      ],
    },
    external_validation: {
      status: opts.externalValidation,
      note: opts.externalValidation === 'complete'
        ? 'Dedicated Windows DAW validation reported complete.'
        : 'Dedicated Windows DAW validation remains required before public release.',
    },
    distribution: {
      primary: true,
      manual_zip: 'fallback_only',
      aax_included: aaxIncluded,
      aax_identity: aaxIncluded ? {
        source_commit: aaxProvenance.manifest.source.commit,
        b_number: aaxProvenance.manifest.source.b_number,
        source_state: aaxProvenance.manifest.source.state,
        build_mode: aaxProvenance.manifest.release.mode,
        kimera_embedded: aaxProvenance.manifest.release.kimera_embedded,
        native_only: aaxProvenance.manifest.release.native_only,
        audio_suite_enabled: aaxProvenance.manifest.release.audio_suite_enabled,
        signed_manifest: `Kirin-Hypha-${VERSION}-Windows-x64-AAX.json`,
        signed_manifest_sha256: aaxProvenance.manifestSha256,
      } : null,
      public_ready: false,
    },
  };
}

export async function buildInstaller(opts) {
  validatePublicKey(opts.updatePublicKey || '');
  if (process.platform !== 'win32') throw new Error('Windows installer builds must run on Windows');
  if (opts.signing === 'signed') {
    run('pwsh', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'scripts/windows/installer_environment.ps1']);
    signingEnvironment();
  }
  let aaxProvenance = null;
  if (opts.aaxArtifactDir) {
    aaxProvenance = loadWindowsAaxSignedProvenance({
      artifactRoot: opts.aaxArtifactDir,
      version: VERSION,
      updatePublicKey: opts.updatePublicKey || '',
    });
    opts = bindAaxSourceIdentity(opts, requireCleanReleaseSource({ root: ROOT }), aaxProvenance);
  } else {
    opts.commit ||= git(['rev-parse', 'HEAD'], 'unknown');
    opts.bNumber ||= inferBNumber();
  }
  opts.runUrl ||= inferRunUrl();
  if (opts.signing === 'signed' && !opts.legalDir) throw new Error('Signed distribution requires retained source/legal delivery');
  if (opts.signing === 'signed') {
    const checked = verifyUnsignedWindowsHandoff(path.resolve(ROOT, opts.artifactDir), {
      commit: opts.commit, ciRun: opts.runUrl.split('/').at(-1),
      factoryCommit: process.env.GITHUB_SHA, factoryRun: process.env.KIRIN_UNSIGNED_VST3_RUN_ID,
    });
    if (path.resolve(opts.legalDir) !== path.resolve(checked.legalDir)) throw new Error('Signed installer legal source must come from the verified producer');
    opts.unsignedBuild = checked.record;
  }
  if (opts.legalDir) verifyLegalDelivery(opts.legalDir, opts.commit);

  const outputDir = path.resolve(ROOT, opts.outputDir);
  const payloadDir = path.join(outputDir, PAYLOAD_DIR_NAME);
  const installer = path.join(outputDir, `Kirin-Hypha-${VERSION}-Windows-x64-Setup.exe`);
  const aaxManifestCopy = path.join(outputDir, `Kirin-Hypha-${VERSION}-Windows-x64-AAX.json`);
  fs.mkdirSync(outputDir, { recursive: true });
  if (opts.signing === 'signed' && [payloadDir, installer, `${installer}.json`, path.join(outputDir, 'signing-recovery')].some(file => fs.existsSync(file))) {
    throw new Error('Existing signing output requires reconciliation; preserve partial successes before any resend');
  }
  fs.rmSync(payloadDir, { recursive: true, force: true });
  for (const sidecar of [installer, `${installer}.sha256`, `${installer}.json`, aaxManifestCopy]) {
    fs.rmSync(sidecar, { force: true });
  }
  fs.mkdirSync(payloadDir, { recursive: true });
  const legalDir = opts.legalDir ? path.join(payloadDir, 'Legal') : '';
  if (legalDir) stageLegalDelivery(opts.legalDir, legalDir, opts.commit);

  const sourceRecords = ['PRE', 'POST'].map((role) => bundleRecord(opts.artifactDir, role));
  for (const record of sourceRecords) inspectUpdateBinary(record.binary, opts.updatePublicKey || '');
  const aaxSourceRecords = opts.aaxArtifactDir
    ? loadWindowsAaxBundleManifest({ artifactRoot: opts.aaxArtifactDir }).bundles
    : [];
  for (const source of sourceRecords) {
    fs.cpSync(source.bundle, path.join(payloadDir, source.name), { recursive: true });
  }
  const aaxPayloadRecords = [];
  for (const source of aaxSourceRecords) {
    const destination = path.join(payloadDir, source.name);
    fs.cpSync(source.bundle, destination, { recursive: true });
    aaxPayloadRecords.push(verifyWindowsAaxCopy(
      source,
      recordAtBundle(source, destination),
      VERSION,
    ));
  }
  if (aaxProvenance) fs.copyFileSync(aaxProvenance.manifestPath, aaxManifestCopy);
  const payloadRecords = ['PRE', 'POST'].map((role) => bundleRecord(payloadDir, role));
  const recoveryRoot = path.join(outputDir, 'signing-recovery');
  const attempt = opts.signing === 'signed' ? createSigningAttempt(recoveryRoot, 'payload-container') : null;
  const tempRoot = attempt?.directory || fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-installer-'));
  let completed = false;
  const buildEnv = {
    ...process.env,
    KIRIN_ESIGNER_RECOVERY_DIR: recoveryRoot,
    KIRIN_ESIGNER_TOTP_STATE_FILE: path.join(tempRoot, 'totp-window.state'),
  };
  try {
    if (opts.signing === 'signed') await signPayload(payloadRecords, tempRoot, buildEnv);
    run(resolveIscc(buildEnv), innoCompilerArgs({
      outputDir,
      payloadDir,
      signing: opts.signing,
      aaxRecords: aaxPayloadRecords,
      legalDir,
    }), { env: buildEnv });
    completed = true;
  } finally {
    if (attempt) finishSigningAttempt(attempt, completed ? 'completed' : 'failed-or-uncertain');
    else fs.rmSync(tempRoot, { recursive: true, force: true });
  }
  if (!fs.statSync(installer, { throwIfNoEntry: false })?.isFile() || fs.statSync(installer).size <= 0) {
    throw new Error(`Inno Setup did not produce the expected installer: ${installer}`);
  }
  const manifest = manifestFor({
    opts,
    installer,
    payloadRecords,
    aaxPayloadRecords,
    aaxProvenance,
  });
  if (legalDir) manifest.legalDelivery = verifyLegalDelivery(legalDir, opts.commit);
  fs.writeFileSync(`${installer}.sha256`, `${manifest.installer.sha256}  ${path.basename(installer)}\n`);
  fs.writeFileSync(`${installer}.json`, `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`[hypha-installer] wrote ${installer}`);
  console.log(`[hypha-installer] signing ${manifest.signing.status}`);
  return { installer, manifestPath: `${installer}.json`, payloadDir };
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    console.log('Build the Kirin Hypha Windows installer. Add --aax-artifact-dir DIR only for signed AAX payloads.');
    return;
  }
  await buildInstaller(opts);
}

if (process.argv[1] && path.resolve(process.argv[1]) === THIS_FILE) {
  main().catch((error) => {
    console.error(`[hypha-installer] ERROR: ${error.message}`);
    process.exit(1);
  });
}
