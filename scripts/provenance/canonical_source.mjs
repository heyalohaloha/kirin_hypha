import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';

const git = (root, args, env = {}) => execFileSync('git', args, {
  cwd: root, env: { ...process.env, ...env }, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024,
}).trim();
function treeEntries(text) {
  return text.split('\0').filter(Boolean).map(row => {
    const m = row.match(/^(\d+) (?:blob|commit) ([0-9a-f]{40})\t(.+)$/s);
    if (!m) throw new Error('Unexpected Git source tree entry');
    return { mode: m[1], blob: m[2], name: m[3] };
  });
}
function checkedFiles(directory, entries, prefix = '') {
  return entries.map(({ mode, blob, name }) => {
    const file = path.join(directory, name);
    if (!['100644', '100755'].includes(mode) || !fs.lstatSync(file).isFile()
        || fs.lstatSync(file).isSymbolicLink()) throw new Error('Canonical source must be a regular file');
    const bytes = fs.readFileSync(file);
    // Git hash-object --no-filters: hash the object header and the actual bytes.
    const actual = createHash('sha1').update(`blob ${bytes.length}\0`).update(bytes).digest('hex');
    if (actual !== blob) throw new Error(`Raw source differs from canonical Git blob: ${prefix}${name}`);
    return { path: prefix + name, blob, sha256: createHash('sha256').update(bytes).digest('hex') };
  });
}

export function canonicalSourceSnapshot(root) {
  const entries = treeEntries(git(root, ['ls-tree', '-r', '-z', 'HEAD']));
  const submodule = entries.find(e => e.name === 'juce_shell/JUCE' && e.mode === '160000');
  if (!submodule) throw new Error('Canonical JUCE submodule pin is missing');
  const files = checkedFiles(root, entries.filter(e => e !== submodule));
  const juce = path.join(root, submodule.name);
  if (git(juce, ['rev-parse', 'HEAD']) !== submodule.blob) throw new Error('JUCE source pin differs from adopted tree');
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-canonical-source-'));
  const env = { GIT_INDEX_FILE: path.join(temporary, 'index') };
  try {
    git(juce, ['read-tree', submodule.blob], env);
    const manifest = git(root, ['show', 'HEAD:scripts/verify_juce_patch_state.sh']);
    const patchRows = [...manifest.matchAll(/^\s*"([^"\n]+\.patch)::([^"\n]*)"\s*$/gm)];
    if (!/PATCHES=\(/.test(manifest)) throw new Error('Approved JUCE patch manifest is missing');
    for (const [, name, flags] of patchRows) {
      git(juce, ['apply', '--cached', ...flags.split(' ').filter(Boolean), path.join(root, 'juce_shell/patches', name)], env);
    }
    const jucePatchedTree = git(juce, ['write-tree'], env);
    files.push(...checkedFiles(juce, treeEntries(git(juce, ['ls-tree', '-r', '-z', jucePatchedTree])), 'juce_shell/JUCE/'));
    return { commit: git(root, ['rev-parse', 'HEAD']), tree: git(root, ['rev-parse', 'HEAD^{tree}']),
      juceCommit: submodule.blob, jucePatchedTree, files: files.sort((a,b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0) };
  } finally { fs.rmSync(temporary, { recursive: true, force: true }); }
}
