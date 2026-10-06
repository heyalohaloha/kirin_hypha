#!/usr/bin/env node
// Public build input and exact binary evidence; no credentials or network.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import childProcess from 'node:child_process';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { universalSlices } from './update_binary_slices.mjs';

const MODULE = fileURLToPath(import.meta.url);
const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');
export const BINARY_MARKER = 'KirinHyphaUpdateKeySha256=';

export function validatePublicKey(value = '') {
  if (typeof value !== 'string' || (value !== '' && !/^10001,[89a-f][0-9a-f]{510}[13579bdf]$/.test(value))) {
    throw new Error('Update public key requires canonical RSA-2048/65537 with a 2048-bit odd modulus, or explicit empty');
  }
  return value;
}

export function updateBinding(key = '') {
  validatePublicKey(key);
  return { protocol: 1, publicKeySha256: key === '' ? '' : sha256(key) };
}

export function assertUpdateBinding(actual, key = '') {
  const expected = updateBinding(key);
  if (actual?.protocol !== 1 || actual.publicKeySha256 !== expected.publicKeySha256) {
    throw new Error('Update public-key provenance mismatch or missing');
  }
  return expected;
}

export function inspectUpdateBinary(file, key = '', { universal = false } = {}) {
  const binding = updateBinding(key), bytes = fs.readFileSync(file);
  const slices = universalSlices(bytes, universal);
  const expected = binding.publicKeySha256 || 'disabled';
  for (const slice of slices || [{ bytes }]) {
    const markers = [...slice.bytes.toString('latin1').matchAll(/KirinHyphaUpdateKeySha256=([0-9a-f]{64}|disabled);/g)].map(m => m[1]);
    if (!markers.length || markers.some(value => value !== expected)) {
      throw new Error('Distributed binary architecture update public-key marker missing or mismatched');
    }
  }
  return { binarySha256: sha256(bytes), publicKeySha256: binding.publicKeySha256,
    ...(slices ? { architectures: slices.map(s => ({ architecture: s.architecture, sha256: sha256(s.bytes) })) } : {}) };
}

export function verifyUpdatePlist(value, key = '', au = false) {
  const binding = updateBinding(key);
  if (value.KirinHyphaUpdateKeySha256 !== binding.publicKeySha256) {
    throw new Error('Bundle update public-key digest does not match approved build input');
  }
  if (au) {
    const usage = value.AudioComponents?.[0]?.resourceUsage ?? {};
    if (usage['temporary-exception.files.all.read-write'] !== true) throw new Error('AU resourceUsage missing files.all');
    if (key !== '') {
      if (usage['network.client'] !== true || value.KirinHyphaUpdateProtocol !== 1) {
        throw new Error('AU network permission requires the bound opt-in update key and protocol');
      }
    } else if ('network.client' in usage || Object.hasOwn(value, 'KirinHyphaUpdateProtocol')) {
      throw new Error('Disabled update build must not retain AU network permission/protocol');
    }
  }
  return binding;
}

export function inspectMacBundle(bundle, key = '', run = childProcess.execFileSync) {
  const plist = JSON.parse(run('plutil', ['-convert', 'json', '-o', '-', path.join(bundle, 'Contents/Info.plist')],
    { encoding: 'utf8' }));
  verifyUpdatePlist(plist, key, bundle.endsWith('.component'));
  const binary = path.join(bundle, 'Contents/MacOS', plist.CFBundleExecutable || '');
  return { role: /\b(PRE|POST)\b/.exec(path.basename(bundle))?.[1],
    format: bundle.endsWith('.component') ? 'AU' : bundle.endsWith('.vst3') ? 'VST3' : 'AAX',
    ...inspectUpdateBinary(binary, key, { universal: true }) };
}

export function inspectMacTree(root, key = '', count = 4, run) {
  const bundles = [];
  const visit = directory => {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      if (!entry.isDirectory()) continue;
      const full = path.join(directory, entry.name);
      if (/\.(component|vst3|aaxplugin)$/.test(entry.name)) bundles.push(inspectMacBundle(full, key, run));
      else visit(full);
    }
  };
  visit(root);
  bundles.sort((a, b) => `${a.format}/${a.role}`.localeCompare(`${b.format}/${b.role}`));
  const identities = bundles.map(b => `${b.format}/${b.role}`);
  const expected = ({ 2: ['AAX'], 4: ['AU', 'VST3'], 6: ['AAX', 'AU', 'VST3'] }[count] || [])
    .flatMap(format => ['PRE', 'POST'].map(role => `${format}/${role}`)).sort();
  if (bundles.length !== count || new Set(identities).size !== count
      || JSON.stringify([...identities].sort()) !== JSON.stringify(expected)) throw new Error('Update key evidence has a missing or duplicate role/format');
  return { ...updateBinding(key), binaries: bundles };
}

export function assertPackageUpdateBinding(value, key, identities, { universal = false } = {}) {
  assertUpdateBinding(value, key);
  const records = value.binaries;
  if (!Array.isArray(records) || records.length !== identities.length) throw new Error('Exact distributed update binary provenance missing');
  for (const identity of identities) {
    const matching = records.filter(b => `${b.format}/${b.role}` === identity);
    if (matching.length !== 1 || !/^[0-9a-f]{64}$/.test(matching[0].binarySha256 || '')
        || matching[0].publicKeySha256 !== value.publicKeySha256) throw new Error('Distributed role/format update-key provenance mismatch');
    if (universal) {
      const arches = matching[0].architectures;
      if (!Array.isArray(arches) || arches.length !== 2 || ['arm64', 'x86_64'].some(architecture =>
        arches.filter(a => a.architecture === architecture && /^[0-9a-f]{64}$/.test(a.sha256 || '')).length !== 1)) {
        throw new Error('Every Universal architecture requires retained update key/hash evidence');
      }
    }
  }
  return value;
}

export function inspectMacZip(file, key = '', count = 4) {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-update-zip-evidence-'));
  try {
    childProcess.execFileSync('ditto', ['-x', '-k', file, temporary]);
    return inspectMacTree(temporary, key, count);
  } finally { fs.rmSync(temporary, { recursive: true, force: true }); }
}

if (process.argv[1] && path.resolve(process.argv[1]) === MODULE) {
  try {
    const key = process.env.KIRIN_HYPHA_UPDATE_PUBLIC_KEY || '';
    const [mode, ...args] = process.argv.slice(2);
    if (mode === 'key') console.log(JSON.stringify(updateBinding(key)));
    else if (mode === 'mac-tree') console.log(JSON.stringify(inspectMacTree(args[0], key, Number(args[1]))));
    else if (mode === 'mac-zip') console.log(JSON.stringify(inspectMacZip(args[0], key, Number(args[1]))));
    else if (mode === 'mac-bundles') console.log(JSON.stringify({ ...updateBinding(key),
      binaries: args.map(bundle => inspectMacBundle(bundle, key)) }));
    else throw new Error('Expected key, mac-tree ROOT COUNT or mac-bundles BUNDLE...');
  } catch (error) { console.error(`[hypha-update-key] ${error.message}`); process.exitCode = 1; }
}
