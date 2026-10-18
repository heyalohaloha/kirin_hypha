// PACE-sealed internal aliases: the only symlinks an extracted macOS distribution payload may carry.
// Rules come from the frozen signed AAX source trees; every alias must match one exactly.
import fs from 'node:fs';
import path from 'node:path';
import { buildTreeManifest } from '../ls_release/aax_submission_archive.mjs';
import { loadMacAaxBundleManifest } from '../ls_release/kirin_hypha_aax_bundles.mjs';
import { digest, hash } from '../ls_release/hypha_release_contract.mjs';

export const SEALED_ALIAS_SCHEMA = 'hypha-sealed-pace-alias-rules-v1';
// Container-relative bundle location per Mac channel; Windows payloads carry no aliases.
const LAYOUTS = { 'macos-pkg': 'install_relative', 'macos-zip': 'archive_relative' };
const SEAL = 'Contents/_CodeSignature/CodeResources';
const XML = { lt: '<', gt: '>', amp: '&', quot: '"', apos: "'" };

const modeOf = stat => (stat.mode & 0o7777).toString(8).padStart(4, '0');
const unxml = text => text.replace(/&(lt|gt|amp|quot|apos);/g, (_, name) => XML[name]);

const safeRelative = value => typeof value === 'string' && !!value && !path.posix.isAbsolute(value)
  && !/[\\\0\r\n]/.test(value) && value.split('/').every(p => p && p !== '.' && p !== '..');

// Bundle-relative target: only leading "../" ascents inside the bundle, then plain names.
function aliasTarget(link, text) {
  if (typeof text !== 'string' || !text || path.posix.isAbsolute(text) || /[\\\0\r\n]/.test(text)) {
    throw new Error(`Sealed PACE alias link text must be relative: ${link}`);
  }
  const parts = text.split('/');
  let ups = 0;
  while (parts[ups] === '..') ups += 1;
  const base = link.split('/').slice(0, -1);
  if (ups > base.length) throw new Error(`Sealed PACE alias escapes its bundle: ${link}`);
  const names = parts.slice(ups);
  if (!names.length || names.some(p => !p || p === '.' || p === '..')) {
    throw new Error(`Sealed PACE alias link text is not canonical: ${link}`);
  }
  return [...base.slice(0, base.length - ups), ...names].join('/');
}

// Symlink records sealed in CodeResources files2 (keys relative to Contents/).
function sealedSymlinks(bytes) {
  const xml = bytes.toString('utf8');
  const records = new Map();
  const start = xml.indexOf('<key>files2</key>');
  if (start < 0 || !/^\s*<dict>/.test(xml.slice(start + 17))) return records;
  const token = /<(\/?)dict>|<key>([^<]*)<\/key>|<string>([^<]*)<\/string>/g;
  token.lastIndex = start + 17;
  let depth = 0; let entry = null; let key = null;
  for (let m = token.exec(xml); m; m = token.exec(xml)) {
    if (m[0] === '<dict>') {
      depth += 1; if (depth === 2) entry = key; key = null;
    } else if (m[0] === '</dict>') {
      depth -= 1; key = null; if (depth <= 0) break;
    } else if (depth === 0) {
      break;
    } else if (m[2] !== undefined) {
      key = unxml(m[2]);
    } else {
      if (depth === 2 && key === 'symlink' && entry !== null) {
        if (records.has(entry)) throw new Error(`Duplicate sealed symlink record: ${entry}`);
        records.set(entry, unxml(m[3]));
      }
      key = null;
    }
  }
  return records;
}

