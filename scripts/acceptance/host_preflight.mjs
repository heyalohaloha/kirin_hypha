#!/usr/bin/env node
// The two-minute check before macOS host acceptance: every installed bundle has exactly the
// candidate's bytes, no other copy can be loaded instead, no DAW still holds an old binary,
// and there is room for traces. Read-only; it never moves, deletes or quits anything.
import childProcess from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { loadMacShipBundleManifest } from '../ls_release/kirin_hypha_ship_bundles.mjs';
import { loadMacAaxBundleManifest } from '../ls_release/kirin_hypha_aax_bundles.mjs';

export const DAW_PROCESSES = ['REAPER', 'Logic Pro', 'Pro Tools', 'Studio One', 'Cubase', 'Nuendo', 'Live', 'Bitwig Studio', 'Digital Performer'];
const USER_PLUGIN_DIRS = ['Library/Audio/Plug-Ins/Components', 'Library/Audio/Plug-Ins/VST3'];

// Paths and bytes (and symlink targets) only: an installer may set its own owners and modes.
export function contentManifest(root) {
  const entries = [];
  const visit = (directory, relative) => {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
      const absolute = path.join(directory, entry.name), name = path.posix.join(relative, entry.name);
      if (entry.isSymbolicLink()) entries.push(`link ${name} ${fs.readlinkSync(absolute)}`);
      else if (entry.isDirectory()) visit(absolute, name);
      else if (entry.isFile()) entries.push(`file ${name} ${crypto.createHash('sha256').update(fs.readFileSync(absolute)).digest('hex')}`);
      else entries.push(`other ${name}`);
    }
  };
  visit(root, '');
  return entries;
}

export const isDaw = command => {
  const name = path.basename(command || '');
  return DAW_PROCESSES.some(daw => name === daw || (daw !== 'Live' && name.startsWith(`${daw} `)));
};

export function preflight({ bundles, installRoot = '/', home = os.homedir(), processes = [], freeBytes, minimumFreeBytes = 20e9 }) {
  const rows = [];
  for (const bundle of bundles) {
    const installed = path.join(installRoot, bundle.install_relative), name = path.basename(installed);
    if (!fs.statSync(bundle.sourcePath, { throwIfNoEntry: false })?.isDirectory()) {
      rows.push({ ok: false, check: `installed ${name}`, detail: `candidate build missing at ${bundle.sourcePath}` }); continue;
    }
    if (!fs.statSync(installed, { throwIfNoEntry: false })?.isDirectory()) {
      rows.push({ ok: false, check: `installed ${name}`, detail: `missing at ${installed}` }); continue;
    }
    const want = contentManifest(bundle.sourcePath), have = contentManifest(installed);
    const differ = want.length !== have.length || want.some((line, index) => line !== have[index]);
    rows.push({ ok: !differ, check: `installed ${name}`, detail: differ ? 'bytes differ from the candidate' : 'candidate bytes' });
  }
  // A user-level bundle with a shipped (or former) name can be loaded instead of the installed one.
  // Differently named helpers (validation delays, diagnostic builds) are separate plug-ins.
  const names = new Set(bundles.flatMap(bundle => [bundle.install_relative, ...(bundle.legacy_install_relative || [])])
    .map(relative => path.basename(relative).toLowerCase()));
  const shadows = USER_PLUGIN_DIRS.flatMap(dir => {
    const absolute = path.join(home, dir);
    return fs.existsSync(absolute) ? fs.readdirSync(absolute).filter(item => names.has(item.toLowerCase())).map(item => path.join(absolute, item)) : [];
  });
  rows.push({ ok: shadows.length === 0, check: 'no user-level copy', detail: shadows.length ? `move aside: ${shadows.join(', ')}` : 'none' });
  const running = [...new Set(processes.filter(isDaw).map(command => path.basename(command)))];
  rows.push({ ok: running.length === 0, check: 'no DAW running', detail: running.length ? `quit first: ${running.join(', ')}` : 'none' });
  rows.push({ ok: freeBytes >= minimumFreeBytes, check: 'free space for traces',
    detail: `${(freeBytes / 1e9).toFixed(1)} GB (need ${(minimumFreeBytes / 1e9).toFixed(0)} GB)` });
  return rows;
}

function main(argv) {
  const valueAfter = flag => { const index = argv.indexOf(flag); return index >= 0 ? argv[index + 1] : undefined; };
  const root = path.resolve(valueAfter('--root') || '.');
  const bundles = [...loadMacShipBundleManifest({ root }).bundles, ...loadMacAaxBundleManifest({ root }).bundles];
  const traceDir = process.env.KIRIN_HYPHA_CHAIN_TRACE_DIR || os.homedir();
  const stat = fs.statfsSync(fs.existsSync(traceDir) ? traceDir : os.homedir());
  const processes = childProcess.execFileSync('ps', ['-axo', 'comm='], { encoding: 'utf8' }).split('\n');
  const rows = preflight({ bundles, installRoot: valueAfter('--install-root') || '/', processes, freeBytes: stat.bavail * stat.bsize });
  for (const row of rows) console.log(`${row.ok ? 'PASS' : 'FAIL'} ${row.check}: ${row.detail}`);
  return rows.every(row => row.ok) ? 0 : 1;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) process.exitCode = main(process.argv.slice(2));
