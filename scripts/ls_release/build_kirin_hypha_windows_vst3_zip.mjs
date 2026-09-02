#!/usr/bin/env node
import childProcess from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const THIS_FILE = fileURLToPath(import.meta.url);
const SCRIPT_DIR = path.dirname(THIS_FILE);
const ROOT = path.resolve(SCRIPT_DIR, '..', '..');
const VERSION = readCargoVersion();
const DEFAULT_ARTIFACT_DIR = process.env.KIRIN_WINDOWS_ARTIFACT_DIR
  || (process.env.GITHUB_ACTIONS ? 'juce_shell/build-windows' : 'dist/WINDOWS_CI/kirin-hypha-windows-vst3');

function usage() {
  return `Usage:
  node scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs [options]

Options:
  --artifact-dir <dir>          Root containing Kirin Hypha PRE/POST .vst3 bundles.
                                Default: ${DEFAULT_ARTIFACT_DIR}
  --output-dir <dir>            Output dir. Default: dist/WINDOWS_LS
  --release-kind <ls|ci|beta>   Package intent. Default: ls
  --external-validation <state> complete|pending. Default: pending
  --payload-signing <state>     signed|unsigned. Default: unsigned
  --b-number <B-000>            Release B number. Default: inferred from HEAD subject.
  --commit <sha>                Source commit. Default: git rev-parse HEAD.
  --run-url <url>               GitHub Actions run URL.
  --artifact-name <name>        CI artifact name. Default: kirin-hypha-windows-vst3
  --help                        Show this help.
`;
}

export function parseArgs(argv) {
  if (argv.includes('--help') || argv.includes('-h')) return { help: true };
  const opts = {
    artifactDir: DEFAULT_ARTIFACT_DIR,
    outputDir: 'dist/WINDOWS_LS',
    releaseKind: 'ls',
    externalValidation: process.env.KIRIN_WINDOWS_EXTERNAL_VALIDATION || 'pending',
    payloadSigning: process.env.KIRIN_WINDOWS_PAYLOAD_SIGNING || 'unsigned',
    bNumber: process.env.KIRIN_B_NUMBER || '',
    commit: process.env.KIRIN_COMMIT || '',
    runUrl: process.env.KIRIN_GITHUB_RUN_URL || inferRunUrl(),
    artifactName: process.env.KIRIN_WINDOWS_ARTIFACT_NAME || 'kirin-hypha-windows-vst3',
  };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--artifact-dir') {
      opts.artifactDir = requireValue(argv, ++i, arg);
    } else if (arg === '--output-dir') {
      opts.outputDir = requireValue(argv, ++i, arg);
    } else if (arg === '--release-kind') {
      opts.releaseKind = requireValue(argv, ++i, arg);
    } else if (arg === '--external-validation') {
      opts.externalValidation = requireValue(argv, ++i, arg);
    } else if (arg === '--payload-signing') {
      opts.payloadSigning = requireValue(argv, ++i, arg);
    } else if (arg === '--b-number') {
      opts.bNumber = requireValue(argv, ++i, arg);
    } else if (arg === '--commit') {
      opts.commit = requireValue(argv, ++i, arg);
    } else if (arg === '--run-url') {
      opts.runUrl = requireValue(argv, ++i, arg);
    } else if (arg === '--artifact-name') {
      opts.artifactName = requireValue(argv, ++i, arg);
    } else {
      throw new Error(`unknown option: ${arg}`);
    }
  }
  if (!['ls', 'ci', 'beta'].includes(opts.releaseKind)) {
    throw new Error(`--release-kind must be ls, ci, or beta: ${opts.releaseKind}`);
  }
  if (!['complete', 'pending'].includes(opts.externalValidation)) {
    throw new Error(`--external-validation must be complete or pending: ${opts.externalValidation}`);
  }
  if (!['signed', 'unsigned'].includes(opts.payloadSigning)) {
    throw new Error(`--payload-signing must be signed or unsigned: ${opts.payloadSigning}`);
  }
  if (!opts.bNumber) opts.bNumber = inferBNumber();
  if (!opts.commit) opts.commit = git(['rev-parse', 'HEAD'], 'unknown');
  return opts;
}

