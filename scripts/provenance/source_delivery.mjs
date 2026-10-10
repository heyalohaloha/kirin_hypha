// Prepare retained source/legal bytes. This is not linkage, extraction or release acceptance.
import fs from 'node:fs';
import path from 'node:path';
import childProcess from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { assetDecision, embeddedAssets, sha256, readAssetRegistry, rejectHeldBytes } from './asset_gate.mjs';
import { sourceRequirements, validateComponentInventory } from './distribution_gate.mjs';
import { readSourceZip } from './source_zip.mjs';
import { writeSourceZip } from './source_zip_writer.mjs';
import { verifyLegalDelivery } from './legal_delivery.mjs';
import { requireCleanReleaseSource } from '../ls_release/release_source_identity.mjs';
import { atomicJson, fileFact } from '../ls_release/hypha_release_contract.mjs';

const REQUIRED = ['LICENSE', 'THIRD_PARTY_NOTICES.md',
  'THIRD_PARTY_LICENSES/MoSQITo-1.2.1-Apache-2.0.txt'];
const licenseName = relative => /^(?:licen[cs]e|copying|copyright|notice)(?:[-_.]|$)/i.test(path.posix.basename(relative))
  || /(?:^|\/)licen[cs]es\//i.test(relative)
  || /^THIRD_PARTY_LICENSES\/cargo\/r-efi-[^/]+\/AUTHORS$/.test(relative)
  || /^THIRD_PARTY_LICENSES\/cargo\/realfft-[^/]+\/README.md$/.test(relative);

export function distributionComponents(root, inventory) {
  const metadata = JSON.parse(childProcess.execFileSync('cargo',
    ['metadata', '--offline', '--locked', '--format-version', '1'],
    { cwd: root, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 }));
  const components = [
    { id: 'MoSQITo@1.2.1', license: 'Apache-2.0',
      source: 'https://github.com/Eomys/MoSQITo/tree/a14daeafcf0a37f36e4a29314ea42bc68b304c0e',
      modificationNotice: 'Rust adaptations and subsequent streaming changes, as described in THIRD_PARTY_NOTICES.md.',
      licenseFiles: [REQUIRED[2]] },
    { id: 'JUCE@7.0.12', license: 'GPL-3.0',
      source: 'https://github.com/juce-framework/JUCE/tree/4f43011b96eb0636104cb3e433894cda98243626',
      modificationNotice: 'Pinned JUCE source with the retained scripts/apply_juce_patches.sh patch stack.',
      licenseFiles: ['juce_shell/JUCE/LICENSE.md', 'LICENSE'] },
  ];
  for (const expected of inventory.packages) {
    const pkg = metadata.packages.find(p => `${p.name}@${p.version}` === expected.id);
    if (!pkg) throw new Error(`Missing source package: ${expected.id}`);
    const owned = ['kirin_measure', 'kirin_hypha_ffi'].includes(pkg.name);
    const sourceRoot = path.dirname(pkg.manifest_path);
    const prefix = `dependencies/${pkg.name}-${pkg.version}/`;
    const retained = `THIRD_PARTY_LICENSES/cargo/${pkg.name}-${pkg.version}/`;
    const licenses = owned ? ['LICENSE'] : inventory.files.filter(f =>
      ((f.archive.startsWith(prefix) || f.archive.startsWith(retained)) && licenseName(f.archive))
      || f.archive === `${retained}MIT-standard-terms.txt`
      || (f.file.startsWith(`${sourceRoot}${path.sep}`) && licenseName(f.archive)))
      .map(f => f.archive);
    if (!licenses.length) throw new Error(`Retained upstream license text required: ${expected.id}`);
    const vcsFile = path.join(sourceRoot, '.cargo_vcs_info.json');
    const vcs = fs.existsSync(vcsFile) ? JSON.parse(fs.readFileSync(vcsFile, 'utf8')).git?.sha1 : '';
    components.push({ id: expected.id, license: expected.license || (owned ? 'GPL-3.0' : ''),
      source: owned ? `Owned source: ${path.relative(root, sourceRoot).split(path.sep).join('/')}`
        : `${pkg.repository || pkg.source || pkg.homepage || pkg.name}${vcs ? ` (package VCS ${vcs})` : ''}`,
      modificationNotice: pkg.name === 'ebur128'
        ? 'Vendored Rust implementation with retained local streaming/cache changes.'
        : owned ? 'Original project source; retained history and modifications included.'
          : 'Conservative normal/build dependency source; upstream package bytes retained.',
      licenseFiles: [...new Set(licenses)].sort() });
  }
  return components;
}

