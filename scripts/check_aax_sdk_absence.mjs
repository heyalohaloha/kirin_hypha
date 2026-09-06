#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
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
  const findings = [];

  function visit(absoluteDirectory, relativeDirectory) {
    for (const entry of fs.readdirSync(absoluteDirectory, { withFileTypes: true })) {
      const relative = relativeDirectory ? path.join(relativeDirectory, entry.name) : entry.name;
      const reason = aaxSdkReason(relative, entry.isDirectory() ? 'directory' : 'file');
      if (reason) findings.push({ path: relative.split(path.sep).join('/'), reason });

      if (!entry.isDirectory() || entry.isSymbolicLink()) continue;
      if (skippedDirectoryNames.has(entry.name) || isBuildDirectory(entry.name)) continue;
      visit(path.join(absoluteDirectory, entry.name), relative);
    }
  }

  visit(path.resolve(root), '');
  return findings;
}

function main() {
  const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
  const root = process.argv[2] ? path.resolve(process.argv[2]) : path.resolve(scriptDirectory, '..');
  const findings = findAaxSdkEntries(root);
  if (findings.length === 0) {
    console.log('AAX SDK absence check passed');
    return;
  }

  console.error('AAX SDK absence check failed: licensed SDK material must remain outside the repository');
  for (const finding of findings) console.error(`- ${finding.path}: ${finding.reason}`);
  process.exitCode = 1;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) main();
