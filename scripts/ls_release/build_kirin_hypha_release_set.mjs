#!/usr/bin/env node
import childProcess from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(SCRIPT_DIR, '..', '..');

function usage() {
  return `Usage:
  node scripts/ls_release/build_kirin_hypha_release_set.mjs [options]

Builds the release artifact set:
  1. Windows readiness/preflight gates
  2. Windows VST3 LS zip from a windows-latest artifact
  3. macOS signed/notarized installer pkg
  4. macOS HP zip

Options:
  --windows-artifact-dir <dir>          Root containing Windows PRE/POST .vst3 bundles.
  --windows-external-validation <state> complete|pending. Default: pending
  --skip-windows-package                Only for diagnostics; release output is not complete.
  --skip-macos-pkg                      Only for diagnostics; release output is not complete.
  --skip-hp-zip                         Only for diagnostics; release output is not complete.
  --help                                Show this help.
`;
}

function parseArgs(argv) {
  const opts = {
    windowsArtifactDir: process.env.KIRIN_WINDOWS_ARTIFACT_DIR || '',
    windowsExternalValidation: process.env.KIRIN_WINDOWS_EXTERNAL_VALIDATION || 'pending',
    skipWindowsPackage: false,
    skipMacosPkg: false,
    skipHpZip: false,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--windows-artifact-dir') {
      opts.windowsArtifactDir = requireValue(argv, ++i, arg);
    } else if (arg === '--windows-external-validation') {
      opts.windowsExternalValidation = requireValue(argv, ++i, arg);
    } else if (arg === '--skip-windows-package') {
      opts.skipWindowsPackage = true;
    } else if (arg === '--skip-macos-pkg') {
      opts.skipMacosPkg = true;
    } else if (arg === '--skip-hp-zip') {
      opts.skipHpZip = true;
    } else if (arg === '--help' || arg === '-h') {
      opts.help = true;
    } else {
      throw new Error(`unknown option: ${arg}`);
    }
  }
  return opts;
}

function requireValue(argv, index, name) {
  const value = argv[index];
  if (!value) throw new Error(`${name} requires a value`);
  return value;
}

function log(message) {
  console.log(`[build-kirin-hypha-release-set] ${message}`);
}

function run(command, args) {
  log(`${command} ${args.map((arg) => (/\s/.test(arg) ? JSON.stringify(arg) : arg)).join(' ')}`);
  const result = childProcess.spawnSync(command, args, {
    cwd: ROOT,
    encoding: 'utf8',
    stdio: 'inherit',
  });
  if (result.status !== 0) throw new Error(`${command} exited with ${result.status}`);
}

function requireWindowsArtifactDir(value) {
  if (value) {
    const abs = path.resolve(ROOT, value);
    if (fs.existsSync(abs)) return value;
    throw new Error(`explicit Windows artifact dir missing: ${value}`);
  }
  const candidates = [
    'dist/WINDOWS_CI/kirin-hypha-windows-vst3',
    'juce_shell/build-windows',
  ];
  for (const candidate of candidates) {
    const abs = path.resolve(ROOT, candidate);
    if (fs.existsSync(abs)) return candidate;
  }
  throw new Error(
    'Windows package is required for a complete release set. Download the GitHub Actions artifact ' +
      '`kirin-hypha-windows-vst3` to dist/WINDOWS_CI/kirin-hypha-windows-vst3, or pass --windows-artifact-dir.',
  );
}

function runMain() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    console.log(usage());
    return;
  }

  run('cargo', ['run', '--package', 'xtask', '--', 'windows-readiness']);
  run('cargo', ['run', '--package', 'xtask', '--', 'windows-preflight']);

  if (opts.skipWindowsPackage) {
    log('SKIP Windows VST3 package; release set is incomplete.');
  } else {
    const artifactDir = requireWindowsArtifactDir(opts.windowsArtifactDir);
    run('node', [
      'scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs',
      '--artifact-dir',
      artifactDir,
      '--release-kind',
      'ls',
      '--external-validation',
      opts.windowsExternalValidation,
    ]);
  }

  if (opts.skipMacosPkg) {
    log('SKIP macOS LS pkg; release set is incomplete.');
  } else {
    run('node', ['scripts/ls_release/build_kirin_hypha_pkg.mjs']);
  }

  if (opts.skipHpZip) {
    log('SKIP macOS HP zip; release set is incomplete.');
  } else {
    run('cargo', ['run', '--package', 'xtask', '--', 'release-package']);
  }
}

try {
  runMain();
} catch (error) {
  console.error(`[build-kirin-hypha-release-set] ERROR: ${error.message}`);
  process.exit(1);
}
