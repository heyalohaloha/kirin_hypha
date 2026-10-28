#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';
import { loadMacAaxBundleManifest } from '../ls_release/kirin_hypha_aax_bundles.mjs';
import { readPrivateFile, requirePrivateFile } from './reaper_private_file.mjs';
import { inspectBundle, inspectInstalledBundle } from './aax_swap_preflight.mjs';
import { prepareSession, runPreparedSession } from './aax_swap_session.mjs';
import { buildAuthorization } from './aax_swap_command.mjs';
import { authorizeCommand } from './aax_swap_transport.mjs';

export function parseArgs(args) {
  if (args.length === 1 && ['--help', '-h'].includes(args[0])) return { help: true };
  if (args.length === 2 && args[0] === '--hash') return { hash: args[1] };
  const options = {};
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index];
    if (['--plan', '--run'].includes(argument)) {
      if (options.mode) throw new Error('choose one mode: --plan or --run');
      options.mode = argument.slice(2); options.config = args[++index];
    } else if (argument === '--receipt') {
      if (options.receipt) throw new Error('duplicate receipt');
      options.receipt = args[++index];
    } else throw new Error(`unknown argument: ${argument}`);
  }
  if (!options.mode || !options.config) throw new Error('--plan or --run requires a private config');
  if (!options.receipt) throw new Error('--receipt requires a new private receipt path');
  return options;
}

function openReceipt(file) {
  const absolute = path.resolve(file);
  const fd = fs.openSync(absolute, fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_EXCL | fs.constants.O_NOFOLLOW, 0o600);
  try {
    const checked = requirePrivateFile(absolute), opened = fs.fstatSync(fd);
    if (checked.ino !== opened.ino || checked.dev !== opened.dev || opened.nlink !== 1) throw new Error('receipt identity mismatch');
  } catch (error) { fs.closeSync(fd); throw error; }
  return {
    append(event) {
      const checked = requirePrivateFile(absolute), opened = fs.fstatSync(fd);
      if (checked.ino !== opened.ino || checked.dev !== opened.dev || opened.nlink !== 1) throw new Error('receipt changed');
      const bytes = Buffer.from(JSON.stringify({ at: new Date().toISOString(), ...event }) + '\n');
      let position = 0;
      while (position < bytes.length) {
        const count = fs.writeSync(fd, bytes, position, bytes.length - position);
        if (count <= 0) throw new Error('short receipt write');
        position += count;
      }
      fs.fsyncSync(fd);
    },
    close: () => fs.closeSync(fd),
  };
}

async function waitForHostClosure() {
  process.stdout.write('Candidate PRE/POST installed. Complete the acceptance work, quit the host, then press Enter to restore.\n');
  const input = readline.createInterface({ input: process.stdin, output: process.stdout });
  let interrupt;
  try {
    await new Promise((resolve, reject) => {
      let accepted = false;
      interrupt = () => reject(new Error('acceptance interrupted; restoring original pair'));
      process.once('SIGINT', interrupt); process.once('SIGTERM', interrupt);
      input.once('line', () => { accepted = true; resolve(); });
      input.once('close', () => { if (!accepted) reject(new Error('input closed; restoring original pair')); });
    });
  } finally {
    process.removeListener('SIGINT', interrupt); process.removeListener('SIGTERM', interrupt);
    input.close();
  }
}

export async function performSwap(options, dependencies = {}) {
  if (!options || !['plan', 'run'].includes(options.mode) || !options.config || !options.receipt) throw new Error('explicit mode, config and receipt required');
  const configPath = path.resolve(options.config);
  const config = JSON.parse(readPrivateFile(configPath));
  if (config?.schema !== 'hypha-aax-swap-v1' || Object.keys(config).sort().join(',') !== 'schema,sources'
      || !Array.isArray(config.sources) || config.sources.some(item => !item || Object.keys(item).sort().join(',') !== 'role,sha256,source')) {
    throw new Error('invalid private AAX source configuration');
  }
  const manifest = dependencies.manifest || loadMacAaxBundleManifest().bundles;
  const request = await prepareSession({ manifest, sources: config.sources,
    inspectInstalled: dependencies.inspectInstalled || (async ({ target }) => inspectInstalledBundle(target)) });
  const command = buildAuthorization(request, { uid: process.getuid(), parentPid: process.pid, token: crypto.randomBytes(16).toString('hex') });
  const receipt = openReceipt(options.receipt);
  try {
    receipt.append({ phase: 'prepared', sources: config.sources,
      identities: request.candidates.map(({ snapshot, ...identity }) => identity),
      envelopeSha256: command.config.envelopeSha256, scriptSha256: command.scriptSha256, commandSha256: command.commandSha256,
      recoveryDirectory: command.recoveryDirectory });
    if (options.mode === 'plan') {
      receipt.append({ phase: 'planned', authorizationRequests: 0 });
      return { status: 'planned', scriptSha256: command.scriptSha256 };
    }
    const authorize = dependencies.authorize || (async () => authorizeCommand(command, async status => receipt.append({ phase: 'worker', ...status })));
    const result = await runPreparedSession(request, { authorize, interact: dependencies.interact || waitForHostClosure });
    receipt.append({ phase: 'restored' });
    return result;
  } catch (error) {
    try { receipt.append({ phase: 'error', message: String(error?.message || error), recoveryDirectory: command.recoveryDirectory }); }
    catch { /* The worker retains its private journal when the receipt cannot be acknowledged. */ }
    throw error;
  } finally { receipt.close(); }
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) {
    console.log('Usage: node scripts/acceptance/aax_swap.mjs --plan|--run PRIVATE_CONFIG --receipt NEW_PRIVATE_JSONL\nHash only: node scripts/acceptance/aax_swap.mjs --hash BUNDLE\nQuit all AAX hosts before --run. The same administrator session restores both original bundles after acceptance.');
    return;
  }
  if (options.hash) {
    const result = inspectBundle(path.resolve(options.hash));
    console.log(JSON.stringify({ sha256: result.sha256, metadataSha256: result.metadataSha256 }, null, 2));
    return;
  }
  if (process.platform !== 'darwin') throw new Error('AAX acceptance swap requires macOS');
  const result = await performSwap(options);
  console.log(`AAX session: ${result.status}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => { console.error(`AAX swap: ${String(error?.message || error)}`); process.exitCode = 1; });
}