function requireValue(argv, index, name) {
  const value = argv[index];
  if (!value) throw new Error(`${name} requires a value`);
  return value;
}

function log(message) {
  console.log(`[build-kirin-hypha-windows-vst3-zip] ${message}`);
}

function run(command, args, options = {}) {
  log(`${command} ${args.map(shellish).join(' ')}`);
  const result = childProcess.spawnSync(command, args, {
    cwd: ROOT,
    encoding: 'utf8',
    stdio: options.capture ? ['ignore', 'pipe', 'pipe'] : 'inherit',
  });
  if (result.status !== 0) {
    const detail = options.capture ? `${result.stdout || ''}${result.stderr || ''}`.trim() : '';
    throw new Error(`${command} exited with ${result.status}${detail ? `\n${detail}` : ''}`);
  }
  return result.stdout || '';
}

function shellish(value) {
  return /\s/.test(value) ? JSON.stringify(value) : value;
}

function git(args, fallback) {
  try {
    return run('git', args, { capture: true }).trim() || fallback;
  } catch {
    return fallback;
  }
}

function inferBNumber() {
  const subject = git(['log', '-1', '--pretty=%s'], '');
  const match = subject.match(/\bB-\d+\b/);
  return match ? match[0] : 'B-UNKNOWN';
}

function inferRunUrl() {
  const server = process.env.GITHUB_SERVER_URL;
  const repo = process.env.GITHUB_REPOSITORY;
  const runId = process.env.GITHUB_RUN_ID;
  if (server && repo && runId) return `${server}/${repo}/actions/runs/${runId}`;
  return process.env.KIRIN_GITHUB_RUN_ID ? `https://github.com/heyalohaloha/kirin_hypha/actions/runs/${process.env.KIRIN_GITHUB_RUN_ID}` : '';
}

function readCargoVersion() {
  const toml = fs.readFileSync(path.join(ROOT, 'crates/hypha_pre/Cargo.toml'), 'utf8');
  const match = toml.match(/^version\s*=\s*"([^"]+)"/m);
  if (!match) throw new Error('version not found in crates/hypha_pre/Cargo.toml');
  return match[1];
}

function sha256Hex(filePath) {
  const hash = crypto.createHash('sha256');
  hash.update(fs.readFileSync(filePath));
  return hash.digest('hex');
}

function rel(filePath) {
  return path.relative(ROOT, filePath).split(path.sep).join('/');
}

function walkDirs(root, visitor) {
  if (!fs.existsSync(root)) return;
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const full = path.join(root, entry.name);
    if (!entry.isDirectory()) continue;
    visitor(full);
    walkDirs(full, visitor);
  }
}

function findBundle(root, bundleName) {
  const absRoot = path.resolve(ROOT, root);
  if (!fs.existsSync(absRoot)) {
    throw new Error(`Windows artifact dir missing: ${root}. Download the CI artifact '${process.env.KIRIN_WINDOWS_ARTIFACT_NAME || 'kirin-hypha-windows-vst3'}' or pass --artifact-dir.`);
  }
  const matches = [];
  if (path.basename(absRoot) === bundleName) matches.push(absRoot);
  walkDirs(absRoot, (dir) => {
    if (path.basename(dir) === bundleName) matches.push(dir);
  });
  if (matches.length !== 1) {
    throw new Error(`expected exactly one ${bundleName} under ${root}, found ${matches.length}`);
  }
  return matches[0];
}

function verifyBundle(bundle) {
  if (!fs.existsSync(bundle.dir)) throw new Error(`${bundle.label} bundle missing: ${bundle.dir}`);
  if (!fs.existsSync(bundle.binary)) throw new Error(`${bundle.label} binary missing: ${bundle.binary}`);
  const size = fs.statSync(bundle.binary).size;
  if (size <= 0) throw new Error(`${bundle.label} binary is empty: ${bundle.binary}`);
  if (!fs.existsSync(bundle.moduleInfo)) throw new Error(`${bundle.label} moduleinfo missing: ${bundle.moduleInfo}`);
  const moduleInfo = fs.readFileSync(bundle.moduleInfo, 'utf8');
  for (const needle of [`"Version": "${VERSION}"`, `"Name": "${bundle.displayName}"`]) {
    if (!moduleInfo.includes(needle)) {
      throw new Error(`${bundle.label} moduleinfo does not contain ${needle}`);
    }
  }
}

