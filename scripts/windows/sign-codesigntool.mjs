#!/usr/bin/env node
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const THIS_FILE = fileURLToPath(import.meta.url);
const ROOT = path.resolve(path.dirname(THIS_FILE), '..', '..');
export const TOTP_WINDOW_MS = 30_000;
const TOTP_BUFFER_MS = 1_000;
const inProcessWindow = { last: null };

export function parseArgs(argv) {
  const opts = { inputFile: '', batchInputDir: '', batchOutputDir: '', help: false };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === '--help' || arg === '-h') opts.help = true;
    else if (arg === '--input-file') opts.inputFile = argv[++index] || '';
    else if (arg === '--batch-input-dir') opts.batchInputDir = argv[++index] || '';
    else if (arg === '--batch-output-dir') opts.batchOutputDir = argv[++index] || '';
    else throw new Error(`unknown option: ${arg}`);
  }
  return opts;
}

function requiredEnv(env, name) {
  const value = env[name];
  if (typeof value !== 'string' || value.trim() === '') {
    throw new Error(`missing required environment variable: ${name}`);
  }
  return value.trim();
}

export function signingEnvironment(env = process.env) {
  return {
    username: requiredEnv(env, 'ESIGNER_USERNAME'),
    password: requiredEnv(env, 'ESIGNER_PASSWORD'),
    credentialId: requiredEnv(env, 'ESIGNER_CREDENTIAL_ID'),
    totpSecret: requiredEnv(env, 'ESIGNER_TOTP_SECRET'),
    toolPath: requiredEnv(env, 'CODE_SIGN_TOOL_PATH'),
  };
}

export function resolveInvocation(env = process.env) {
  const { toolPath } = signingEnvironment(env);
  const launcher = path.join(toolPath, process.platform === 'win32' ? 'CodeSignTool.bat' : 'CodeSignTool.sh');
  if (!fs.statSync(launcher, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`CodeSignTool launcher not found: ${launcher}`);
  }
  return { command: launcher, cwd: toolPath };
}

export function delayForFreshWindow(nowMs, previousWindow) {
  const current = Math.floor(nowMs / TOTP_WINDOW_MS);
  if (current !== previousWindow) return 0;
  return TOTP_WINDOW_MS - (nowMs % TOTP_WINDOW_MS) + TOTP_BUFFER_MS;
}

export function readWindowState(stateFile) {
  if (!stateFile || !fs.existsSync(stateFile)) return null;
  const raw = fs.readFileSync(stateFile, 'utf8').trim();
  if (!/^\d+$/.test(raw)) throw new Error(`invalid eSigner TOTP window state: ${stateFile}`);
  return Number.parseInt(raw, 10);
}

export function writeWindowState(stateFile, value) {
  if (!stateFile) return;
  if (!Number.isSafeInteger(value) || value < 0) throw new Error(`invalid TOTP window: ${value}`);
  fs.mkdirSync(path.dirname(stateFile), { recursive: true });
  fs.writeFileSync(stateFile, `${value}\n`, { mode: 0o600 });
}

export async function waitForFreshWindow(options = {}) {
  const now = options.now || (() => Date.now());
  const sleep = options.sleep || ((ms) => new Promise((resolve) => setTimeout(resolve, ms)));
  const state = options.state || inProcessWindow;
  const stateFile = options.stateFile || process.env.KIRIN_ESIGNER_TOTP_STATE_FILE;
  const previous = stateFile ? readWindowState(stateFile) : state.last;
  const delay = delayForFreshWindow(now(), previous);
  if (delay > 0) {
    options.logger?.log?.(`[hypha-sign] waiting ${delay}ms for a fresh eSigner OTP window`);
    await sleep(delay);
  }
  state.last = Math.floor(now() / TOTP_WINDOW_MS);
  writeWindowState(stateFile, state.last);
}

function spawnOptions(command, options = {}) {
  return {
    cwd: options.cwd,
    encoding: 'utf8',
    stdio: options.stdio || 'pipe',
    shell: process.platform === 'win32' && /\.(cmd|bat)$/i.test(command),
  };
}

