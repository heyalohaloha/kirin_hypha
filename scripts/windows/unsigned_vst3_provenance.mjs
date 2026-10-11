import { rawSourceSnapshot, assertRawSourceUnchanged, assertPluginvalQualification, inspectNativeLinkMap } from '../provenance/unsigned_qualification.mjs';
// Credential-free private Windows producer handoff. This does not approve distribution.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { fileFact, atomicJson } from '../ls_release/hypha_release_contract.mjs';
import { requireCleanReleaseSource } from '../ls_release/release_source_identity.mjs';
import { prepareSourceDelivery } from '../provenance/source_delivery.mjs';
import { verifyLegalDelivery } from '../provenance/legal_delivery.mjs';
import { inspectUpdateBinary } from '../updates/update_key_binding.mjs';
import { readPeMachine } from './windows-aax-bundles.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const RECORD = 'dist/WINDOWS_UNSIGNED/build.json';
function regular(root, relative) {
  if (typeof relative !== 'string' || !relative || /[\\:\x00-\x1f]/.test(relative)
      || relative.startsWith('/') || relative.split('/').some(p => !p || p === '.' || p === '..')) {
    throw new Error('Unsafe unsigned handoff path');
  }
  let file = path.resolve(root);
  for (const part of relative.split('/')) {
    file = path.join(file, part);
    if (fs.lstatSync(file).isSymbolicLink()) throw new Error('Unsigned handoff must not traverse symlinks');
  }
  if (!fs.statSync(file).isFile()) throw new Error('Unsigned handoff file is not regular');
  return file;
}
function same(root, fact) {
  const file = regular(root, fact.path);
  if (fileFact(file).sha256 !== fact.sha256) throw new Error('Unsigned handoff bytes changed');
  return file;
}

function bundleFiles(directory, binary) {
  const full = regular(directory, binary.path);
  const bundle = path.resolve(path.dirname(full), '../..');
  const entries = [];
  const walk = (folder, prefix = '') => {
    for (const entry of fs.readdirSync(folder, { withFileTypes: true })) {
      const relative = prefix + entry.name;
      if (entry.isSymbolicLink()) throw new Error('Qualified bundle contains symlink');
      if (entry.isDirectory()) walk(path.join(folder, entry.name), relative + '/');
      else if (entry.isFile()) entries.push({ path: relative, sha256: fileFact(path.join(folder, entry.name)).sha256 });
    }
  };
  walk(bundle); return entries;
}

export function verifyUnsignedWindowsHandoff(directory, expected) {
  const record = JSON.parse(fs.readFileSync(regular(directory, RECORD), 'utf8'));
  if (record.schema !== 'hypha-private-windows-vst3-build-v1' || record.commit !== expected.commit
      || String(record.ciRun) !== String(expected.ciRun) || record.factoryCommit !== expected.factoryCommit
      || String(record.factoryRun) !== String(expected.factoryRun) || record.platform !== 'windows-x64'
      || !/^B-\d+$/.test(record.bNumber) || !/^\d+\.\d+\.\d+$/.test(record.version)
      || record.updatePublicKey !== '' || record.kimeraEmbedded !== false) {
    throw new Error('Unsigned Windows source/CI/factory identity mismatch');
  }
  if (!Array.isArray(record.binaries) || record.binaries.length !== 2
      || new Set(record.binaries.map(b => b.role)).size !== 2
      || record.binaries.some(b => !['PRE', 'POST'].includes(b.role))) {
    throw new Error('Unique PRE/POST unsigned binaries required');
  }
  for (const binary of record.binaries) {
    const file = same(directory, binary);
    if (readPeMachine(file) !== 0x8664) throw new Error('Unsigned Windows binary must be x64');
    inspectUpdateBinary(file, '');
    const info = JSON.parse(fs.readFileSync(same(directory, binary.moduleInfo), 'utf8'));
    if (info.Version !== record.version) throw new Error('Unsigned module version mismatch');
  }
  const qualification = JSON.parse(fs.readFileSync(same(directory, record.pluginval), 'utf8').replace(/^\uFEFF/, ''));
  assertPluginvalQualification(qualification, record.binaries, binary => bundleFiles(directory, binary));
  same(directory, record.rawSource);
  inspectNativeLinkMap(fs.readFileSync(same(directory, record.nativeLinkMap), 'utf8'));
  const cache = fs.readFileSync(same(directory, record.cmakeCache), 'utf8');
  if (!/^KIRIN_HYPHA_KIMERA_FONT_FILE:FILEPATH=\s*$/m.test(cache)) throw new Error('Unsigned producer must prove an empty font input');
  same(directory, record.ffiArchive);
  same(directory, record.sourceArchive);
  const legalManifest = regular(directory, `${record.legalDir}/legal-delivery.json`);
  const legal = verifyLegalDelivery(path.dirname(legalManifest), record.commit);
  if (legal.sourceSha256 !== record.sourceArchive.sha256) throw new Error('Unsigned legal/source archive mismatch');
  return { record, legalDir: path.dirname(legalManifest), artifactDir: directory };
}

