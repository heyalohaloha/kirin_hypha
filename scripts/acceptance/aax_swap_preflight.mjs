import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

// Preflight only. A privileged consumer must independently validate its private copy.
export function quoteShell(value) {
  if (typeof value !== 'string' || value.includes('\0')) throw new Error('invalid shell argument: NUL');
  return `'${value.replaceAll("'", "'\\''")}'`;
}

export function validateRoles(manifestBundles, roles) {
  const expected = ['POST', 'PRE'];
  if (!Array.isArray(manifestBundles) || !Array.isArray(roles)
      || JSON.stringify(manifestBundles.map(x => x.role).sort()) !== JSON.stringify(expected)
      || JSON.stringify([...roles].sort()) !== JSON.stringify(expected)) {
    throw new Error('role set must be exactly PRE and POST');
  }
}

export const SUPPORTED_ATTRIBUTES = Object.freeze(['com.apple.provenance', 'com.apple.FinderInfo', 'com.apple.ResourceFork', 'com.apple.quarantine', 'com.example.fixture']);

const SOURCE_SCANNER = fileURLToPath(new URL('./aax_swap_source.py', import.meta.url));

function scanSource(bundle, mode) {
  if (typeof bundle !== 'string' || !path.isAbsolute(bundle) || path.resolve(bundle) !== bundle) {
    throw new Error('bundle path must be absolute and normalized');
  }
  const env = { ...process.env };
  if (process.platform === 'darwin') env.DEVELOPER_DIR = '/Library/Developer/CommandLineTools';
  const result = spawnSync('/usr/bin/python3', ['-I', '-S', '-B', SOURCE_SCANNER, mode, bundle], {
    encoding: 'utf8', maxBuffer: 384 * 1024 * 1024, env,
  });
  if (result.error) throw new Error('descriptor source scanner failed', { cause: result.error });
  if (result.status !== 0) throw new Error(`descriptor source scanner failed: ${result.stderr.trim()}`);
  let data;
  try { data = JSON.parse(result.stdout); } catch (error) { throw new Error('invalid descriptor scanner result', { cause: error }); }
  if (!data || !Array.isArray(data.entries) || !/^[a-f0-9]{64}$/.test(data.sha256 || '')
      || crypto.createHash('sha256').update(JSON.stringify(data.entries)).digest('hex') !== data.sha256
      || !Array.isArray(data.attributes) || !/^[a-f0-9]{64}$/.test(data.metadataSha256 || '')
      || crypto.createHash('sha256').update(JSON.stringify(data.attributes)).digest('hex') !== data.metadataSha256) {
    throw new Error('descriptor scanner hash mismatch');
  }
  return data;
}

export function inspectBundle(bundle, expectedHash) {
  if (expectedHash !== undefined && !/^[a-f0-9]{64}$/.test(expectedHash)) throw new Error('invalid expected hash');
  const result = scanSource(bundle, 'inspect');
  if (expectedHash !== undefined && result.sha256 !== expectedHash) throw new Error('bundle hash mismatch');
  return result;
}

const TARGETS = Object.freeze({
  PRE: 'Library/Application Support/Avid/Audio/Plug-Ins/Kirin Hypha PRE.aaxplugin',
  POST: 'Library/Application Support/Avid/Audio/Plug-Ins/Kirin Hypha POST.aaxplugin',
});

export function inspectInstalledBundle(bundle) {
  if (!Object.values(TARGETS).some(relative => bundle === `/${relative}`)) throw new Error('unexpected installed target');
  return scanSource(bundle, 'installed');
}

// This plan is only a pre-authentication record, not authorization to mutate a target.
export function preparePlan(manifestBundles, sources) {
  validateRoles(manifestBundles, sources.map(source => source.role));
  return manifestBundles.map(bundle => {
    if (bundle.install_relative !== TARGETS[bundle.role]) throw new Error('unexpected manifest destination');
    const input = sources.find(source => source.role === bundle.role);
    if (typeof input.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(input.sha256)) throw new Error('missing expected hash');
    if (typeof input.source !== 'string' || /\s/.test(input.source)) throw new Error('source path whitespace is unsupported');
    const inspected = inspectBundle(input.source, input.sha256);
    return { role: bundle.role, target: `/${bundle.install_relative}`, source: input.source, sha256: inspected.sha256, metadataSha256: inspected.metadataSha256 };
  });
}

// A private, unprivileged snapshot; it must be authenticated again by any privileged receiver.
export function snapshotBundle(bundle, expectedHash, expectedMetadataHash) {
  if (typeof expectedHash !== 'string' || !/^[a-f0-9]{64}$/.test(expectedHash)) throw new Error('missing expected hash');
  const snapshot = scanSource(bundle, 'snapshot');
  if (snapshot.sha256 !== expectedHash) throw new Error('bundle hash mismatch');
  if (expectedMetadataHash !== undefined && snapshot.metadataSha256 !== expectedMetadataHash) throw new Error('bundle metadata hash mismatch');
  verifySnapshot(snapshot, expectedHash);
  return snapshot;
}