function runTool(command, args, cwd, options = {}) {
  const spawn = options.spawnSync || spawnSync;
  const result = spawn(command, args, spawnOptions(command, { cwd, stdio: options.stdio }));
  if (result.error) throw result.error;
  const output = `${result.stdout || ''}\n${result.stderr || ''}`;
  if (result.status !== 0 || /(^|\n)Error:/i.test(output)) {
    const detail = output.split(/\r?\n/).find((line) => /^Error:/i.test(line.trim()));
    throw new Error(detail?.trim() || `CodeSignTool exited with status ${result.status}`);
  }
}

function scan(filePath, context, options) {
  options.logger?.log(`[hypha-sign] malware scan: ${path.basename(filePath)}`);
  runTool(context.command, [
    'scan_code',
    `-credential_id=${context.creds.credentialId}`,
    `-username=${context.creds.username}`,
    `-input_file_path=${filePath}`,
    `-password=${context.creds.password}`,
  ], context.cwd, options);
}

function contextFrom(options) {
  const env = options.env || process.env;
  const creds = signingEnvironment(env);
  const { command, cwd } = resolveInvocation(env);
  return { env, creds, command, cwd };
}

export async function signFile(inputFile, options = {}) {
  const full = path.resolve(ROOT, inputFile);
  if (!fs.statSync(full, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`signing input not found: ${full}`);
  }
  const logger = options.logger === false ? null : (options.logger || console);
  const context = contextFrom(options);
  scan(full, context, { ...options, logger });
  await waitForFreshWindow({
    ...options,
    logger,
    stateFile: context.env.KIRIN_ESIGNER_TOTP_STATE_FILE,
  });
  logger?.log(`[hypha-sign] signing: ${path.basename(full)}`);
  runTool(context.command, [
    'sign',
    `-credential_id=${context.creds.credentialId}`,
    `-username=${context.creds.username}`,
    `-totp_secret=${context.creds.totpSecret}`,
    `-input_file_path=${full}`,
    `-output_dir_path=${path.dirname(full)}`,
    '-override=true',
    `-password=${context.creds.password}`,
  ], context.cwd, { ...options, logger, stdio: options.stdio || 'inherit' });
  return full;
}

export async function batchSign(inputDir, outputDir, options = {}) {
  const input = path.resolve(ROOT, inputDir);
  const output = path.resolve(ROOT, outputDir);
  if (!fs.statSync(input, { throwIfNoEntry: false })?.isDirectory()) {
    throw new Error(`batch signing input directory not found: ${input}`);
  }
  const files = fs.readdirSync(input, { withFileTypes: true })
    .filter((entry) => entry.isFile())
    .map((entry) => path.join(input, entry.name))
    .sort();
  if (files.length === 0) throw new Error(`batch signing input is empty: ${input}`);
  fs.mkdirSync(output, { recursive: true });
  const logger = options.logger === false ? null : (options.logger || console);
  const context = contextFrom(options);
  for (const file of files) scan(file, context, { ...options, logger });
  await waitForFreshWindow({
    ...options,
    logger,
    stateFile: context.env.KIRIN_ESIGNER_TOTP_STATE_FILE,
  });
  logger?.log(`[hypha-sign] batch signing ${files.length} payload binaries`);
  runTool(context.command, [
    'batch_sign',
    `-credential_id=${context.creds.credentialId}`,
    `-username=${context.creds.username}`,
    `-totp_secret=${context.creds.totpSecret}`,
    `-input_dir_path=${input}`,
    `-output_dir_path=${output}`,
    `-password=${context.creds.password}`,
  ], context.cwd, { ...options, logger });
  return files.map((file) => path.join(output, path.basename(file)));
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    console.log('Use --input-file <file> or --batch-input-dir <dir> --batch-output-dir <dir>.');
    return;
  }
  if (opts.inputFile) {
    if (opts.batchInputDir || opts.batchOutputDir) throw new Error('single and batch modes are exclusive');
    await signFile(opts.inputFile);
    return;
  }
  if (!opts.batchInputDir || !opts.batchOutputDir) {
    throw new Error('--input-file or both batch directory options are required');
  }
  await batchSign(opts.batchInputDir, opts.batchOutputDir);
}

if (process.argv[1] && path.resolve(process.argv[1]) === THIS_FILE) {
  main().catch((error) => {
    console.error(`[hypha-sign] ERROR: ${error.message}`);
    process.exit(1);
  });
}
