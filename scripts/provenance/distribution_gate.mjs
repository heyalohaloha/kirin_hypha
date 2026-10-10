// Exact-artifact gate. Extraction/linkage review is retained evidence, never synthesized PASS.
import fs from 'node:fs';
import path from 'node:path';
import childProcess from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { readSourceZip } from './source_zip.mjs';
import { ROOT, readAssetRegistry, embeddedAssets, assetDecision, rejectHeldBytes, sha256 } from './asset_gate.mjs';
import { fileFact, Checkpoint, resolveInput } from '../ls_release/hypha_release_contract.mjs';

const MEDIA = /\.(png|jpe?g|mp4|otf|ttf)$/i;
const REQUIRED_NOTICES = ['LICENSE', 'THIRD_PARTY_NOTICES.md',
  'THIRD_PARTY_LICENSES/MoSQITo-1.2.1-Apache-2.0.txt'];

function command(root, executable, args) {
  return childProcess.execFileSync(executable, args, { cwd: root, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
}

function regular(root, relative) {
  if (typeof relative !== 'string' || !relative || relative.includes('\\') || relative.includes('\0')
      || path.isAbsolute(relative) || relative.split('/').some(p => !p || p === '.' || p === '..')) {
    throw new Error('Invalid distribution-relative file path');
  }
  let current = root;
  for (const part of relative.split('/')) {
    current = path.join(current, part);
    if (fs.lstatSync(current).isSymbolicLink()) throw new Error('Distribution evidence must not traverse symlinks');
  }
  if (!fs.statSync(current).isFile()) throw new Error('Distribution evidence is not a regular file');
  return current;
}

function sameFile(root, relative, expected) {
  const file = regular(root, relative);
  if (sha256(fs.readFileSync(file)) !== expected) throw new Error(`Delivered notice/license mismatch: ${relative}`);
  return fileFact(file);
}

function walkFiles(root, prefix = '') {
  const result = [];
  for (const entry of fs.readdirSync(path.join(root, prefix), { withFileTypes: true })) {
    if (entry.name === '.git') continue;
    const file = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (entry.isSymbolicLink()) throw new Error('Source dependency contains symlink; explicit source review required');
    if (entry.isDirectory()) result.push(...walkFiles(root, file));
    else if (entry.isFile()) result.push(file);
  }
  return result;
}

export function sourceRequirements(root) {
  const tracked = command(root, 'git', ['ls-files', '-z']).split('\0').filter(Boolean);
  // Documentation media not needed to rebuild may be omitted; compiled inputs are added below.
  const files = tracked.filter(p => p !== 'juce_shell/JUCE' && !MEDIA.test(p))
    .map(p => ({ archive: p, file: regular(root, p) }));
  for (const p of embeddedAssets(root)) files.push({ archive: p, file: regular(root, p) });
  const juceRoot = path.join(root, 'juce_shell/JUCE');
  const juceFiles = command(juceRoot, 'git', ['ls-files', '-z']).split('\0').filter(Boolean);
  if (!juceFiles.length) throw new Error('Corresponding Source needs the pinned JUCE source, not a gitlink directory');
  for (const p of juceFiles) files.push({ archive: `juce_shell/JUCE/${p}`, file: regular(juceRoot, p) });
  const metadata = JSON.parse(command(root, 'cargo', ['metadata', '--offline', '--locked', '--format-version', '1']));
  const ffi = metadata.packages.find(p => p.name === 'kirin_hypha_ffi');
  if (!ffi) throw new Error('Shipping Rust dependency inventory unavailable');
  const ids = new Set();
  const visit = id => {
    if (ids.has(id)) return;
    ids.add(id);
    for (const dep of metadata.resolve.nodes.find(n => n.id === id)?.deps || []) {
      if (dep.dep_kinds.some(k => k.kind == null || k.kind === 'build')) visit(dep.pkg);
    }
  };
  visit(ffi.id);
  const packages = metadata.packages.filter(p => ids.has(p.id));
  for (const pkg of packages.filter(p => p.source)) {
    const packageRoot = path.dirname(pkg.manifest_path);
    for (const p of walkFiles(packageRoot)) {
      files.push({ archive: `dependencies/${pkg.name}-${pkg.version}/${p}`, file: regular(packageRoot, p) });
    }
  }
  return { files, packages: packages.map(p => ({ id: `${p.name}@${p.version}`, license: p.license })) };
}

function resolvedLicenseDeclaration(value) {
  if (typeof value !== 'string' || !value.trim()) return false;
  const unresolved = /(^|[^a-z0-9])(?:unknown|noassertion|none|pending|tbd|unverified|unspecified|unconfirmed|unresolved|missing|n\/a|na|not[- _]?provided)(?=$|[^a-z0-9])|未確認|不明|要確認|\?/i;
  return !unresolved.test(value);
}

export function validateComponentInventory(components, inventory, entries) {
  if (!Array.isArray(components) || new Set(components.map(c => c.id)).size !== components.length) throw new Error('Missing/duplicate distribution component inventory');
  for (const c of components) {
    if (typeof c.id !== 'string' || !c.id.trim() || !resolvedLicenseDeclaration(c.license)
        || typeof c.source !== 'string' || !c.source.trim()
        || typeof c.modificationNotice !== 'string' || !c.modificationNotice.trim()
        || !Array.isArray(c.licenseFiles) || !c.licenseFiles.length) {
      throw new Error(`Incomplete or unresolved distribution license/source inventory: ${c.id}`);
    }
    for (const name of c.licenseFiles) if (!entries.has(name) || !entries.get(name).length) throw new Error(`License absent from Corresponding Source: ${name}`);
  }
  for (const expected of [{ id: 'MoSQITo@1.2.1', license: 'Apache-2.0' }, { id: 'JUCE@7.0.12', license: null }, ...inventory.packages]) {
    const c = components.find(row => row.id === expected.id);
    if (!c || (expected.license && c.license !== expected.license)) {
      throw new Error(`Incomplete distribution license/source inventory: ${expected.id}`);
    }
  }
}

export function verifyDistributionEvidence({ root, commit, releaseTag, artifacts, report, requirements = sourceRequirements }) {
  if (report.schema !== 'hypha-distribution-provenance-v1' || report.commit !== commit) {
    throw new Error('Distribution provenance candidate mismatch');
  }
  const registry = readAssetRegistry(root);
  const decision = assetDecision(root, embeddedAssets(root));
  if (!decision.approved) throw new Checkpoint(`New distribution holds ${decision.held.length} embedded materials; rights evidence or reviewed replacement required`);
  if (report.kimeraEmbedded !== false) throw new Checkpoint('Optional external font rights not verified for this gate; use the existing no-font build');
  if (!report.sourceArchive || !report.sourceSha256 || !report.sourceDownload) throw new Checkpoint('Retained Corresponding Source ZIP and delivery URL required');
  const sourceFile = resolveInput({ root }, report.sourceArchive);
  const sourceFact = fileFact(sourceFile);
  if (sourceFact.sha256 !== report.sourceSha256) throw new Error('Corresponding Source ZIP hash mismatch');
  const expectedUrl = `https://github.com/heyalohaloha/kirin_hypha/releases/download/${report.releaseTag}/${encodeURIComponent(path.basename(sourceFile))}`;
  if (!/^v\d+\.\d+\.\d+$/.test(releaseTag) || report.releaseTag !== releaseTag
      || report.sourceDownload !== expectedUrl) throw new Error('Invalid exact-release source delivery URL');
  const entries = readSourceZip(sourceFile);
  rejectHeldBytes(registry, entries);
  const inventory = requirements(root);
  const requiredNames = new Set(inventory.files.map(f => f.archive));
  for (const name of entries.keys()) {
    if (!requiredNames.has(name)) throw new Error(`Unexpected Corresponding Source file requires review: ${name}`);
  }
  for (const required of inventory.files) {
    if (!entries.has(required.archive) || !entries.get(required.archive).equals(fs.readFileSync(required.file))) {
      throw new Error(`Corresponding Source missing or changed: ${required.archive}`);
    }
  }
  const components = report.components;
  validateComponentInventory(components, inventory, entries);
  const channels = ['macos-pkg', 'macos-zip', 'windows-exe'];
  if (!Array.isArray(report.payloads) || report.payloads.length !== channels.length) throw new Error('All three actual distribution payloads required');
  const facts = [sourceFact];
  for (const channel of channels) {
    const payload = report.payloads.find(p => p.channel === channel);
    const artifact = artifacts[channels.indexOf(channel)];
    if (!payload || payload.artifactSha256 !== artifact.sha256 || !payload.extractionEvidence
        || payload.extractionResult !== 'PASS' || !payload.extractor?.trim()) throw new Error('Missing exact-artifact extraction evidence');
    const evidence = fileFact(resolveInput({ root }, payload.extractionEvidence));
    if (evidence.sha256 !== payload.extractionEvidenceSha256) throw new Error('Extraction evidence hash mismatch');
    facts.push(evidence);
    const payloadRoot = resolveInput({ root }, payload.root);
    const extraction = JSON.parse(fs.readFileSync(evidence.path, 'utf8'));
    const actualFiles = walkFiles(payloadRoot).sort().map(p => ({ path: p, sha256: sha256(fs.readFileSync(regular(payloadRoot, p))) }));
    if (extraction.schema !== 'hypha-extraction-evidence-v1' || extraction.artifactSha256 !== artifact.sha256
        || extraction.extractor !== payload.extractor || extraction.exitCode !== 0 || !extraction.reviewer?.trim()
        || !Array.isArray(extraction.command) || !extraction.command.length
        || JSON.stringify(extraction.payloadFiles) !== JSON.stringify(actualFiles)) {
      throw new Error('Actual extraction inventory/artifact/review evidence mismatch');
    }
    if (!payload.legalFiles || !payload.binaryFiles?.length) throw new Error('Extracted legal/binary files required');
    for (const required of REQUIRED_NOTICES) {
      facts.push(sameFile(payloadRoot, payload.legalFiles[required], sha256(fs.readFileSync(regular(root, required)))));
    }
    for (const component of components) for (const name of component.licenseFiles) {
      facts.push(sameFile(payloadRoot, payload.legalFiles[name], sha256(entries.get(name))));
    }
    const deliveryFile = regular(payloadRoot, payload.sourceDeliveryFile);
    const delivery = JSON.parse(fs.readFileSync(deliveryFile, 'utf8'));
    if (delivery.commit !== commit || delivery.sha256 !== sourceFact.sha256 || delivery.url !== report.sourceDownload) {
      throw new Error('Delivered Corresponding Source pointer is missing/mismatched');
    }
    facts.push(fileFact(deliveryFile));
    for (const binary of payload.binaryFiles) facts.push(sameFile(payloadRoot, binary.path, binary.sha256));
    const material = payload.buildMaterialEvidence && fileFact(resolveInput({ root }, payload.buildMaterialEvidence));
    if (!material || material.sha256 !== payload.buildMaterialEvidenceSha256) throw new Error('Missing build material evidence');
    const build = JSON.parse(fs.readFileSync(material.path, 'utf8'));
    if (build.schema !== 'hypha-build-material-evidence-v1' || build.commit !== commit || !build.reviewer?.trim()
        || !build.cmakeCaches?.length || JSON.stringify(build.binaryFiles) !== JSON.stringify(payload.binaryFiles)) {
      throw new Error('Build material candidate/binary review mismatch');
    }
    facts.push(material);
    for (const cache of build.cmakeCaches) {
      const cacheFact = fileFact(resolveInput({ root }, cache.path));
      const text = fs.readFileSync(cacheFact.path, 'utf8');
      if (cacheFact.sha256 !== cache.sha256
          || !/^KIRIN_HYPHA_KIMERA_FONT_FILE:FILEPATH=\s*$/m.test(text)) {
        throw new Error('Retained producer CMake configuration does not prove the no-font build');
      }
      facts.push(cacheFact);
    }
    rejectHeldBytes(registry, walkFiles(payloadRoot).map(p => [p, fs.readFileSync(regular(payloadRoot, p))]), 'binary');
  }
  return { facts, publicFacts: [sourceFact] };
}

export function verifyReleaseProvenance(state) {
  const reportPath = state.inputs.provenanceReport;
  if (!reportPath || !fs.existsSync(resolveInput(state, reportPath))) throw new Checkpoint('Exact-distribution provenance report is required; fixtures/local CI are not distribution evidence');
  const file = resolveInput(state, reportPath);
  const report = JSON.parse(fs.readFileSync(file, 'utf8'));
  const packageFacts = state.stages.packages.facts;
  const windowsAax = JSON.parse(fs.readFileSync(packageFacts[9].path, 'utf8'));
  const windowsInstaller = JSON.parse(fs.readFileSync(packageFacts[8].path, 'utf8'));
  if (windowsAax.release?.kimera_embedded !== false
      || windowsInstaller.distribution?.aax_identity?.kimera_embedded !== false) {
    throw new Checkpoint('Actual Windows AAX/installer font provenance must match the no-font distribution');
  }
  const result = verifyDistributionEvidence({ root: state.root, commit: state.candidate.commit,
    releaseTag: `v${state.candidate.version}`, artifacts: [packageFacts[0], packageFacts[3], packageFacts[6]], report });
  return { ...result, facts: [fileFact(file), ...result.facts] };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    if (process.argv.length !== 3) throw new Error('Usage: distribution_gate.mjs <ignored release pipeline state.json>');
    const state = JSON.parse(fs.readFileSync(path.resolve(ROOT, process.argv[2]), 'utf8'));
    const result = verifyReleaseProvenance(state);
    console.log(JSON.stringify({ result: 'PASS', retainedFiles: result.facts.length, sourceArchives: result.publicFacts.length }));
  } catch (error) { console.error(`[provenance-distribution] ${error.message}`); process.exitCode = 1; }
}
