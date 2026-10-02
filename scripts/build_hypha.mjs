#!/usr/bin/env node
// One native build graph per OS, shared Rust/JUCE core, no signing or machine changes.
import childProcess from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { sourceSnapshot } from './hypha_build_source.mjs';
export { sourceSnapshot } from './hypha_build_source.mjs';
import { rejectSignedArtifacts, verifyArtifacts } from './hypha_build_artifacts.mjs';

const MODULE_PATH = fileURLToPath(import.meta.url);
export const PROJECT_ROOT = path.resolve(path.dirname(MODULE_PATH), '..');

export const HELP = `Usage: node scripts/build_hypha.mjs --sdk PATH --license-confirmed [options]
Quick local test: node scripts/build_hypha.mjs --without-aax

macOS: PRE/POST x AAX/AU/VST3, each x86_64 + arm64 (Universal).
Windows: PRE/POST x AAX/VST3, each x64. AU is Apple-only.
This is a local UNSIGNED/DIAGNOSTIC build, not a release or a retail Pro Tools gate.
No iLok, credentials, CI, installation, notarization or upload is used by this command.

Options:
  --without-aax         SDK-free local test: AU/VST3 on Mac, VST3 on Windows
  --sdk PATH             External licensed AAX SDK (or KIRIN_AAX_SDK_PATH)
  --license-confirmed    Explicit confirmation for this SDK use
  --platform macos|windows  Auto-detected; cross-OS planning only with --dry-run
  --arch universal|x64  Must match the supported OS matrix; Windows ARM64 is not assumed
  --jobs N              Native build parallelism (default: 2)
  --build-id ID         Separate output/cache under target/hypha-build (default: default)
  --dry-run             Read-only validation and command plan; no build tools run
  --verify-only         Recheck the existing build manifest/binaries; no rebuild
  --help                Show this entry and the signing/release boundary

Output: target/hypha-build/<macos-universal|windows-x64>[-ID]/hypha-build.json
SDK-free output has a separate -no-aax directory; the default still builds all formats.
Signing/distribution: docs/aax_build_signing_entry.md and docs/ls_release/kirin_hypha_ls_runbook.md
End-to-end through HP: node scripts/build_hypha.mjs --release --help
`;

export function parseArgs(argv, env = process.env) {
  const options = { sdk: env.KIRIN_AAX_SDK_PATH || '', licenseConfirmed: false,
    platform: '', arch: '', jobs: 2, buildId: 'default', dryRun: false, verifyOnly: false, withoutAax: false };
  const values = { '--sdk': 'sdk', '--platform': 'platform', '--arch': 'arch',
    '--jobs': 'jobs', '--build-id': 'buildId' };
  for (let index = 0; index < argv.length; index++) {
    const arg = argv[index];
    if (Object.hasOwn(values, arg)) {
      const value = argv[++index];
      if (!value || value.startsWith('--')) throw new Error(`${arg} requires a value`);
      options[values[arg]] = value;
    } else if (arg === '--license-confirmed') options.licenseConfirmed = true;
    else if (arg === '--without-aax') options.withoutAax = true;
    else if (arg === '--dry-run') options.dryRun = true;
    else if (arg === '--verify-only') options.verifyOnly = true;
    else if (arg === '--help' || arg === '-h') options.help = true;
    else throw new Error(`Unknown option: ${arg}`);
  }
  return options;
}

function contained(parent, candidate) {
  const relative = path.relative(parent, candidate);
  return relative === '' || (!relative.startsWith(`..${path.sep}`)
    && relative !== '..' && !path.isAbsolute(relative));
}

function rejectSymlinkAncestors(root, candidate) {
  let current = candidate;
  while (contained(root, current)) {
    if (fs.lstatSync(current, { throwIfNoEntry: false })?.isSymbolicLink()) {
      throw new Error('Build/cache path must not traverse a symlink');
    }
    if (current === root) break;
    current = path.dirname(current);
  }
}