function bundleAliases(spec, frozen) {
  if (fs.lstatSync(spec.sourcePath).isSymbolicLink()) throw new Error('Payload root must not be a symlink');
  // One manifest read: its digest is the treeFact compared with the freeze, its entries are the rules.
  const manifest = buildTreeManifest(spec.sourcePath);
  const treeSha256 = digest(manifest);
  const fact = JSON.stringify({ path: path.resolve(spec.sourcePath), treeSha256 });
  if (!frozen.some(expected => JSON.stringify(expected) === fact)) {
    throw new Error(`${spec.role} AAX source bundle is not a frozen signed payload; sealed aliases unavailable`);
  }
  const entries = new Map(manifest.entries.map(e => [e.path, e]));
  const links = manifest.entries.filter(e => e.type === 'symlink');
  if (!links.length) return { role: spec.role, treeSha256, aliases: [] };
  const seal = entries.get(SEAL);
  if (seal?.type !== 'file' || entries.get('Contents')?.type !== 'directory'
      || entries.get('Contents/_CodeSignature')?.type !== 'directory') {
    throw new Error(`${spec.role} AAX source aliases have no code signature seal`);
  }
  const sealBytes = fs.readFileSync(path.join(spec.sourcePath, SEAL));
  if (hash(sealBytes) !== seal.sha256) throw new Error(`${spec.role} AAX source seal changed after freeze`);
  const sealed = sealedSymlinks(sealBytes);
  const aliases = links.map(link => {
    const target = aliasTarget(link.path, link.target);
    const parts = target.split('/');
    for (let i = 1; i < parts.length; i += 1) {
      const ancestor = entries.get(parts.slice(0, i).join('/'));
      if (ancestor?.type === 'symlink') throw new Error(`Sealed PACE alias target traverses a symlink: ${link.path}`);
      if (ancestor?.type !== 'directory') throw new Error(`Sealed PACE alias target is not a regular file: ${link.path}`);
    }
    const file = entries.get(target);
    if (file?.type !== 'file') throw new Error(`Sealed PACE alias target is not a regular file: ${link.path}`);
    if (!link.path.startsWith('Contents/') || sealed.get(link.path.slice(9)) !== link.target) {
      throw new Error(`Source alias is not sealed by the code signature: ${link.path}`);
    }
    return { link: link.path, text: link.target, mode: link.mode, target,
      targetSha256: file.sha256, targetSize: file.size_bytes, targetMode: file.mode, sealSha256: seal.sha256 };
  });
  return { role: spec.role, treeSha256, aliases };
}

function assertRuleSet(sealed) {
  const bundles = sealed?.bundles;
  if (sealed?.schema !== SEALED_ALIAS_SCHEMA || !Array.isArray(bundles) || !bundles.length
      || new Set(bundles.map(b => b.role)).size !== bundles.length
      || bundles.some(b => !Number.isSafeInteger(b.aliasCount) || b.aliasCount < 0)
      || JSON.stringify(Object.keys(sealed.channels || {}).sort()) !== JSON.stringify(Object.keys(LAYOUTS).sort())) {
    throw new Error('Invalid sealed PACE alias rule set');
  }
  const total = bundles.reduce((sum, b) => sum + b.aliasCount, 0);
  for (const channel of Object.keys(LAYOUTS)) {
    const rules = sealed.channels[channel];
    // Every frozen source alias is expected exactly once; an empty or partial set is not a relaxation.
    if (!Array.isArray(rules) || rules.length !== total || new Set(rules.map(r => r.path)).size !== total
        || bundles.some(b => rules.filter(r => r.role === b.role).length !== b.aliasCount)) {
      throw new Error(`Expected ${channel} alias rule set does not match the frozen source bundles`);
    }
    for (const r of rules) {
      if (![r.bundle, r.link, r.target].every(safeRelative) || r.path !== `${r.bundle}/${r.link}`
          || aliasTarget(r.link, r.text) !== r.target) throw new Error('Invalid sealed PACE alias rule set');
    }
  }
  return sealed;
}

export function sealedAliasRules({ bundles, frozenPayloads }) {
  if (!Array.isArray(frozenPayloads) || !frozenPayloads.length) throw new Error('Frozen payload facts required for sealed PACE aliases');
  if (!Array.isArray(bundles) || !bundles.length) throw new Error('Signed AAX source bundles required for sealed PACE aliases');
  const sources = bundles.map(spec => {
    for (const field of Object.values(LAYOUTS)) {
      if (!safeRelative(spec[field])) throw new Error(`Sealed PACE alias ${spec.role} ${field} must be a safe relative path`);
    }
    return bundleAliases(spec, frozenPayloads);
  });
  const channels = Object.fromEntries(Object.entries(LAYOUTS).map(([channel, field]) => [channel,
    sources.flatMap((source, index) => source.aliases.map(alias => ({ role: source.role,
      bundle: bundles[index][field], path: `${bundles[index][field]}/${alias.link}`, ...alias })))]));
  return assertRuleSet({ schema: SEALED_ALIAS_SCHEMA, channels, bundles: sources.map(s =>
    ({ role: s.role, sourceTreeSha256: s.treeSha256, aliasCount: s.aliases.length })) });
}

