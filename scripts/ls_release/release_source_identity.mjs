#!/usr/bin/env node
import childProcess from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const MODULE_PATH = fileURLToPath(import.meta.url);
const DEFAULT_ROOT = path.resolve(path.dirname(MODULE_PATH), '..', '..');

function run(root, command, args) {
  const result = childProcess.spawnSync(command, args, {
    cwd: root,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  if (result.status !== 0) {
    const detail = `${result.stdout || ''}${result.stderr || ''}`.trim();
    throw new Error(`${command} ${args.join(' ')} failed${detail ? `: ${detail}` : ''}`);
  }
  return (result.stdout || '').trim();
}

export function readReleaseSourceIdentity({ root = DEFAULT_ROOT, requireBNumber = true } = {}) {
  const resolvedRoot = path.resolve(root);
  const commit = run(resolvedRoot, 'git', ['rev-parse', 'HEAD']);
  if (!/^[0-9a-f]{40}$/.test(commit)) throw new Error(`invalid release commit: ${commit}`);
  const subject = run(resolvedRoot, 'git', ['log', '-1', '--pretty=%s']);
  let bNumber = subject.match(/\bB-\d+\b/)?.[0];
  let bNumberSourceCommit = commit;
  if (!bNumber) {
    // A squash merge may omit the build number. Only a byte-identical tree can
    // supply it; the release commit itself remains HEAD, never the PR commit.
    const tree = run(resolvedRoot, 'git', ['rev-parse', 'HEAD^{tree}']);
    const matches = run(resolvedRoot, 'git', ['log', '--all', '--format=%H%x09%T%x09%s'])
      .split('\n').map(line => line.split('\t')).filter(row => row[1] === tree)
      .map(row => ({ commit: row[0], number: row[2]?.match(/\bB-(\d+)\b/)?.[1] }))
      .filter(row => row.number).sort((a, b) => Number(b.number) - Number(a.number));
    if (!matches.length && requireBNumber) throw new Error('release commit subject has no B number and no identical-tree numbered commit');
    bNumber = matches.length ? `B-${matches[0].number}` : null;
    bNumberSourceCommit = matches[0]?.commit;
  }
  const dirtyEntries = run(resolvedRoot, 'git', [
    'status', '--porcelain=1', '--untracked-files=all', '--ignore-submodules=dirty',
  ]).split('\n').filter(Boolean);
  return {
    commit,
    shortCommit: commit.slice(0, 12),
    bNumber,
    ...(bNumberSourceCommit && bNumberSourceCommit !== commit ? { bNumberSourceCommit } : {}),
    sourceState: dirtyEntries.length === 0 ? 'clean source' : 'modified source',
    dirtyEntries,
  };
}

export function requireCleanReleaseSource({ root = DEFAULT_ROOT } = {}) {
  const resolvedRoot = path.resolve(root);
  const identity = readReleaseSourceIdentity({ root: resolvedRoot });
  if (identity.dirtyEntries.length > 0) {
    throw new Error(
      `release source is not clean:\n${identity.dirtyEntries.join('\n')}`,
    );
  }
  run(resolvedRoot, 'bash', ['scripts/verify_juce_patch_state.sh']);
  return identity;
}

function parseArgs(argv) {
  const options = { requireClean: false, field: '' };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === '--require-clean') options.requireClean = true;
    else if (arg === '--field') options.field = argv[++index] || '';
    else if (arg === '--help' || arg === '-h') options.help = true;
    else throw new Error(`unknown option: ${arg}`);
  }
  return options;
}

function runCli(argv) {
  const options = parseArgs(argv);
  if (options.help) {
    console.log('Usage: node release_source_identity.mjs [--require-clean] [--field commit|shortCommit|bNumber|sourceState]');
    return;
  }
  const identity = options.requireClean
    ? requireCleanReleaseSource()
    : readReleaseSourceIdentity();
  if (options.field) {
    if (!['commit', 'shortCommit', 'bNumber', 'sourceState'].includes(options.field)) {
      throw new Error(`unsupported field: ${options.field}`);
    }
    console.log(identity[options.field]);
  } else {
    console.log(JSON.stringify(identity, null, 2));
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === MODULE_PATH) {
  try {
    runCli(process.argv.slice(2));
  } catch (error) {
    console.error(`[release-source-identity] ERROR: ${error.message}`);
    process.exitCode = 1;
  }
}
