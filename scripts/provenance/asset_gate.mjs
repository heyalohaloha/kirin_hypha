// Distribution permission is separate from local build/test permission.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

export const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const REGISTRY = 'docs/provenance/asset_distribution_registry.json';
const safeRelative = file => typeof file === 'string' && file.length > 0 && !file.includes('\\')
  && !file.includes('\0') && !path.isAbsolute(file) && !file.split('/').some(p => !p || p === '.' || p === '..');

export function readAssetRegistry(root) {
  const registry = JSON.parse(fs.readFileSync(path.join(root, REGISTRY), 'utf8'));
  if (registry.schema !== 'hypha-asset-distribution-registry-v1' || !Array.isArray(registry.assets)) {
    throw new Error('Invalid asset distribution registry');
  }
  const paths = new Set();
  for (const asset of registry.assets) {
    if (!safeRelative(asset.path) || paths.has(asset.path) || !/^[a-f0-9]{64}$/.test(asset.sha256)
        || !['hold', 'verified'].includes(asset.status)) throw new Error('Invalid/duplicate asset record');
    paths.add(asset.path);
    if (asset.status === 'verified' && (typeof asset.rightsEvidence !== 'string' || !asset.rightsEvidence.trim()
        || !Array.isArray(asset.allowedUses) || !asset.allowedUses.length
        || asset.allowedUses.some(use => !['binary', 'preview', 'source'].includes(use)))) {
      throw new Error('Verified asset needs retained rights evidence and allowed uses');
    }
  }
  return registry;
}

export function embeddedAssets(root) {
  const cmake = fs.readFileSync(path.join(root, 'juce_shell/CMakeLists.txt'), 'utf8');
  const registry = readAssetRegistry(root);
  if (registry.embedContractSha256 !== sha256(Buffer.from(cmake))) {
    throw new Error('Embedded material build contract changed; provenance review required');
  }
  const sources = cmake.match(/set\(KIRIN_HYPHA_DATA_SOURCES\s+([\s\S]*?)\)/)?.[1];
  if (!sources) throw new Error('Embedded asset source inventory unavailable');
  const assets = [...sources.matchAll(/\$\{CMAKE_CURRENT_SOURCE_DIR\}\/\.\.\/([^\s)]+)/g)].map(m => m[1]);
  if (!assets.length || sources.replace(/\$\{CMAKE_CURRENT_SOURCE_DIR\}\/\.\.\/[^\s)]+/g, '').trim()) {
    throw new Error('Unrecognized embedded asset declaration requires provenance review');
  }
  return assets;
}

export function assetDecision(root, paths, use = 'binary') {
  const registry = readAssetRegistry(root);
  const held = [];
  for (const file of paths) {
    if (!safeRelative(file)) throw new Error('Invalid embedded asset source path');
    const actual = sha256(fs.readFileSync(path.join(root, file)));
    const record = registry.assets.find(a => a.path === file && a.sha256 === actual);
    if (!record || record.status !== 'verified' || !record.allowedUses.includes(use)) {
      held.push({ path: file, sha256: actual, reason: 'Rights/use evidence Unknown or distribution hold' });
    }
  }
  return { approved: held.length === 0, use, held };
}

export function rejectHeldBytes(registry, entries, use = 'source') {
  const denied = new Set(registry.assets.filter(a => a.status === 'hold'
    || !a.allowedUses?.includes(use)).map(a => a.sha256));
  for (const [file, bytes] of entries) {
    if (denied.has(sha256(bytes))) throw new Error(`Held asset/use in new distribution: ${file}`);
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const inputs = embeddedAssets(ROOT);
    const binary = assetDecision(ROOT, inputs, 'binary');
    const preview = assetDecision(ROOT, inputs, 'preview');
    // Public diagnostic CI does not carry an exact NOTICE/source-delivery report.
    // Rights to embedded inputs alone must never authorize binary redistribution.
    const decision = { binaryInputs: binary, preview, binaryDistributionApproved: false };
    if (process.argv.slice(2).join(' ') === '--github-output') {
      if (!process.env.GITHUB_OUTPUT) throw new Error('GITHUB_OUTPUT unavailable');
      fs.appendFileSync(process.env.GITHUB_OUTPUT,
        `preview-approved=${preview.approved}\nbinary-approved=false\n`);
    } else if (process.argv.length > 2) throw new Error('Usage: asset_gate.mjs [--github-output]');
    console.log(JSON.stringify(decision, null, 2));
    // HOLD suppresses publication, not the required source/build/test matrix.
  } catch (error) { console.error(`[provenance-assets] ${error.message}`); process.exitCode = 1; }
}
