#!/usr/bin/env node
import childProcess from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(SCRIPT_DIR, '..', '..');
const DEFAULT_STATE = 'release_state/kirin_hypha_1.1.1_ls.state.json';

function usage() {
  return `Usage:
  node scripts/ls_release/kirin_hypha_ls_dry_run.mjs [options]

Options:
  --state <path>              State JSON path. Default: ${DEFAULT_STATE}
  --with-apple-verification   Run pkgutil/spctl/stapler verification on PKGs.
  --with-ls-chrome            Read logged-in Lemon Squeezy admin page from Google Chrome.
  --print-artifacts-json      Print current artifact values for manual state updates.
  --json                      Output machine-readable JSON.
  --help                      Show this help.
`;
}

function parseArgs(argv) {
  const opts = {
    state: DEFAULT_STATE,
    withAppleVerification: false,
    withLsChrome: false,
    printArtifactsJson: false,
    json: false,
    help: false,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--state') {
      const value = argv[i + 1];
      if (!value) throw new Error('--state requires a path');
      opts.state = value;
      i += 1;
    } else if (arg === '--with-apple-verification') {
      opts.withAppleVerification = true;
    } else if (arg === '--with-ls-chrome') {
      opts.withLsChrome = true;
    } else if (arg === '--print-artifacts-json') {
      opts.printArtifactsJson = true;
    } else if (arg === '--json') {
      opts.json = true;
    } else if (arg === '--help' || arg === '-h') {
      opts.help = true;
    } else {
      throw new Error(`unknown option: ${arg}`);
    }
  }
  return opts;
}

function resolveRoot(relativeOrAbsolute) {
  return path.isAbsolute(relativeOrAbsolute) ? relativeOrAbsolute : path.resolve(ROOT, relativeOrAbsolute);
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, 'utf8'));
}

function sha512Base64(filePath) {
  const hash = crypto.createHash('sha512');
  hash.update(fs.readFileSync(filePath));
  return hash.digest('base64');
}

function sha256Hex(filePath) {
  const hash = crypto.createHash('sha256');
  hash.update(fs.readFileSync(filePath));
  return hash.digest('hex');
}

function formatMiB(bytes) {
  return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
}

