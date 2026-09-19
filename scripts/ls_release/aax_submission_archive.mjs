#!/usr/bin/env node
import childProcess from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

export const AAX_CONTENT_MANIFEST_SCHEMA = 'kirin-hypha-aax-submission-contents-v1';

export function runEvidenceCommand(command, args, { cwd, capture = true } = {}) {
  const result = childProcess.spawnSync(command, args, {
    cwd,
    encoding: 'utf8',
    stdio: capture ? ['ignore', 'pipe', 'pipe'] : 'inherit',
  });
  if (result.error) throw new Error(`${command} could not start: ${result.error.message}`);
  if (result.status !== 0) {
    throw new Error(`${command} failed with status ${result.status}`);
  }
  return { stdout: result.stdout || '', stderr: result.stderr || '' };
}

export function sha256Buffer(buffer) {
  return crypto.createHash('sha256').update(buffer).digest('hex');
}

export function sha256File(filePath) {
  return sha256Buffer(fs.readFileSync(filePath));
}

function safeComponent(value, label) {
  if (typeof value !== 'string'
      || value.length === 0
      || value === '.'
      || value === '..'
      || /[\/\\\0\r\n\t]/.test(value)) {
    throw new Error(`${label} must be one safe path component`);
  }
}

function safeRelative(value, label) {
  if (typeof value !== 'string'
      || value.length === 0
      || path.posix.isAbsolute(value)
      || value.includes('\\')
      || value.split('/').some((part) => part === '' || part === '.' || part === '..')
      || /[\0\r\n\t]/.test(value)) {
    throw new Error(`${label} must be a safe POSIX relative path`);
  }
}

export function resolveEvidencePath(artifactRoot, relativePath, label) {
  safeRelative(relativePath, `${label} relative_path`);
  const root = path.resolve(artifactRoot);
  const resolved = path.resolve(root, ...relativePath.split('/'));
  if (!resolved.startsWith(`${root}${path.sep}`)) {
    throw new Error(`${label} escapes the AAX artifact root`);
  }
  return resolved;
}

function modeString(stat) {
  return (stat.mode & 0o7777).toString(8).padStart(4, '0');
}

export function buildTreeManifest(rootPath, rootName = path.basename(rootPath)) {
  safeComponent(rootName, 'AAX content manifest root_name');
  if (!fs.statSync(rootPath, { throwIfNoEntry: false })?.isDirectory()) {
    throw new Error(`AAX content manifest root is missing: ${rootPath}`);
  }
  const entries = [];
  function visit(directory, prefix) {
    const children = fs.readdirSync(directory, { withFileTypes: true })
      .sort((left, right) => (left.name < right.name ? -1 : left.name > right.name ? 1 : 0));
    for (const child of children) {
      const absolute = path.join(directory, child.name);
      const relative = prefix ? `${prefix}/${child.name}` : child.name;
      safeRelative(relative, 'AAX content manifest entry path');
      const stat = fs.lstatSync(absolute);
      if (stat.isSymbolicLink()) {
        entries.push({
          path: relative,
          type: 'symlink',
          mode: modeString(stat),
          target: fs.readlinkSync(absolute),
        });
      } else if (stat.isDirectory()) {
        entries.push({ path: relative, type: 'directory', mode: modeString(stat) });
        visit(absolute, relative);
      } else if (stat.isFile()) {
        entries.push({
          path: relative,
          type: 'file',
          mode: modeString(stat),
          size_bytes: stat.size,
          sha256: sha256File(absolute),
        });
      } else {
        throw new Error(`unsupported AAX archive entry type: ${relative}`);
      }
    }
  }
  visit(path.resolve(rootPath), '');
  const manifest = { schema: AAX_CONTENT_MANIFEST_SCHEMA, root_name: rootName, entries };
  validateTreeManifest(manifest);
  return manifest;
}

