// Raw source and actual linker evidence for the credential-free Windows producer.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { readRustRuntime, assertRustRuntime } from './runtime_guard.mjs';
import { canonicalSourceSnapshot } from './canonical_source.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const PLUGINVAL_SHA256 = 'f4ac5c31c5544a434f73e686b816861d92c22ae849d69ad693681cbc9522fcd8';
export function rawSourceSnapshot(root, { readRuntime = readRustRuntime } = {}) {
  return { schema: 'hypha-raw-source-v2', rustc: assertRustRuntime(readRuntime(root)), ...canonicalSourceSnapshot(root) };
}

export function assertRawSourceUnchanged(before, after) {
  if (before.schema !== 'hypha-raw-source-v2' || JSON.stringify(before) !== JSON.stringify(after)) {
    throw new Error('Raw source bytes changed since qualification started');
  }
}

export function assertPluginvalQualification(record, binaries, readFiles) {
  if (record.schema !== 'hypha-pluginval-qualification-v1' || record.toolSha256 !== PLUGINVAL_SHA256
      || record.results?.length !== 2 || record.results.some(r => r.exitCode !== 0)
      || record.before?.length !== 2 || JSON.stringify(record.before) !== JSON.stringify(record.after)) {
    throw new Error('Unsigned pluginval qualification is incomplete or changed');
  }
  for (const [index, binary] of binaries.entries()) {
    const expectedName = `Kirin Hypha ${binary.role}.vst3`;
    const files = record.after[index];
    if (record.results[index].bundle !== expectedName || !Array.isArray(files)
        || new Set(files.map(f => f.path)).size !== files.length) throw new Error('Pluginval bundle identity mismatch');
    const actual = readFiles(binary).sort((a,b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
    const qualified = [...files].sort((a,b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
    if (JSON.stringify(actual) !== JSON.stringify(qualified)) throw new Error('Payload differs from pluginval-qualified bytes');
  }
}

export function inspectNativeLinkMap(text) {
  if (!/Publics by Value/.test(text)) throw new Error('Expected an actual MSVC linker map');
  const families = { FLAC: /FLAC__/, Ogg: /ogg_(?:stream|sync|page|packet)_/,
    Vorbis: /vorbis_/, IJG: /jpeg_/, PNG: /png_/, zlib: /(?:inflate|deflate)(?:End|Init|Reset|@)/ };
  const matched = Object.entries(families).map(([component, pattern]) => {
    const lines = text.split(/\r?\n/).filter(line => pattern.test(line));
    if (!lines.length) throw new Error(`Linked component evidence missing: ${component}`);
    return { component, symbolCount: lines.length, examples: lines.slice(0, 3) };
  });
  return matched;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, output] = process.argv.slice(2);
    if (mode !== 'source' || !output) throw new Error('Usage: unsigned_qualification.mjs source OUTPUT');
    const snapshot = rawSourceSnapshot(ROOT);
    fs.mkdirSync(path.dirname(output), { recursive: true });
    fs.writeFileSync(output, JSON.stringify(snapshot, null, 2) + '\n', { flag: 'wx' });
  } catch (error) { console.error(`[unsigned-qualification] ${error.message}`); process.exitCode = 1; }
}