export function prepareSourceDelivery({ root, commit, version, reportPath, directory,
  archiveInput = '',
  requirements = sourceRequirements, componentInventory = distributionComponents,
  checkSource = () => requireCleanReleaseSource({ root }) }) {
  if (!/^[a-f0-9]{40}$/.test(commit) || !/^\d+\.\d+\.\d+$/.test(version)) {
    throw new Error('Exact source commit/version required');
  }
  if (checkSource().commit !== commit) throw new Error('Source delivery commit differs from clean HEAD');
  const decision = assetDecision(root, embeddedAssets(root));
  if (!decision.approved) throw new Error('Embedded material use is not approved');
  const inventory = requirements(root);
  const previous = fs.existsSync(reportPath) ? JSON.parse(fs.readFileSync(reportPath, 'utf8')) : null;
  if (previous && (previous.schema !== 'hypha-distribution-provenance-v1' || previous.commit !== commit
      || previous.releaseTag !== `v${version}`)) throw new Error('Existing provenance belongs to another candidate');
  const components = previous?.components || componentInventory(root, inventory);
  const expected = new Map(inventory.files.map(f => [f.archive, f]));
  const licenseFiles = new Set(components.flatMap(c => c.licenseFiles || []));
  validateComponentInventory(components, inventory, new Map(inventory.files.filter(f =>
    licenseFiles.has(f.archive)).map(f => [f.archive, fs.readFileSync(f.file)])));
  fs.mkdirSync(directory, { recursive: true });
  const archive = path.join(directory, `Kirin-Hypha-${version}-Corresponding-Source.zip`);
  if (archiveInput && !fs.existsSync(archive)) {
    const imported = readSourceZip(archiveInput);
    if (imported.size !== expected.size || inventory.files.some(f =>
      !imported.get(f.archive)?.equals(fs.readFileSync(f.file)))) throw new Error('Imported source archive differs from exact candidate');
    fs.copyFileSync(archiveInput, archive, fs.constants.COPYFILE_EXCL);
  }
  const entries = fs.existsSync(archive) ? readSourceZip(archive) : writeSourceZip(archive, inventory.files);
  if (entries.size !== expected.size || inventory.files.some(f =>
    !entries.get(f.archive)?.equals(fs.readFileSync(f.file)))) throw new Error('Retained source no longer matches candidate');
  rejectHeldBytes(readAssetRegistry(root), entries);
  const fact = fileFact(archive);
  const url = `https://github.com/heyalohaloha/kirin_hypha/releases/download/v${version}/${path.basename(archive)}`;
  if (previous?.sourceSha256 && previous.sourceSha256 !== fact.sha256) {
    throw new Error('Existing source delivery hash differs');
  }
  const legalDir = path.join(directory, 'legal');
  const names = [...new Set([...REQUIRED, ...components.flatMap(c => c.licenseFiles)])].sort();
  fs.mkdirSync(legalDir, { recursive: true });
  for (const name of names) {
    const entry = expected.get(name);
    if (!entry) throw new Error(`Required notice not in source inventory: ${name}`);
    const destination = path.join(legalDir, name);
    const bytes = entries.get(name);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    if (fs.existsSync(destination) && !fs.readFileSync(destination).equals(bytes)) {
      throw new Error('Existing legal bytes differ; preserve prior candidate');
    }
    if (!fs.existsSync(destination)) fs.writeFileSync(destination, bytes, { flag: 'wx' });
  }
  const delivery = { commit, sha256: fact.sha256, url };
  const deliveryFile = path.join(legalDir, 'source-delivery.json');
  if (fs.existsSync(deliveryFile)
      && JSON.stringify(JSON.parse(fs.readFileSync(deliveryFile, 'utf8'))) !== JSON.stringify(delivery)) {
    throw new Error('Existing source pointer differs');
  }
  atomicJson(deliveryFile, delivery);
  names.push('source-delivery.json');
  const componentFile = path.join(legalDir, 'component-notices.json');
  const componentNotices = { schema: 'hypha-delivered-components-v1', commit,
    sourceSha256: fact.sha256, scope: 'Conservative normal/build dependency inventory; actual linkage review remains required.', components };
  if (fs.existsSync(componentFile) && JSON.stringify(JSON.parse(fs.readFileSync(componentFile, 'utf8')))
      !== JSON.stringify(componentNotices)) throw new Error('Existing component notices differ');
  atomicJson(componentFile, componentNotices);
  names.push('component-notices.json');
  atomicJson(path.join(legalDir, 'legal-delivery.json'), {
    schema: 'hypha-legal-delivery-v1', commit, sourceSha256: fact.sha256, sourceDownload: url,
    files: names.map(name => ({ path: name, sha256: sha256(fs.readFileSync(path.join(legalDir, name))) })),
  });
  verifyLegalDelivery(legalDir, commit);
  const report = { ...previous, schema: 'hypha-distribution-provenance-v1', commit,
    releaseTag: `v${version}`, kimeraEmbedded: false, sourceArchive: path.relative(root, archive),
    sourceSha256: fact.sha256, sourceDownload: url, components, payloads: previous?.payloads || [] };
  atomicJson(reportPath, report);
  return { legalDir, sourceArchive: archive, reportPath, facts: [fact,
    fileFact(path.join(legalDir, 'legal-delivery.json'))] };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [root, commit, version, reportPath, directory] = process.argv.slice(2);
    if (!directory) throw new Error('Usage: source_delivery.mjs ROOT COMMIT VERSION REPORT SOURCE_DIR');
    const result = prepareSourceDelivery({ root: path.resolve(root), commit, version,
      reportPath: path.resolve(reportPath), directory: path.resolve(directory) });
    console.log(JSON.stringify({ sourceArchive: result.sourceArchive, legalDir: result.legalDir }));
  } catch (error) { console.error(`[source-delivery] ${error.message}`); process.exitCode = 1; }
}