export function frozenPayloadFacts(freeze) {
  const { sha256, ...frozen } = freeze || {};
  if (!sha256 || digest(frozen) !== sha256 || !Array.isArray(frozen.payloads) || !frozen.payloads.length) {
    throw new Error('Frozen candidate receipt required for sealed PACE aliases');
  }
  return frozen.payloads;
}

export function releaseSealedAliases(state) {
  const frozenPayloads = frozenPayloadFacts(state.freeze);
  return sealedAliasRules({ bundles: loadMacAaxBundleManifest({ root: state.root }).bundles, frozenPayloads });
}

// Regular file reached without any symlink component below the payload root.
function realFile(root, relative, link) {
  let current = root;
  for (const part of relative.split('/')) {
    current = path.join(current, part);
    const stat = fs.lstatSync(current, { throwIfNoEntry: false });
    if (!stat) throw new Error(`Sealed PACE alias target is missing: ${link}`);
    if (stat.isSymbolicLink()) throw new Error(`Sealed PACE alias target traverses a symlink: ${link}`);
  }
  if (!fs.lstatSync(current).isFile()) throw new Error(`Sealed PACE alias target is not a regular file: ${link}`);
  return current;
}

function verifyAlias(root, rule) {
  const stat = fs.lstatSync(path.join(root, ...rule.path.split('/')));
  if (!stat.isSymbolicLink() || modeOf(stat) !== rule.mode) throw new Error(`Sealed PACE alias mode changed: ${rule.path}`);
  const text = fs.readlinkSync(path.join(root, ...rule.path.split('/')));
  if (aliasTarget(rule.link, text) !== rule.target || text !== rule.text) {
    throw new Error(`Sealed PACE alias text or target changed: ${rule.path}`);
  }
  const target = realFile(root, `${rule.bundle}/${rule.target}`, rule.path);
  const seal = realFile(root, `${rule.bundle}/${SEAL}`, rule.path);
  if (hash(fs.readFileSync(seal)) !== rule.sealSha256) throw new Error(`Sealed PACE alias code signature seal changed: ${rule.path}`);
  const bytes = fs.readFileSync(target);
  if (bytes.length !== rule.targetSize || hash(bytes) !== rule.targetSha256) {
    throw new Error(`Sealed PACE alias target bytes changed: ${rule.path}`);
  }
  if (modeOf(fs.lstatSync(target)) !== rule.targetMode) throw new Error(`Sealed PACE alias target mode changed: ${rule.path}`);
  return { role: rule.role, path: rule.path, text, mode: rule.mode, target: `${rule.bundle}/${rule.target}`,
    targetSha256: rule.targetSha256, targetSize: rule.targetSize, targetMode: rule.targetMode, sealSha256: rule.sealSha256 };
}

function walk(root, prefix, files, links) {
  for (const entry of fs.readdirSync(path.join(root, prefix), { withFileTypes: true })) {
    if (entry.name === '.git') continue; // Same skip as the strict walker this replaces.
    const file = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (entry.isSymbolicLink()) links.push(file);
    else if (entry.isDirectory()) walk(root, file, files, links);
    else if (entry.isFile()) files.push(file);
  }
}

// Regular files for the extraction inventory, plus verified alias evidence kept apart from them.
export function sealedPayloadInventory(root, channel, sealed) {
  assertRuleSet(sealed);
  const rules = LAYOUTS[channel] ? sealed.channels[channel] : [];
  const files = []; const links = [];
  walk(root, '', files, links);
  const found = new Set(links);
  const aliases = rules.map(rule => {
    // Only a link the walk reached is checked, so no ancestor of the link itself is a symlink.
    if (!found.has(rule.path)) throw new Error(`Missing sealed PACE alias in ${channel} payload: ${rule.path}`);
    return { channel, ...verifyAlias(root, rule) };
  });
  const expected = new Set(rules.map(r => r.path));
  const extra = links.find(link => !expected.has(link));
  if (extra) throw new Error(`Unexpected symlink in ${channel} payload: ${extra}`);
  return { files, aliases };
}