function copyRequired(src, dst) {
  if (!fs.existsSync(src)) throw new Error(`required file missing: ${src}`);
  fs.copyFileSync(src, dst);
}

function writeReadme(filePath, version, payloadSigning) {
  fs.writeFileSync(filePath, `Kirin Hypha ${version} Windows VST3

This fallback package contains the PRE and POST Windows VST3 bundles.
For normal installation, use Kirin-Hypha-${version}-Windows-x64-Setup.exe.
This ZIP is retained for diagnostics and recovery, not as the primary distribution.
Payload Authenticode state: ${payloadSigning}

Install target:
%LOCALAPPDATA%\\Programs\\Common\\VST3

PowerShell:
New-Item -ItemType Directory -Force "$env:LOCALAPPDATA\\Programs\\Common\\VST3"
Copy-Item -Recurse -Force ".\\Kirin Hypha PRE.vst3" "$env:LOCALAPPDATA\\Programs\\Common\\VST3\\"
Copy-Item -Recurse -Force ".\\Kirin Hypha POST.vst3" "$env:LOCALAPPDATA\\Programs\\Common\\VST3\\"

See WINDOWS_EXTERNAL_VALIDATION.md for the full validation checklist.
`);
}

export function commitReceiptFor(opts) {
  const runLine = opts.runUrl ? `GitHub Actions run: ${opts.runUrl}` : 'GitHub Actions run: unknown';
  return `Kirin Hypha ${VERSION} Windows VST3
Commit: ${opts.commit}
B-number: ${opts.bNumber}
${runLine}
Artifact name: ${opts.artifactName}
CI gates: Analysis contract, UI render, exact audio transparency, layout verify, pluginval strictness 5, artifact packaging
External validation: ${opts.externalValidation}
`;
}

function writeCommit(filePath, opts) {
  fs.writeFileSync(filePath, commitReceiptFor(opts));
}

function listFiles(root) {
  const out = [];
  function visit(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) visit(full);
      if (entry.isFile()) out.push(full);
    }
  }
  visit(root);
  return out.sort((a, b) => a.localeCompare(b));
}

function writeShaSums(packageRoot) {
  const filePath = path.join(packageRoot, 'SHA256SUMS.txt');
  const lines = listFiles(packageRoot)
    .filter((item) => item !== filePath)
    .map((item) => {
      const relative = path.relative(packageRoot, item).split(path.sep).join('/');
      return `${sha256Hex(item)}  ./${relative}`;
    });
  fs.writeFileSync(filePath, `${lines.join('\n')}\n`);
}