export function writeUnsignedWindowsHandoff(ciRun) {
  if (process.platform !== 'win32' || process.arch !== 'x64' || process.env.RUNNER_OS !== 'Windows'
      || !process.env.KIRIN_HYPHA_PRODUCER_REPOSITORY
      || process.env.GITHUB_REPOSITORY !== process.env.KIRIN_HYPHA_PRODUCER_REPOSITORY) {
    throw new Error('Formal unsigned producer requires the private Windows x64 workflow');
  }
  const { commit, bNumber } = requireCleanReleaseSource({ root: ROOT });
  const factoryCommit = process.env.GITHUB_SHA, factoryRun = process.env.GITHUB_RUN_ID;
  if (!/^\d+$/.test(ciRun || '') || !/^[a-f0-9]{40}$/.test(factoryCommit || '') || !/^\d+$/.test(factoryRun || '')) {
    throw new Error('Exact factory and source CI provenance required');
  }
  const version = fs.readFileSync(path.join(ROOT, 'crates/hypha_pre/Cargo.toml'), 'utf8')
    .match(/^version\s*=\s*"(\d+\.\d+\.\d+)"/m)?.[1];
  assertRawSourceUnchanged(JSON.parse(fs.readFileSync(path.join(ROOT, 'dist/WINDOWS_UNSIGNED/raw-source.json'), 'utf8')), rawSourceSnapshot(ROOT));
  const source = prepareSourceDelivery({ root: ROOT, commit, version,
    reportPath: path.join(ROOT, 'release_state/unsigned-vst3/source-report.json'),
    directory: path.join(ROOT, 'release_state/unsigned-vst3/source-delivery') });
  const relativeFact = file => ({ path: path.relative(ROOT, file).split(path.sep).join('/'), sha256: fileFact(file).sha256 });
  const record = { schema: 'hypha-private-windows-vst3-build-v1', commit, bNumber, version,
    ciRun, factoryCommit, factoryRun, platform: 'windows-x64', updatePublicKey: '', kimeraEmbedded: false,
    pluginval: relativeFact(path.join(ROOT, 'dist/WINDOWS_UNSIGNED/pluginval.json')),
    rawSource: relativeFact(path.join(ROOT, 'dist/WINDOWS_UNSIGNED/raw-source.json')),
    nativeLinkMap: relativeFact(path.join(ROOT, 'juce_shell/build-windows/KirinHyphaPOST.map')),
    cmakeCache: relativeFact(path.join(ROOT, 'juce_shell/build-windows/CMakeCache.txt')),
    ffiArchive: relativeFact(path.join(ROOT, 'target/release/kirin_hypha_ffi.lib')),
    sourceArchive: relativeFact(source.sourceArchive), legalDir: path.relative(ROOT, source.legalDir).split(path.sep).join('/'),
    binaries: ['PRE', 'POST'].map(role => {
      const bundle = path.join(ROOT, `juce_shell/build-windows/KirinHypha${role}_artefacts/Release/VST3/Kirin Hypha ${role}.vst3`);
      return { role, ...relativeFact(path.join(bundle, `Contents/x86_64-win/Kirin Hypha ${role}.vst3`)),
        moduleInfo: relativeFact(path.join(bundle, 'Contents/Resources/moduleinfo.json')) };
    }) };
  atomicJson(path.join(ROOT, RECORD), record);
  verifyUnsignedWindowsHandoff(ROOT, { commit, ciRun, factoryCommit, factoryRun });
  return record;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, directory, commit, ciRun, factoryCommit, factoryRun] = process.argv.slice(2);
    if (mode === 'write') console.log(JSON.stringify(writeUnsignedWindowsHandoff(directory)));
    else if (mode === 'verify') console.log(JSON.stringify(verifyUnsignedWindowsHandoff(path.resolve(directory),
      { commit, ciRun, factoryCommit, factoryRun })));
    else throw new Error('Usage: unsigned_vst3_provenance.mjs write CI_RUN | verify DIR COMMIT CI_RUN FACTORY_COMMIT FACTORY_RUN');
  } catch (error) { console.error(`[unsigned-vst3] ${error.message}`); process.exitCode = 1; }
}