export function createPlan(options, { root = PROJECT_ROOT, host = process.platform } = {}) {
  root = fs.realpathSync(root);
  const platform = options.platform || ({ darwin: 'macos', win32: 'windows' }[host]);
  if (!['macos', 'windows'].includes(platform)) throw new Error('Only macOS and Windows are supported');
  const arch = platform === 'macos' ? 'universal' : 'x64';
  if (options.arch && options.arch !== arch) {
    throw new Error(`Supported ${platform} architecture is ${arch}; ARM64/Arm64X Windows AAX is not qualified`);
  }
  if (!options.dryRun && ({ macos: 'darwin', windows: 'win32' }[platform]) !== host) {
    throw new Error('Run the actual build on the selected OS; cross-OS use is dry-run only');
  }
  if (options.verifyOnly && options.dryRun) throw new Error('--verify-only and --dry-run are exclusive');
  if (!/^[1-9]\d*$/.test(String(options.jobs)) || Number(options.jobs) > 64) {
    throw new Error('--jobs must be an integer from 1 to 64');
  }
  if (!/^[a-z0-9][a-z0-9_-]{0,63}$/.test(options.buildId)) throw new Error('Invalid --build-id');
  const withAax = !options.withoutAax;
  const formats = (platform === 'macos' ? ['AAX', 'AU', 'VST3'] : ['AAX', 'VST3'])
    .filter(format => withAax || format !== 'AAX');
  const label = `${platform}-${arch}${withAax ? '' : '-no-aax'}`;
  const buildDir = path.join(root, 'target', 'hypha-build',
    `${label}${options.buildId === 'default' ? '' : `-${options.buildId}`}`);
  rejectSymlinkAncestors(root, buildDir);
  const version = fs.readFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'utf8')
    .match(/^version\s*=\s*"(\d+\.\d+\.\d+)"/m)?.[1];
  if (!version) throw new Error('Hypha version missing');
  let sdk;
  if (withAax && !options.verifyOnly) {
    if (!options.sdk) throw new Error('--sdk or KIRIN_AAX_SDK_PATH is required');
    if (!options.licenseConfirmed) throw new Error('--license-confirmed is required');
    sdk = fs.realpathSync(path.resolve(root, options.sdk));
    if (contained(root, sdk)) throw new Error('AAX SDK must remain outside the repository');
    if (!fs.statSync(path.join(sdk, 'Interfaces', 'ACF'), { throwIfNoEntry: false })?.isDirectory()) {
      throw new Error('AAX SDK must contain Interfaces/ACF');
    }
  }
  const command = (tool, args) => ({ tool, args });
  const preparation = [command('bash', ['scripts/apply_juce_patches.sh']),
    command('bash', ['scripts/verify_juce_patch_state.sh'])];
  const targets = platform === 'macos'
    ? ['x86_64-apple-darwin', 'aarch64-apple-darwin'] : ['x86_64-pc-windows-msvc'];
  const commands = targets.map((target) => command('cargo',
    ['build', '--release', '-p', 'kirin_hypha_ffi', '--target', target, '--locked']));
  const ffiLibrary = platform === 'macos'
    ? path.join(buildDir, 'ffi', 'libkirin_hypha_ffi.a')
    : path.join(root, 'target', targets[0], 'release', 'kirin_hypha_ffi.lib');
  if (platform === 'macos') commands.push(command('lipo', ['-create',
    ...targets.map((target) => path.join(root, 'target', target, 'release', 'libkirin_hypha_ffi.a')),
    '-output', ffiLibrary]));
  const configure = ['-S', path.join(root, 'juce_shell'), '-B', buildDir,
    '-DCMAKE_BUILD_TYPE=Release', `-DKIRIN_FFI_LIB=${ffiLibrary}`,
    `-DKIRIN_HYPHA_AAX_SDK_PATH=${sdk || ''}`,
    `-DKIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED=${withAax ? 'ON' : 'OFF'}`,
    `-DKIRIN_HYPHA_REQUIRE_AAX=${withAax ? 'ON' : 'OFF'}`, '-DKIRIN_HYPHA_AAX_DISTRIBUTION_BUILD=OFF',
    '-DKIRIN_HYPHA_KIMERA_FONT_FILE=', '-DKIRIN_HYPHA_KIMERA_APP_LICENSE_CONFIRMED=OFF'];
  if (platform === 'macos') configure.push('-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64');
  else configure.push('-G', 'Visual Studio 17 2022', '-A', 'x64');
  commands.push(command('cmake', configure), command('cmake', ['--build', buildDir,
    '--config', 'Release', '--target',
    ...['PRE', 'POST'].flatMap((role) => formats.map((format) => `KirinHypha${role}_${format}`)),
    '--parallel', String(options.jobs)]));
  return { root, platform, arch, label, buildDir, version, formats, preparation, commands,
    manifestPath: path.join(buildDir, 'hypha-build.json') };
}

