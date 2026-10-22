import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const checkout = fileURLToPath(new URL('../', import.meta.url));
// Exact root-relative names, never basename/prefix matches elsewhere in storage.
// Each entry is app-owned and absent as a path component from owned native source;
// assertExclusionOwnership rechecks that evidence before both baseline operations.
export const exclusions = Object.freeze([
  { path: 'Code Cache', reason: 'Electron compiled script cache' },
  { path: 'GPUCache', reason: 'Electron GPU cache' },
  { path: 'DawnGraphiteCache', reason: 'Electron Dawn/Graphite cache' },
  { path: 'DawnWebGPUCache', reason: 'Electron Dawn/WebGPU cache' },
  { path: 'GraphiteDawnCache', reason: 'Electron Graphite/Dawn cache' },
  { path: 'DevToolsActivePort', reason: 'Electron debugging endpoint discovery' },
  { path: 'DIPS', reason: 'Electron browsing-state database' },
  { path: 'DIPS-wal', reason: 'Electron browsing-state database journal' },
  { path: 'Local Storage/leveldb', reason: 'Electron local storage database' },
  { path: 'Preferences', reason: 'Electron browser preferences' },
  { path: 'Session Storage', reason: 'Electron session storage database' },
  { path: 'SingletonCookie', reason: 'Electron single-instance cookie' },
  { path: 'SingletonLock', reason: 'Electron single-instance lock' },
  { path: 'SingletonSocket', reason: 'Electron single-instance socket' },
  { path: 'config.json', fileOnly: true, reason: 'Kirin OS userData settings via getConfigPath/writeConfig' },
  { path: '.config.json.tmp-<pid>-<counter>', pattern: '^\\.config\\.json\\.tmp-[1-9][0-9]*-[0-9]+$',
    fileOnly: true, sourcePrefix: '.config.json.tmp-', reason: 'Kirin OS atomic config sibling via atomicWriteFileSync' },
  { path: 'folder-scan', reason: 'Kirin OS folder scan object store' },
].map(Object.freeze));

export const exclusionPaths = Object.freeze(exclusions.map(item => item.path));
export const exclusionRules = Object.freeze(exclusions.map(item => item.pattern || item.fileOnly
  ? Object.freeze({ path: item.path, ...(item.pattern ? { pattern: item.pattern } : {}), fileOnly: true }) : item.path));
const folded = exclusions.filter(item => !item.pattern)
  .map(item => ({ name: item.path.toLowerCase(), fileOnly: item.fileOnly }));
const patterns = exclusions.filter(item => item.pattern).map(item => new RegExp(item.pattern, 'i'));
export function excluded(relative, nonFile = false) {
  const name = relative.toLowerCase();
  return folded.some(item => item.fileOnly ? !nonFile && name === item.name
    : name === item.name || name.startsWith(item.name + '/'))
    || !nonFile && patterns.some(pattern => pattern.test(name));
}
export function exclusionParent(relative) {
  return relative === '' || folded.some(item => item.name.startsWith(relative.toLowerCase() + '/'));
}

// Discover all owned native source, including tests and new directories. No writer API,
// resolver, source hash or saved source inventory determines snapshot coverage.
export function readOwnedSources(root = checkout) {
  root = path.resolve(root);
  const sources = new Map();
  const omitted = new Set(['.git', 'target', 'release_state', 'vendor', 'node_modules']);
  const native = /\.(rs|cpp|cc|cxx|c|h|hpp|hh|m|mm)$/i;
  const rejectLink = () => { throw Object.assign(new Error('native source symlink requires ownership review'),
    { code: 'STORAGE_EXCLUSION_SOURCE_LINK' }); };
  if (fs.lstatSync(root).isSymbolicLink()) rejectLink();
  function walk(relative) {
    for (const entry of fs.readdirSync(path.join(root, relative), { withFileTypes: true })) {
      const name = relative ? `${relative}/${entry.name}` : entry.name;
      if (omitted.has(name) || name === 'juce_shell/JUCE') continue;
      if (entry.isSymbolicLink()) {
        const target = fs.statSync(path.join(root, name), { throwIfNoEntry: false });
        if (!target || target.isDirectory() || native.test(name)) rejectLink();
        continue;
      }
      if (entry.isDirectory()) walk(name);
      else if (native.test(name)) {
        sources.set(name, fs.readFileSync(path.join(root, name), 'utf8'));
      }
    }
  }
  walk('');
  return sources;
}

function decodeLiteral(value) {
  return value.replace(/\\u\{([0-9a-fA-F]+)\}|\\u([0-9a-fA-F]{4})|\\U([0-9a-fA-F]{8})|\\x([0-9a-fA-F]{2})/g,
    (_, rust, unicode, wide, hex) => String.fromCodePoint(parseInt(rust || unicode || wide || hex, 16)))
    .replace(/\\\\/g, '/').toLowerCase();
}

export function assertExclusionOwnership(sources = readOwnedSources()) {
  // Deliberately stronger than writer inference: even a read-only literal reference
  // requires reviewing/removing the exclusion. Constants, branches, reassignments and
  // helpers cannot hide a referenced name by moving it away from a known writer.
  // Local Storage is checked as a component too, covering separate leveldb joins.
  const escape = value => value.toLowerCase().replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const forbidden = exclusions.filter(item => !item.sourcePrefix).map(item => escape(item.path.split('/')[0]));
  const prefixes = exclusions.filter(item => item.sourcePrefix).map(item => escape(item.sourcePrefix));
  const component = new RegExp(`["/\\\\(](?:(?:${forbidden.join('|')})["/\\\\)]|${prefixes.join('|')})`);
  for (const [file, code] of sources) {
    // Refuse spellings this bounded check cannot interpret, rather than silently
    // accepting a literal that can resolve to an excluded name in C/C++.
    const adjacentComment = /"\s*(?:(?:\/\*[\s\S]*?\*\/|\/\/[^\n]*(?:\n|$))\s*)+(?:u8|[uUL])?"/;
    const unsupportedEscape = /(?:^|[^\\])\\(?:[1-7][0-7]{0,2}|0[0-7]+|x[0-9a-fA-F]{3,})/m;
    if (adjacentComment.test(code) || unsupportedEscape.test(code))
      throw Object.assign(new Error(`native string spelling requires ownership review: ${file}`),
        { code: 'STORAGE_EXCLUSION_SYNTAX' });
    // Inspect text without tokenising quotes: a quote in a comment must not hide a
    // later literal. Comments containing a quoted excluded name also require review.
    const text = decodeLiteral(code).replace(/"\s*(?:\+\s*)?"/g, '');
    if (component.test(text))
      throw Object.assign(new Error(`excluded storage name referenced by native source: ${file}`),
        { code: 'STORAGE_EXCLUSION_OWNERSHIP' });
  }
  return sources.size;
}