// Validate serialized bytes as untrusted input before any future extraction.
export function verifySnapshot(snapshot, expectedHash) {
  if (!snapshot || !Array.isArray(snapshot.entries) || !Array.isArray(snapshot.files)
      || !/^[a-f0-9]{64}$/.test(expectedHash || '') || snapshot.sha256 !== expectedHash) {
    throw new Error('invalid snapshot hash or structure');
  }
  const dirs = new Set();
  const files = new Map();
  const seen = new Set();
  let total = 0;
  for (const entry of snapshot.entries) {
    if (!entry || !['file', 'directory'].includes(entry.kind)) throw new Error('invalid snapshot entry kind');
    if (typeof entry.path !== 'string' || entry.path.includes('\0') || entry.path.startsWith('/')
        || (entry.path && entry.path.split('/').some(part => !part || part === '.' || part === '..'))) {
      throw new Error('invalid snapshot path');
    }
    if (seen.has(entry.path)) throw new Error('duplicate snapshot path');
    if (!Number.isInteger(entry.mode) || entry.mode < 0 || entry.mode > 0o777 || (entry.mode & 0o022)
        || (entry.kind === 'directory' && (entry.mode & 0o500) !== 0o500)
        || (entry.kind === 'file' && !(entry.mode & 0o400))) throw new Error('unsafe snapshot mode');
    if (entry.path === '' && entry.kind !== 'directory') throw new Error('invalid snapshot root');
    if (entry.path !== '') {
      const parent = entry.path.includes('/') ? entry.path.slice(0, entry.path.lastIndexOf('/')) : '';
      if (!dirs.has(parent)) throw new Error('missing snapshot parent');
    }
    if (/[\r\n\t]/.test(entry.path)) throw new Error('unsupported tree path');
    seen.add(entry.path);
    if (entry.kind === 'directory') dirs.add(entry.path);
    else {
      if (!Number.isSafeInteger(entry.bytes) || entry.bytes < 0 || !/^[a-f0-9]{64}$/.test(entry.sha256 || '')) throw new Error('invalid file hash or size');
      total += entry.bytes;
      if (total > 256 * 1024 * 1024) throw new Error('bundle exceeds snapshot size limit');
      files.set(entry.path, entry);
    }
  }
  if (!dirs.has('')) throw new Error('missing snapshot root');
  const dataPaths = new Set();
  for (const file of snapshot.files) {
    const entry = files.get(file?.path);
    if (!entry || dataPaths.has(file.path) || typeof file.base64 !== 'string'
        || file.base64.length !== 4 * Math.ceil(entry.bytes / 3)) throw new Error('invalid snapshot file hash or size');
    const bytes = Buffer.from(file.base64, 'base64');
    if (bytes.toString('base64') !== file.base64 || bytes.length !== entry.bytes
        || crypto.createHash('sha256').update(bytes).digest('hex') !== entry.sha256) throw new Error('snapshot file hash mismatch');
    dataPaths.add(file.path);
  }
  if (dataPaths.size !== files.size) throw new Error('missing snapshot file');
  if (crypto.createHash('sha256').update(JSON.stringify(snapshot.entries)).digest('hex') !== expectedHash) throw new Error('snapshot hash mismatch');
  if (!Array.isArray(snapshot.attributes) || !/^[a-f0-9]{64}$/.test(snapshot.metadataSha256 || '')
      || crypto.createHash('sha256').update(JSON.stringify(snapshot.attributes)).digest('hex') !== snapshot.metadataSha256) {
    throw new Error('snapshot metadata hash mismatch');
  }
  if (snapshot.attributes.length !== snapshot.entries.length) throw new Error('snapshot metadata paths mismatch');
  let metadataBytes = 0;
  for (let index = 0; index < snapshot.attributes.length; index += 1) {
    const record = snapshot.attributes[index];
    if (!record || record.path !== snapshot.entries[index].path || !Array.isArray(record.values)) throw new Error('snapshot metadata path mismatch');
    const names = new Set();
    for (const value of record.values) {
      if (!value || typeof value.name !== 'string' || !value.name || value.name.includes('\0')
          || names.has(value.name) || typeof value.base64 !== 'string') throw new Error('invalid snapshot metadata');
      if (!SUPPORTED_ATTRIBUTES.includes(value.name)) throw new Error('unsupported snapshot attribute');
      if (value.base64.length > 4 * Math.ceil(64 * 1024 * 1024 / 3)) throw new Error('snapshot metadata too large');
      const bytes = Buffer.from(value.base64, 'base64');
      metadataBytes += bytes.length;
      if (metadataBytes > 64 * 1024 * 1024 || bytes.toString('base64') !== value.base64) throw new Error('invalid snapshot metadata bytes');
      names.add(value.name);
    }
  }
  return true;
}