export function commandRunner(root) {
  return (tool, args, { capture = false, allowFailure = false, includeStderr = false } = {}) => {
    const result = childProcess.spawnSync(tool, args, { cwd: root, encoding: 'utf8',
      stdio: capture ? ['ignore', 'pipe', 'pipe'] : 'inherit', maxBuffer: 32 * 1024 * 1024 });
    if (!allowFailure && (result.error || result.status !== 0)) {
      throw new Error(`${tool} failed (exit ${result.status ?? 'unavailable'})`);
    }
    return (result.stdout || '') + (includeStderr ? result.stderr || '' : '');
  };
}

export function executePlan(plan, options, { run = commandRunner(plan.root),
  snapshot = sourceSnapshot, verify = verifyArtifacts, log = console.log } = {}) {
  if (options.dryRun) {
    log(JSON.stringify({ platform: plan.label, formats: plan.formats,
      expectedBundles: plan.formats.length * 2, output: plan.buildDir,
      signing: 'not performed', commands: [...plan.preparation, ...plan.commands] }, null, 2));
    return;
  }
  if (options.verifyOnly) {
    const saved = JSON.parse(fs.readFileSync(plan.manifestPath, 'utf8'));
    if (saved.schema !== 'kirin-hypha-local-build-v1' || saved.platform !== plan.label
        || saved.version !== plan.version || saved.source.fingerprint !== snapshot(plan.root).fingerprint) {
      throw new Error('Build manifest does not match current source/platform/version');
    }
    const measured = verify(plan, run);
    if (JSON.stringify(measured) !== JSON.stringify(saved.artifacts)) throw new Error('Artifact manifest mismatch');
    log(`[hypha-build] PASS: ${measured.length} verified bundles; no rebuild`);
    return saved;
  }
  fs.mkdirSync(plan.buildDir, { recursive: true });
  const lock = path.join(plan.buildDir, '.hypha-build.lock');
  const fd = fs.openSync(lock, 'wx');
  let invalidated = false;
  try {
    rejectSignedArtifacts(plan.platform, plan.buildDir, run);
    for (const { tool, args } of plan.preparation) run(tool, args);
    const before = snapshot(plan.root);
    if (fs.existsSync(plan.manifestPath)) fs.unlinkSync(plan.manifestPath);
    invalidated = true;
    fs.mkdirSync(path.join(plan.buildDir, 'ffi'), { recursive: true });
    for (const { tool, args } of plan.commands) run(tool, args);
    const artifacts = verify(plan, run);
    const after = snapshot(plan.root);
    if (JSON.stringify(before) !== JSON.stringify(after)) {
      throw new Error('Source changed during build; refusing a mixed-source success manifest');
    }
    const result = { schema: 'kirin-hypha-local-build-v1', generatedAt: new Date().toISOString(),
      platform: plan.label, version: plan.version, source: after,
      buildOnly: true, signed: false, notarized: false, notForDistribution: true,
      hostAcceptance: 'not tested', artifacts };
    fs.writeFileSync(plan.manifestPath, `${JSON.stringify(result, null, 2)}\n`, { flag: 'wx' });
    log(`[hypha-build] PASS: ${artifacts.length} verified bundles -> ${plan.manifestPath}`);
    return result;
  } catch (error) {
    if (invalidated && fs.existsSync(plan.manifestPath)) fs.unlinkSync(plan.manifestPath);
    throw error;
  } finally {
    fs.closeSync(fd);
    fs.unlinkSync(lock);
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === MODULE_PATH) {
  try {
    if (process.argv.includes('--release')) {
      const { runReleaseCli } = await import('./release_hypha.mjs');
      await runReleaseCli(process.argv.slice(2).filter(arg => arg !== '--release'));
    } else {
      const options = parseArgs(process.argv.slice(2));
      if (options.help) console.log(HELP);
      else executePlan(createPlan(options), options);
    }
  } catch (error) {
    console.error(`[hypha-build] ERROR: ${error.message}`);
    process.exitCode = 1;
  }
}
