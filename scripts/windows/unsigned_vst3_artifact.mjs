// The build record defines the entire upload tree. No parallel workflow path list.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { RECORD, verifyUnsignedWindowsHandoff } from './unsigned_vst3_provenance.mjs';
import { fileFact } from '../ls_release/hypha_release_contract.mjs';
import { qualifiedWindowsVst3Files } from './windows_vst3_bundle.mjs';
import { verifyLegalDelivery } from '../provenance/legal_delivery.mjs';

export function stageUnsignedWindowsArtifact(root, destination) {
  const record = JSON.parse(fs.readFileSync(path.join(root, RECORD), 'utf8'));
  const expected = Object.fromEntries(['commit', 'ciRun', 'factoryCommit', 'factoryRun'].map(k => [k, record[k]]));
  const checked = verifyUnsignedWindowsHandoff(root, expected);
  const files = new Map();
  const add = fact => {
    if (files.has(fact.path) && files.get(fact.path) !== fact.sha256) throw new Error('Conflicting artifact file facts');
    files.set(fact.path, fact.sha256);
  };
  add({ path: RECORD, sha256: fileFact(path.join(root, RECORD)).sha256 });
  for (const key of ['pluginval','rawSource','nativeLinkMap','nativePreLinkMap','cmakeCache','ffiArchive','sourceArchive']) add(record[key]);
  for (const binary of record.binaries) {
    const bundle = path.posix.dirname(path.posix.dirname(path.posix.dirname(binary.path)));
    for (const file of qualifiedWindowsVst3Files(path.join(root, bundle), binary.role)) add({ ...file, path: `${bundle}/${file.path}` });
  }
  const legal = verifyLegalDelivery(checked.legalDir, record.commit);
  for (const file of legal.files) add({ ...file, path: `${record.legalDir}/${file.path}` });
  add({ path: `${record.legalDir}/legal-delivery.json`, sha256: fileFact(path.join(checked.legalDir, 'legal-delivery.json')).sha256 });
  if (fs.existsSync(destination)) throw new Error('Artifact staging destination already exists; preserve previous bytes');
  fs.mkdirSync(destination, { recursive: true });
  for (const [relative, sha256] of files) {
    const source = path.join(root, relative), target = path.join(destination, relative);
    if (fileFact(source).sha256 !== sha256) throw new Error('Artifact source changed before staging');
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.copyFileSync(source, target, fs.constants.COPYFILE_EXCL);
  }
  verifyUnsignedWindowsHandoff(destination, expected);
  return [...files].map(([relative, sha256]) => ({ path: relative, sha256 })).sort((a,b) => a.path.localeCompare(b.path));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    if (process.argv.length !== 3) throw new Error('Usage: unsigned_vst3_artifact.mjs FRESH_UPLOAD_DIRECTORY');
    console.log(JSON.stringify(stageUnsignedWindowsArtifact(path.resolve(import.meta.dirname, '../..'), path.resolve(process.argv[2]))));
  } catch (error) { console.error(`[unsigned-artifact] ${error.message}`); process.exitCode = 1; }
}
