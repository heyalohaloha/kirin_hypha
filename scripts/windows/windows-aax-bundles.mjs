#!/usr/bin/env node
import { spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const THIS_FILE = fileURLToPath(import.meta.url);
const ROOT = path.resolve(path.dirname(THIS_FILE), '..', '..');
const SCHEMA = 'kirin-hypha-windows-aax-bundles-v1';
const INSTALL_PARENT = 'Avid/Audio/Plug-Ins';
const X64_MACHINE = 0x8664;
const AUTHENTICODE_SUBJECT = 'CN=Daisuke Nishio';

function fail(message) {
  throw new Error(`invalid Windows AAX bundle manifest: ${message}`);
}

function validateRelative(value, field) {
  if (
    typeof value !== 'string'
    || value.length === 0
    || path.posix.isAbsolute(value)
    || value.includes('\\')
    || value.split('/').some((part) => part === '' || part === '.' || part === '..')
    || /[\0\r\n\t]/.test(value)
  ) {
    fail(`${field} must be a safe POSIX relative path`);
  }
}

function validateManifest(manifest) {
  if (manifest?.schema !== SCHEMA) fail(`schema must be ${SCHEMA}`);
  if (!Array.isArray(manifest.bundles) || manifest.bundles.length !== 2) {
    fail('bundles must contain exactly PRE and POST');
  }
  const roles = new Set();
  for (const bundle of manifest.bundles) {
    if (!['PRE', 'POST'].includes(bundle.role) || roles.has(bundle.role)) {
      fail(`role must be unique PRE or POST: ${bundle.role}`);
    }
    roles.add(bundle.role);
    for (const field of ['source_relative', 'binary_relative', 'install_relative']) {
      validateRelative(bundle[field], `${bundle.role}.${field}`);
    }
    const expectedName = `Kirin Hypha ${bundle.role}.aaxplugin`;
    if (
      path.posix.basename(bundle.source_relative) !== expectedName
      || path.posix.basename(bundle.binary_relative) !== expectedName
      || path.posix.basename(bundle.install_relative) !== expectedName
    ) {
      fail(`${bundle.role} names must be ${expectedName}`);
    }
    if (path.posix.dirname(bundle.binary_relative) !== 'Contents/x64') {
      fail(`${bundle.role}.binary_relative must be under Contents/x64`);
    }
    if (path.posix.dirname(bundle.install_relative) !== INSTALL_PARENT) {
      fail(`${bundle.role}.install_relative is outside the Avid directory`);
    }
  }
}

function nativePath(root, relative) {
  return path.join(root, ...relative.split('/'));
}

export function loadWindowsAaxBundleManifest(options = {}) {
  const root = path.resolve(options.root || ROOT);
  const manifestPath = path.join(root, 'config', 'hypha_windows_aax_bundles.json');
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  validateManifest(manifest);
  const artifactRoot = path.resolve(root, options.artifactRoot || 'build-aax-windows');
  return {
    schema: manifest.schema,
    artifactRoot,
    bundles: manifest.bundles.map((spec) => ({
      role: spec.role,
      name: path.posix.basename(spec.source_relative),
      bundle: nativePath(artifactRoot, spec.source_relative),
      binaryRelative: spec.binary_relative,
      spec,
    })),
  };
}

export function readPeMachine(binaryPath) {
  const data = fs.readFileSync(binaryPath);
  if (data.length < 0x40 || data[0] !== 0x4d || data[1] !== 0x5a) {
    throw new Error(`AAX binary is not a PE file: ${binaryPath}`);
  }
  const peOffset = data.readUInt32LE(0x3c);
  if (peOffset + 6 > data.length || data.toString('ascii', peOffset, peOffset + 4) !== 'PE\0\0') {
    throw new Error(`AAX binary has no valid PE header: ${binaryPath}`);
  }
  return data.readUInt16LE(peOffset + 4);
}

function sha256(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

export function inspectWindowsAaxRecord(record) {
  if (!fs.statSync(record.bundle, { throwIfNoEntry: false })?.isDirectory()) {
    throw new Error(`${record.role} AAX bundle is missing: ${record.bundle}`);
  }
  const binary = nativePath(record.bundle, record.binaryRelative);
  if (!fs.statSync(binary, { throwIfNoEntry: false })?.isFile() || fs.statSync(binary).size <= 0) {
    throw new Error(`${record.role} AAX binary is missing or empty: ${binary}`);
  }
  const machine = readPeMachine(binary);
  if (machine !== X64_MACHINE) {
    throw new Error(`${record.role} AAX machine is 0x${machine.toString(16)}, expected x64`);
  }
  return { ...record, binary, binarySha256: sha256(binary) };
}

function powershellLiteral(value) {
  return `'${value.replaceAll("'", "''")}'`;
}

function run(command, args, label, options = {}) {
  const result = spawnSync(command, args, {
    cwd: ROOT,
    encoding: 'utf8',
    stdio: options.capture ? ['ignore', 'pipe', 'pipe'] : 'inherit',
  });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${label} failed with status ${result.status}`);
  return result.stdout || '';
}

function resolveWraptool(env = process.env) {
  const programFiles = env.ProgramFiles || 'C:\\Program Files';
  const candidates = [
    env.KIRIN_AAX_WRAPTOOL,
    path.join(programFiles, 'PACEAntiPiracy', 'Eden', 'Fusion', 'Current', 'bin', 'wraptool.exe'),
    path.join(programFiles, 'PACEAntiPiracy', 'Eden', 'Fusion', 'Versions', '6', 'bin', 'wraptool.exe'),
  ].filter(Boolean);
  const match = candidates.find((candidate) => fs.statSync(candidate, { throwIfNoEntry: false })?.isFile());
  if (!match) throw new Error('PACE wraptool.exe is required for Windows AAX distribution');
  return match;
}

export function verifyWindowsAaxRecord(record, version, options = {}) {
  if (process.platform !== 'win32') throw new Error('Windows AAX signature verification must run on Windows');
  const inspected = inspectWindowsAaxRecord(record);
  const binary = powershellLiteral(inspected.binary);
  const script = [
    `$file = Get-Item -LiteralPath ${binary}`,
    `$signature = Get-AuthenticodeSignature -LiteralPath ${binary}`,
    '[Console]::Out.WriteLine($signature.Status)',
    '[Console]::Out.WriteLine($file.VersionInfo.FileVersion)',
    '[Console]::Out.WriteLine($file.VersionInfo.ProductVersion)',
    '[Console]::Out.WriteLine([string]$signature.SignerCertificate.Subject)',
    '[Console]::Out.WriteLine([string]$signature.TimeStamperCertificate.Subject)',
  ].join('; ');
  const output = run('powershell.exe', ['-NoProfile', '-Command', script], 'inspect AAX Authenticode', { capture: true });
  const [status, fileVersion, productVersion, signerSubject, timestampSubject] = output.trim().split(/\r?\n/);
  if (status !== 'Valid') throw new Error(`${record.role} AAX Authenticode status is ${status}`);
  if (fileVersion !== version || productVersion !== version) {
    throw new Error(`${record.role} AAX version is ${fileVersion}/${productVersion}, expected ${version}`);
  }
  if (!signerSubject?.includes(AUTHENTICODE_SUBJECT)) {
    throw new Error(`${record.role} AAX Authenticode signer is not the Kirin publisher`);
  }
  if (!timestampSubject) throw new Error(`${record.role} AAX Authenticode timestamp is missing`);
  run(
    options.wraptool || resolveWraptool(options.env),
    ['verify', '--localonly', '--in', inspected.binary],
    `${record.role} AAX PACE verification`,
  );
  return inspected;
}

export function recordAtBundle(sourceRecord, bundlePath) {
  return {
    ...sourceRecord,
    bundle: bundlePath,
  };
}

export function verifyWindowsAaxCopy(sourceRecord, destinationRecord, version, options = {}) {
  const source = verifyWindowsAaxRecord(sourceRecord, version, options);
  const destination = verifyWindowsAaxRecord(destinationRecord, version, options);
  if (source.binarySha256 !== destination.binarySha256) {
    throw new Error(`${source.role} AAX changed while staging the installer`);
  }
  return destination;
}

function main(argv) {
  const artifactIndex = argv.indexOf('--artifact-dir');
  if (artifactIndex < 0 || !argv[artifactIndex + 1]) {
    console.log(`Usage: node ${path.relative(ROOT, THIS_FILE)} --artifact-dir DIR --version VERSION`);
    return;
  }
  const versionIndex = argv.indexOf('--version');
  if (versionIndex < 0 || !argv[versionIndex + 1]) throw new Error('--version is required');
  const manifest = loadWindowsAaxBundleManifest({ artifactRoot: argv[artifactIndex + 1] });
  for (const record of manifest.bundles) verifyWindowsAaxRecord(record, argv[versionIndex + 1]);
}

if (process.argv[1] && path.resolve(process.argv[1]) === THIS_FILE) {
  try {
    main(process.argv.slice(2));
  } catch (error) {
    console.error(`[windows-aax] ERROR: ${error.message}`);
    process.exitCode = 1;
  }
}
