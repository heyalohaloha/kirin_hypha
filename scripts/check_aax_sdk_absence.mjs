#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const skippedDirectoryNames = new Set([
  '.git',
  'node_modules',
  'target',
  'dist',
  'release_state',
]);

function isBuildDirectory(name) {
  return name === 'build' || name.startsWith('build-');
}

export function aaxSdkReason(relativePath, kind = 'file') {
  const normalized = relativePath.split(path.sep).join('/');
  const segments = normalized.split('/');
  const sdkDirectory = segments.find((segment) =>
    /^(?:avid[-_. ]*)?aax[-_. ]*sdk(?:[-_. ].*)?$/i.test(segment));
  if (sdkDirectory) {
    return `AAX SDK directory name (${sdkDirectory})`;
  }

  if (kind === 'file') {
    const base = segments.at(-1) ?? '';
    if (/^AAX_[A-Za-z0-9].*\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx)$/i.test(base)) {
      return 'AAX SDK-style source/header name';
    }
    if (/aax.*sdk.*\.(?:zip|tar|tar\.gz|tgz|7z|dmg)$/i.test(base)) {
      return 'AAX SDK archive name';
    }
  }

  return null;
}

export function findAaxSdkEntries(root) {
  const absoluteRoot = path.resolve(root);
  const findings = new Map();

  function record(relative, kind = 'file') {
    const normalized = relative.split(path.sep).join('/');
    const reason = aaxSdkReason(normalized, kind);
    if (reason && !findings.has(normalized)) {
      findings.set(normalized, { path: normalized, reason });
    }
  }

  const repository = spawnSync(
    'git', ['-C', absoluteRoot, 'rev-parse', '--show-toplevel'],
    { encoding: 'utf8' },
  );
  const gitMetadata = path.join(absoluteRoot, '.git');
  if (repository.status !== 0 && fs.existsSync(gitMetadata)) {
    throw new Error(`failed to identify repository root: ${repository.stderr?.trim() ?? ''}`);
  }
  if (repository.status === 0
      && fs.realpathSync(repository.stdout.trim()) === fs.realpathSync(absoluteRoot)) {
    const tracked = spawnSync(
      'git', ['-C', absoluteRoot, 'ls-files', '--cached', '-z'],
      { encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 },
    );
    if (tracked.status !== 0) {
      throw new Error(`failed to enumerate tracked repository paths: ${tracked.stderr.trim()}`);
    }
    for (const relative of tracked.stdout.split('\0')) {
      if (relative) record(relative);
    }
  }

  function visit(absoluteDirectory, relativeDirectory) {
    for (const entry of fs.readdirSync(absoluteDirectory, { withFileTypes: true })) {
      const relative = relativeDirectory ? path.join(relativeDirectory, entry.name) : entry.name;
      record(relative, entry.isDirectory() ? 'directory' : 'file');

      if (!entry.isDirectory() || entry.isSymbolicLink()) continue;
      if (skippedDirectoryNames.has(entry.name) || isBuildDirectory(entry.name)) continue;
      visit(path.join(absoluteDirectory, entry.name), relative);
    }
  }

  visit(absoluteRoot, '');
  return [...findings.values()].sort((left, right) => left.path.localeCompare(right.path));
}

function main() {
  const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
  const root = process.argv[2] ? path.resolve(process.argv[2]) : path.resolve(scriptDirectory, '..');
  const findings = findAaxSdkEntries(root);
  if (findings.length === 0) {
    console.log('AAX SDK absence check passed (Git index when present + worktree/distribution walk)');
    return;
  }

  console.error('AAX SDK absence check failed: licensed SDK material must remain outside the repository');
  for (const finding of findings) console.error(`- ${finding.path}: ${finding.reason}`);
  process.exitCode = 1;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) main();
