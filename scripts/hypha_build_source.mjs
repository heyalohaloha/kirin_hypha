import crypto from 'node:crypto';
import childProcess from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { readReleaseSourceIdentity } from './ls_release/release_source_identity.mjs';

function worktreeFingerprint(root, commit) {
  const diff = childProcess.execFileSync('git', ['diff', '--binary', '--no-ext-diff',
    '--ignore-submodules=dirty', 'HEAD', '--'], { cwd: root, maxBuffer: 32 * 1024 * 1024 });
  const digest = crypto.createHash('sha256').update(commit).update(diff);
  const newFiles = childProcess.execFileSync('git', ['ls-files', '--others',
    '--exclude-standard', '-z'], { cwd: root }).toString().split('\0').filter(Boolean).sort();
  for (const file of newFiles) digest.update(file).update(fs.readFileSync(path.join(root, file)));
  return digest.digest('hex');
}

export function sourceSnapshot(root) {
  const source = readReleaseSourceIdentity({ root });
  const juceRoot = path.join(root, 'juce_shell', 'JUCE');
  let juce = null;
  if (fs.existsSync(path.join(juceRoot, '.git'))) {
    const commit = childProcess.execFileSync('git', ['rev-parse', 'HEAD'], { cwd: juceRoot,
      encoding: 'utf8' }).trim();
    juce = { commit, fingerprint: worktreeFingerprint(juceRoot, commit) };
  }
  return { commit: source.commit, bNumber: source.bNumber, state: source.sourceState,
    fingerprint: crypto.createHash('sha256').update(worktreeFingerprint(root, source.commit))
      .update(JSON.stringify(juce)).digest('hex'), juce };
}
