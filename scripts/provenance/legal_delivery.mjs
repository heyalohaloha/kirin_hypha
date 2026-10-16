// Legal documents live beside signed bundles; never modify a signed plugin to add them.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { sha256 } from './asset_gate.mjs';

const safe = relative => typeof relative === 'string' && relative.length > 0
  && !path.isAbsolute(relative) && !relative.includes('\\') && !relative.includes('\0')
  && !relative.includes(':')
  && !relative.split('/').some(p => !p || p === '.' || p === '..');
const MANIFEST = 'legal-delivery.json';

export function verifyLegalDelivery(directory, commit) {
  const root = path.resolve(directory);
  if (fs.lstatSync(root).isSymbolicLink()) throw new Error('Legal root must not be a symlink');
  if (fs.lstatSync(path.join(root, MANIFEST)).isSymbolicLink()) throw new Error('Legal manifest must not be a symlink');
  const manifest = JSON.parse(fs.readFileSync(path.join(root, MANIFEST), 'utf8'));
  if (manifest.schema !== 'hypha-legal-delivery-v1' || manifest.commit !== commit
      || !Array.isArray(manifest.files) || !manifest.files.length) {
    throw new Error('Legal delivery source identity/inventory mismatch');
  }
  const names = new Set();
  for (const entry of manifest.files) {
    if (!safe(entry.path) || names.has(entry.path) || !/^[a-f0-9]{64}$/.test(entry.sha256)) {
      throw new Error('Invalid legal file inventory');
    }
    names.add(entry.path);
    let file = root;
    for (const part of entry.path.split('/')) {
      file = path.join(file, part);
      if (fs.lstatSync(file).isSymbolicLink()) throw new Error('Legal file traverses symlink');
    }
    if (!fs.statSync(file).isFile() || sha256(fs.readFileSync(file)) !== entry.sha256) {
      throw new Error(`Legal file missing or changed: ${entry.path}`);
    }
  }
  for (const required of ['LICENSE', 'THIRD_PARTY_NOTICES.md',
    'THIRD_PARTY_LICENSES/MoSQITo-1.2.1-Apache-2.0.txt', 'source-delivery.json', 'component-notices.json']) {
    if (!names.has(required)) throw new Error(`Required legal file absent: ${required}`);
  }
  const delivery = JSON.parse(fs.readFileSync(path.join(root, 'source-delivery.json'), 'utf8'));
  if (delivery.commit !== commit || !/^[a-f0-9]{64}$/.test(delivery.sha256)
      || delivery.sha256 !== manifest.sourceSha256 || delivery.url !== manifest.sourceDownload
      || !/^https:\/\/github\.com\/heyalohaloha\/kirin_hypha\/releases\/download\/v\d+\.\d+\.\d+\/[^/]+\.zip$/.test(delivery.url)) {
    throw new Error('Corresponding Source pointer mismatch');
  }
  const components = JSON.parse(fs.readFileSync(path.join(root, 'component-notices.json'), 'utf8'));
  if (components.schema !== 'hypha-delivered-components-v1' || components.commit !== commit
      || components.sourceSha256 !== delivery.sha256 || !Array.isArray(components.components)
      || !components.components.length) throw new Error('Delivered component notice identity mismatch');
  return manifest;
}

export function stageLegalDelivery(directory, destination, commit) {
  const manifest = verifyLegalDelivery(directory, commit);
  const target = path.resolve(destination);
  fs.mkdirSync(target, { recursive: true });
  if (fs.lstatSync(target).isSymbolicLink()) throw new Error('Legal destination must not be a symlink');
  for (const entry of manifest.files) {
    const file = path.join(target, entry.path);
    let parent = target;
    for (const part of entry.path.split('/').slice(0, -1)) {
      parent = path.join(parent, part);
      fs.mkdirSync(parent, { recursive: true });
      if (fs.lstatSync(parent).isSymbolicLink()) throw new Error('Legal destination traverses symlink');
    }
    if (fs.existsSync(file)) {
      if (fs.lstatSync(file).isSymbolicLink() || sha256(fs.readFileSync(file)) !== entry.sha256) {
        throw new Error('Refusing to overwrite different existing legal bytes');
      }
    } else fs.copyFileSync(path.join(directory, entry.path), file, fs.constants.COPYFILE_EXCL);
  }
  const record = path.join(target, MANIFEST);
  const bytes = fs.readFileSync(path.join(directory, MANIFEST));
  if (fs.existsSync(record) && (fs.lstatSync(record).isSymbolicLink() || !fs.readFileSync(record).equals(bytes))) {
    throw new Error('Refusing to overwrite existing legal manifest');
  }
  if (!fs.existsSync(record)) fs.writeFileSync(record, bytes, { flag: 'wx' });
  return verifyLegalDelivery(target, commit);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, directory, destination, commit] = process.argv.slice(2);
    if (mode !== 'stage' || !directory || !destination || !/^[a-f0-9]{40}$/.test(commit || '')) {
      throw new Error('Usage: legal_delivery.mjs stage LEGAL_DIR DESTINATION EXACT_COMMIT');
    }
    console.log(JSON.stringify(stageLegalDelivery(directory, destination, commit)));
  } catch (error) { console.error(`[legal-delivery] ${error.message}`); process.exitCode = 1; }
}