export function validateTreeManifest(manifest) {
  if (manifest?.schema !== AAX_CONTENT_MANIFEST_SCHEMA) {
    throw new Error(`unsupported AAX content manifest: ${manifest?.schema || 'missing'}`);
  }
  safeComponent(manifest.root_name, 'AAX content manifest root_name');
  if (!Array.isArray(manifest.entries) || manifest.entries.length === 0) {
    throw new Error('AAX content manifest entries are missing');
  }
  let previous = '';
  for (const entry of manifest.entries) {
    safeRelative(entry?.path, 'AAX content manifest entry path');
    if (entry.path <= previous) throw new Error('AAX content manifest entries are not unique and sorted');
    previous = entry.path;
    if (!['directory', 'file', 'symlink'].includes(entry.type)) {
      throw new Error(`unsupported AAX content manifest type: ${entry.type || 'missing'}`);
    }
    if (!/^[0-7]{4}$/.test(entry.mode || '')) {
      throw new Error(`invalid AAX content manifest mode: ${entry.path}`);
    }
    if (entry.type === 'file'
        && (!Number.isSafeInteger(entry.size_bytes)
          || entry.size_bytes < 0
          || !/^[0-9a-f]{64}$/.test(entry.sha256 || ''))) {
      throw new Error(`invalid AAX content manifest file identity: ${entry.path}`);
    }
    if (entry.type === 'symlink'
        && (typeof entry.target !== 'string' || entry.target.length === 0 || /[\0\r\n]/.test(entry.target))) {
      throw new Error(`invalid AAX content manifest symlink target: ${entry.path}`);
    }
  }
  return manifest;
}

export function manifestBytes(manifest) {
  validateTreeManifest(manifest);
  return Buffer.from(`${JSON.stringify(manifest, null, 2)}\n`);
}

export function assertTreeMatchesManifest(rootPath, manifest) {
  validateTreeManifest(manifest);
  const actual = buildTreeManifest(rootPath, manifest.root_name);
  if (!Buffer.from(JSON.stringify(actual)).equals(Buffer.from(JSON.stringify(manifest)))) {
    throw new Error('AAX extracted payload does not match the submitted content manifest');
  }
  return actual;
}

export function requireBundleRoots(manifest, bundleNames) {
  validateTreeManifest(manifest);
  const wanted = [...bundleNames].sort();
  const topDirectories = manifest.entries
    .filter((entry) => !entry.path.includes('/'))
    .map((entry) => (entry.type === 'directory' ? entry.path : `${entry.path} [${entry.type}]`))
    .sort();
  if (JSON.stringify(topDirectories) !== JSON.stringify(wanted)) {
    throw new Error(`AAX content manifest must contain exactly ${wanted.join(' and ')}`);
  }
}

export function evidenceRecord(filePath, artifactRoot) {
  const stat = fs.statSync(filePath, { throwIfNoEntry: false });
  if (!stat?.isFile()) throw new Error(`AAX evidence file is missing: ${filePath}`);
  const relativePath = path.relative(path.resolve(artifactRoot), path.resolve(filePath))
    .split(path.sep).join('/');
  safeRelative(relativePath, 'AAX evidence relative_path');
  return {
    file_name: path.basename(filePath),
    relative_path: relativePath,
    size_bytes: stat.size,
    sha256: sha256File(filePath),
  };
}

export function verifyEvidenceFile(record, artifactRoot, label) {
  if (!record
      || typeof record.file_name !== 'string'
      || path.posix.basename(record.relative_path || '') !== record.file_name
      || !Number.isSafeInteger(record.size_bytes)
      || record.size_bytes <= 0
      || !/^[0-9a-f]{64}$/.test(record.sha256 || '')) {
    throw new Error(`${label} identity is invalid`);
  }
  const filePath = resolveEvidencePath(artifactRoot, record.relative_path, label);
  const stat = fs.statSync(filePath, { throwIfNoEntry: false });
  if (!stat?.isFile() || stat.size !== record.size_bytes || sha256File(filePath) !== record.sha256) {
    throw new Error(`${label} bytes do not match the receipt`);
  }
  return filePath;
}