function zipPackage(packageRoot, zipPath) {
  fs.rmSync(zipPath, { force: true });
  if (process.platform === 'win32') {
    try {
      run('tar', ['-a', '-c', '-f', zipPath, '-C', path.dirname(packageRoot), path.basename(packageRoot)]);
      return;
    } catch (error) {
      log(`tar zip failed, falling back to PowerShell ZipFile: ${error.message}`);
    }
    const parent = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-winzip-'));
    fs.cpSync(packageRoot, path.join(parent, path.basename(packageRoot)), { recursive: true });
    const command = [
      'Add-Type -AssemblyName System.IO.Compression.FileSystem;',
      `if (Test-Path -LiteralPath ${psq(zipPath)}) { Remove-Item -LiteralPath ${psq(zipPath)} -Force }`,
      `[System.IO.Compression.ZipFile]::CreateFromDirectory(${psq(parent)}, ${psq(zipPath)})`,
    ].join(' ');
    try {
      run('powershell', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', command]);
    } finally {
      fs.rmSync(parent, { recursive: true, force: true });
    }
  } else {
    run('ditto', ['-c', '-k', '--norsrc', '--noqtn', '--keepParent', packageRoot, zipPath]);
  }
}

function psq(value) {
  return `'${value.replace(/'/g, "''")}'`;
}

function validateZip(zipPath) {
  if (process.platform === 'win32') {
    const command = [
      'Add-Type -AssemblyName System.IO.Compression.FileSystem;',
      `$z=[System.IO.Compression.ZipFile]::OpenRead(${psq(zipPath)});`,
      'if ($z.Entries.Count -le 0) { throw "zip has no entries" }',
      '$z.Dispose();',
    ].join(' ');
    run('powershell', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', command]);
  } else {
    run('unzip', ['-t', zipPath]);
  }
}

function artifactPurposeFor(releaseKind) {
  return releaseKind === 'ls' ? 'windows_ls_vst3_zip' : `windows_${releaseKind}_vst3_zip`;
}

export function artifactManifestFor(opts, packageBase, zipPath, packageRoot, bundles) {
  const externalComplete = opts.externalValidation === 'complete';
  const size = fs.statSync(zipPath).size;
  const contents = listFiles(packageRoot).map((item) => ({
    path: path.relative(packageRoot, item).split(path.sep).join('/'),
    sha256: sha256Hex(item),
  }));
  return {
    schema: 'kirin-hypha-windows-vst3-fallback-artifact-v3',
    schema_version: 3,
    product: {
      name: 'Kirin Hypha',
      version: VERSION,
    },
    generated_at: new Date().toISOString(),
    purpose: artifactPurposeFor(opts.releaseKind),
    known_limitations: [
      'Fallback manual package; the Inno Setup EXE is the primary Windows distribution.',
      'The ZIP container itself has no Authenticode surface; inspect the embedded VST3 binaries.',
      'Do not replace the primary Windows installer with this recovery artifact.',
    ],
    package: {
      name: `${packageBase}.zip`,
      path: rel(zipPath),
      size_bytes: size,
      sha256: sha256Hex(zipPath),
      format: 'zip',
      contains_top_level_folder: true,
      distribution_role: 'fallback_only',
      payload_signing: opts.payloadSigning,
    },
    binary_artifact: {
      source: 'GitHub Actions artifact or windows-latest build output',
      artifact_name: opts.artifactName,
      run_url: opts.runUrl || null,
      commit: opts.commit,
      b_number: opts.bNumber,
      ci_result: opts.releaseKind === 'ci' ? 'windows_job_success' : 'windows_job_success_required',
      windows_job: 'windows VST3 preflight',
      windows_job_gates: [
        'Build kirin_hypha_ffi staticlib (MSVC)',
        'Validate two-slot continuous Analysis contract',
        'Windows preflight gate',
        'Apply tracked JUCE patches',
        'Configure JUCE VST3 shell (Windows)',
        'Validate Windows UI render contract',
        'Build JUCE VST3 shell (Windows)',
        'Validate Windows VST3 audio transparency',
        'Verify Windows VST3 artifacts',
        'Upload Windows VST3 artifacts',
        'Validate Windows VST3 with pluginval',
        'Build Windows installer and sign all executable surfaces',
        'Verify Windows installer install, same-version reinstall, signatures, and uninstall',
        'Upload primary Windows installer',
        'Package fallback Windows VST3 ZIP',
        'Upload fallback Windows VST3 ZIP',
      ],
    },
    external_validation: {
      status: externalComplete ? 'complete' : 'pending',
      note: externalComplete
        ? 'Windows DAW validation was reported complete before this artifact was produced.'
        : 'Windows DAW validation is pending.',
    },
    contents,
    validation: {
      local_zip_test: 'zip integrity test passed',
      local_sha256_file: `${rel(zipPath)}.sha256`,
      binaries: bundles.map((bundle) => ({
        label: bundle.label,
        path: path.relative(packageRoot, path.join(packageRoot, bundle.fileName, 'Contents', 'x86_64-win', bundle.fileName)).split(path.sep).join('/'),
        sha256: sha256Hex(path.join(packageRoot, bundle.fileName, 'Contents', 'x86_64-win', bundle.fileName)),
      })),
    },
  };
}

export function localReleaseStateFor(opts, artifactManifest) {
  const expectedPurpose = artifactPurposeFor(opts.releaseKind);
  if (artifactManifest?.purpose !== expectedPurpose) {
    throw new Error(
      `artifact purpose does not match release kind: expected ${expectedPurpose}, got ${artifactManifest?.purpose}`,
    );
  }
  const artifactValidation = artifactManifest?.external_validation?.status;
  if (!['complete', 'pending'].includes(artifactValidation)) {
    throw new Error(`unsupported artifact validation state: ${artifactValidation}`);
  }
  if (artifactValidation !== opts.externalValidation) {
    throw new Error(
      `artifact validation does not match release state: expected ${opts.externalValidation}, got ${artifactValidation}`,
    );
  }
  return {
    schema: 'kirin-hypha-windows-release-state-v1',
    generatedAt: new Date().toISOString(),
    releaseKind: opts.releaseKind,
    releaseStatus: 'fallback_only_primary_installer_required',
    lsUpload: 'skip_primary_installer_required',
    artifact: artifactManifest,
  };
}

function runMain() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    console.log(usage());
    return;
  }

  const artifactDir = path.resolve(ROOT, opts.artifactDir);
  const outputDir = path.resolve(ROOT, opts.outputDir);
  const shortCommit = opts.commit.slice(0, 7);
  const packageBase = `Kirin-Hypha-${VERSION}-Windows-VST3-${opts.bNumber}-${shortCommit}`;
  const packageRoot = path.join(outputDir, packageBase);
  const zipPath = path.join(outputDir, `${packageBase}.zip`);
  const shaPath = `${zipPath}.sha256`;
  const sidecarPath = `${zipPath}.json`;

  const bundles = [
    {
      label: 'PRE VST3',
      fileName: 'Kirin Hypha PRE.vst3',
      displayName: 'PRE Kirin Hypha',
      dir: findBundle(artifactDir, 'Kirin Hypha PRE.vst3'),
    },
    {
      label: 'POST VST3',
      fileName: 'Kirin Hypha POST.vst3',
      displayName: 'POST Kirin Hypha',
      dir: findBundle(artifactDir, 'Kirin Hypha POST.vst3'),
    },
  ].map((bundle) => ({
    ...bundle,
    binary: path.join(bundle.dir, 'Contents', 'x86_64-win', bundle.fileName),
    moduleInfo: path.join(bundle.dir, 'Contents', 'Resources', 'moduleinfo.json'),
  }));

  for (const bundle of bundles) verifyBundle(bundle);

  fs.mkdirSync(outputDir, { recursive: true });
  fs.rmSync(packageRoot, { recursive: true, force: true });
  fs.rmSync(zipPath, { force: true });
  fs.rmSync(shaPath, { force: true });
  fs.rmSync(sidecarPath, { force: true });
  fs.mkdirSync(packageRoot, { recursive: true });

  for (const bundle of bundles) {
    fs.cpSync(bundle.dir, path.join(packageRoot, bundle.fileName), { recursive: true });
  }
  copyRequired(path.join(ROOT, 'docs/windows_external_validation.md'), path.join(packageRoot, 'WINDOWS_EXTERNAL_VALIDATION.md'));
  writeReadme(path.join(packageRoot, 'README_FIRST.txt'), VERSION, opts.payloadSigning);
  writeCommit(path.join(packageRoot, 'COMMIT.txt'), opts);
  writeShaSums(packageRoot);
  zipPackage(packageRoot, zipPath);
  validateZip(zipPath);

  const manifest = artifactManifestFor(opts, packageBase, zipPath, packageRoot, bundles);
  const releaseState = localReleaseStateFor(opts, manifest);
  fs.writeFileSync(shaPath, `${manifest.package.sha256}  ${path.basename(zipPath)}\n`);
  fs.writeFileSync(sidecarPath, `${JSON.stringify(manifest, null, 2)}\n`);

  if (opts.releaseKind === 'ls') {
    const stateName = `kirin_hypha_${VERSION}_windows_ls_${opts.bNumber.toLowerCase().replace(/[^a-z0-9]/g, '')}.state.json`;
    const statePath = path.join(ROOT, 'release_state', stateName);
    fs.mkdirSync(path.dirname(statePath), { recursive: true });
    fs.writeFileSync(statePath, `${JSON.stringify(releaseState, null, 2)}\n`);
    log(`wrote ${statePath}`);
  }

  log(`wrote ${zipPath}`);
  log(`sha256 ${manifest.package.sha256}`);
  log(`ls_upload ${releaseState.lsUpload}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === THIS_FILE) {
  try {
    runMain();
  } catch (error) {
    console.error(`[build-kirin-hypha-windows-vst3-zip] ERROR: ${error.message}`);
    process.exit(1);
  }
}