function run(command, args, options = {}) {
  const result = childProcess.spawnSync(command, args, {
    cwd: options.cwd || ROOT,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  return {
    command: `${command} ${args.map((arg) => (/\s/.test(arg) ? JSON.stringify(arg) : arg)).join(' ')}`,
    status: result.status,
    ok: result.status === 0,
    stdout: result.stdout || '',
    stderr: result.stderr || '',
  };
}

function runOsascript(script) {
  const result = childProcess.spawnSync('osascript', [], {
    input: script,
    encoding: 'utf8',
    stdio: ['pipe', 'pipe', 'pipe'],
  });
  return {
    ok: result.status === 0,
    status: result.status,
    stdout: result.stdout || '',
    stderr: result.stderr || '',
  };
}

function addCheck(checks, id, ok, details = {}) {
  checks.push({ id, ok: Boolean(ok), ...details });
}

function commandCheck(checks, id, commandResult) {
  addCheck(checks, id, commandResult.ok, {
    command: commandResult.command,
    output: `${commandResult.stdout}${commandResult.stderr}`.trim(),
  });
}

function collectArtifact(artifact) {
  const filePath = resolveRoot(artifact.path);
  const stat = fs.statSync(filePath);
  return {
    label: artifact.label,
    type: artifact.type,
    fileName: artifact.fileName,
    path: artifact.path,
    size: stat.size,
    sha512: sha512Base64(filePath),
    sha256: sha256Hex(filePath),
    lsDisplaySize: formatMiB(stat.size),
  };
}

function verifyLocal(state, checks) {
  const currentArtifacts = [];
  for (const artifact of state.artifacts) {
    const filePath = resolveRoot(artifact.path);
    addCheck(checks, `${artifact.label} artifact exists`, fs.existsSync(filePath), { path: artifact.path });
    if (!fs.existsSync(filePath)) continue;

    const current = collectArtifact(artifact);
    currentArtifacts.push(current);

    addCheck(checks, `${artifact.label} state byte size is populated`, Number.isFinite(artifact.size), {
      expected: 'number',
      actual: artifact.size,
    });
    if (Number.isFinite(artifact.size)) {
      addCheck(checks, `${artifact.label} byte size matches state`, current.size === artifact.size, {
        expected: artifact.size,
        actual: current.size,
      });
    }
    addCheck(checks, `${artifact.label} state sha512 is populated`, typeof artifact.sha512 === 'string' && artifact.sha512.length > 0, {
      expected: 'sha512',
      actual: artifact.sha512,
    });
    if (typeof artifact.sha512 === 'string' && artifact.sha512.length > 0) {
      addCheck(checks, `${artifact.label} sha512 matches state`, current.sha512 === artifact.sha512, {
        expected: artifact.sha512,
        actual: current.sha512,
      });
    }
    if (artifact.sha256) {
      addCheck(checks, `${artifact.label} sha256 matches state`, current.sha256 === artifact.sha256, {
        expected: artifact.sha256,
        actual: current.sha256,
      });
    }
    addCheck(checks, `${artifact.label} state Lemon Squeezy display size is populated`, typeof artifact.lsDisplaySize === 'string' && artifact.lsDisplaySize.length > 0, {
      expected: 'display size',
      actual: artifact.lsDisplaySize,
    });
    if (typeof artifact.lsDisplaySize === 'string' && artifact.lsDisplaySize.length > 0) {
      addCheck(checks, `${artifact.label} Lemon Squeezy display size matches state`, current.lsDisplaySize === artifact.lsDisplaySize, {
        expected: artifact.lsDisplaySize,
        actual: current.lsDisplaySize,
      });
    }
  }
  return currentArtifacts;
}

function verifyAppleArtifacts(state, checks) {
  for (const artifact of state.artifacts) {
    const filePath = resolveRoot(artifact.path);
    if (!fs.existsSync(filePath)) continue;
    if (artifact.type !== 'pkg') continue;

    const payload = run('pkgutil', ['--payload-files', filePath]);
    commandCheck(checks, `${artifact.label} payload readable`, payload);
    if (payload.ok) {
      const text = payload.stdout;
      for (const expected of state.expectedPayloads || []) {
        addCheck(checks, `${artifact.label} payload contains ${expected}`, text.includes(expected), { expected });
      }
    }
    commandCheck(checks, `${artifact.label} pkg signature`, run('pkgutil', ['--check-signature', filePath]));
    commandCheck(checks, `${artifact.label} Gatekeeper install assess`, run('spctl', ['-a', '-vv', '-t', 'install', filePath]));
    commandCheck(checks, `${artifact.label} stapler validate`, run('xcrun', ['stapler', 'validate', filePath]));
  }
}

async function verifyLsChrome(state, checks) {
  const ls = state.lemonSqueezy || {};
  if (!ls.productAdminUrl) {
    addCheck(checks, 'Lemon Squeezy product URL configured', false, {
      expected: 'state.lemonSqueezy.productAdminUrl',
      actual: null,
    });
    return;
  }

  const openScript = `
tell application "Google Chrome"
  if (count of windows) = 0 then make new window
  set URL of active tab of front window to "${ls.productAdminUrl}"
end tell
`;
  const openResult = runOsascript(openScript);
  addCheck(checks, 'Chrome opened Lemon Squeezy product URL', openResult.ok, {
    url: ls.productAdminUrl,
    output: `${openResult.stdout}${openResult.stderr}`.trim(),
  });
  if (!openResult.ok) return;

  await new Promise((resolve) => setTimeout(resolve, 5000));
  const readScript = `
tell application "Google Chrome"
  if (count of windows) = 0 then return "NO_CHROME_WINDOWS"
  set pageUrl to URL of active tab of front window
  set pageTitle to title of active tab of front window
  set pageText to execute active tab of front window javascript "document.body ? document.body.innerText : ''"
  return "URL=" & pageUrl & linefeed & "TITLE=" & pageTitle & linefeed & pageText
end tell
`;
  const readResult = runOsascript(readScript);
  addCheck(checks, 'Chrome Lemon Squeezy page text readable', readResult.ok, {
    output: readResult.ok ? '' : `${readResult.stdout}${readResult.stderr}`.trim(),
  });
  if (!readResult.ok) return;

  const text = readResult.stdout;
  addCheck(checks, 'Lemon Squeezy product name visible', text.includes(ls.productName), {
    expected: ls.productName,
  });
  if (ls.expectedStatus) {
    addCheck(checks, 'Lemon Squeezy product status visible', text.includes(ls.expectedStatus), {
      expected: ls.expectedStatus,
    });
  }
  if (Number.isFinite(ls.expectedFilesCount)) {
    addCheck(checks, 'Lemon Squeezy files count visible', text.includes(`Files (${ls.expectedFilesCount})`), {
      expected: `Files (${ls.expectedFilesCount})`,
    });
  }
  for (const artifact of state.artifacts) {
    addCheck(checks, `Lemon Squeezy has ${artifact.label} file name`, text.includes(artifact.fileName), {
      expected: artifact.fileName,
    });
    addCheck(checks, `Lemon Squeezy has ${artifact.label} display size`, text.includes(artifact.lsDisplaySize), {
      expected: artifact.lsDisplaySize,
    });
  }
}

function printHuman(result) {
  for (const check of result.checks) {
    const status = check.ok ? 'PASS' : 'FAIL';
    const detail = check.expected !== undefined || check.actual !== undefined
      ? ` expected=${JSON.stringify(check.expected)} actual=${JSON.stringify(check.actual)}`
      : check.path
        ? ` path=${check.path}`
        : check.command
          ? ` command=${check.command}`
          : '';
    console.log(`[${status}] ${check.id}${detail}`);
    if (!check.ok && check.output) console.log(check.output);
  }

  if (result.currentArtifacts.length > 0) {
    console.log('');
    console.log('Current artifacts:');
    for (const artifact of result.currentArtifacts) {
      console.log(`- ${artifact.label}: ${artifact.fileName}`);
      console.log(`  size: ${artifact.size}`);
      console.log(`  sha512: ${artifact.sha512}`);
      console.log(`  sha256: ${artifact.sha256}`);
      console.log(`  LS display: ${artifact.lsDisplaySize}`);
    }
  }

  console.log('');
  console.log(result.ok ? 'SUMMARY: PASS' : 'SUMMARY: FAIL');
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    console.log(usage());
    return;
  }

  const statePath = resolveRoot(opts.state);
  const state = readJson(statePath);
  const checks = [];
  addCheck(checks, 'state schema is supported', state.schema === 'kirin-release-ls-state-v1', {
    expected: 'kirin-release-ls-state-v1',
    actual: state.schema,
  });

  const currentArtifacts = verifyLocal(state, checks);
  if (opts.withAppleVerification) verifyAppleArtifacts(state, checks);
  if (opts.withLsChrome) await verifyLsChrome(state, checks);
  if (opts.printArtifactsJson) console.log(JSON.stringify(currentArtifacts, null, 2));

  const result = {
    ok: checks.every((check) => check.ok),
    statePath: path.relative(ROOT, statePath),
    currentArtifacts,
    checks,
  };

  if (opts.json) {
    console.log(JSON.stringify(result, null, 2));
  } else if (!opts.printArtifactsJson) {
    printHuman(result);
  }
  if (!result.ok) process.exitCode = 1;
}

main().catch((error) => {
  console.error(`[kirin_hypha_ls_dry_run] ERROR: ${error.message}`);
  process.exit(1);
});