export function persistImmutableFile(sourcePath, destinationPath) {
  const expected = sha256File(sourcePath);
  fs.mkdirSync(path.dirname(destinationPath), { recursive: true });
  try {
    fs.copyFileSync(sourcePath, destinationPath, fs.constants.COPYFILE_EXCL);
  } catch (error) {
    if (error.code !== 'EEXIST') throw error;
  }
  if (sha256File(destinationPath) !== expected) {
    throw new Error(`immutable AAX evidence path already contains different bytes: ${destinationPath}`);
  }
  return destinationPath;
}

export function parseNotarytoolLog(output, label = 'AAX notarytool log') {
  let response;
  try {
    response = JSON.parse(String(output));
  } catch (error) {
    throw new Error(`${label} did not return valid JSON: ${error.message}`);
  }
  const jobId = String(response.jobId || '').toLowerCase();
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(jobId)) {
    throw new Error(`${label} jobId is missing or invalid`);
  }
  if (response.status !== 'Accepted') {
    throw new Error(`${label} status is ${response.status || 'missing'}, expected Accepted`);
  }
  if (typeof response.archiveFilename !== 'string' || response.archiveFilename.length === 0) {
    throw new Error(`${label} archiveFilename is missing`);
  }
  if (!Number.isSafeInteger(response.logFormatVersion) || response.logFormatVersion < 1) {
    throw new Error(`${label} logFormatVersion is missing or invalid`);
  }
  if (response.issues !== null && !Array.isArray(response.issues)) {
    throw new Error(`${label} issues must be null or an array`);
  }
  const archiveSha256 = String(response.sha256 || '').toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(archiveSha256)) {
    throw new Error(`${label} archive SHA-256 is missing or invalid`);
  }
  return {
    job_id: jobId,
    status: response.status,
    archive_filename: response.archiveFilename,
    archive_sha256: archiveSha256,
    log_format_version: response.logFormatVersion,
    issues: response.issues,
  };
}

export function validateNotaryEvidenceLinks(receipt, manifest, log) {
  validateTreeManifest(manifest);
  if (receipt.submission?.id !== log.job_id) {
    throw new Error('AAX notary log job ID does not match the accepted submission');
  }
  if (receipt.submission?.status !== 'Accepted' || log.status !== 'Accepted') {
    throw new Error('AAX notarization evidence is not Accepted');
  }
  if (receipt.submission?.name !== receipt.archive?.file_name
      || log.archive_filename !== receipt.archive?.file_name) {
    throw new Error('AAX notarization archive filename does not match the submitted archive');
  }
  if (receipt.archive?.root_name !== manifest.root_name) {
    throw new Error('AAX archive root does not match the content manifest');
  }
  if (receipt.archive?.sha256 !== log.archive_sha256) {
    throw new Error('AAX archive SHA-256 does not match the Apple notary log');
  }
  return true;
}

export function extractAndVerifyArchive({ archivePath, manifest, destinationParent, runner = runEvidenceCommand }) {
  validateTreeManifest(manifest);
  if (fs.existsSync(destinationParent) && fs.readdirSync(destinationParent).length !== 0) {
    throw new Error(`AAX extraction destination is not empty: ${destinationParent}`);
  }
  fs.mkdirSync(destinationParent, { recursive: true });
  runner('ditto', ['-x', '-k', archivePath, destinationParent]);
  const children = fs.readdirSync(destinationParent).sort();
  if (children.length !== 1 || children[0] !== manifest.root_name) {
    throw new Error('AAX submission archive has an unexpected top-level layout');
  }
  const extractedRoot = path.join(destinationParent, manifest.root_name);
  assertTreeMatchesManifest(extractedRoot, manifest);
  return extractedRoot;
}

export function materializeVerifiedTree(sourceRoot, destinationRoot, manifest, runner = runEvidenceCommand) {
  if (fs.existsSync(destinationRoot)) {
    if (!fs.statSync(destinationRoot).isDirectory() || fs.readdirSync(destinationRoot).length !== 0) {
      throw new Error(`AAX materialization destination is not empty: ${destinationRoot}`);
    }
  }
  runner('ditto', [sourceRoot, destinationRoot]);
  assertTreeMatchesManifest(destinationRoot, manifest);
  return destinationRoot;
}
