// Metadata-only local discovery. Does not decode audio, choose a holdout, or grant usage rights.
// Usage: node audio_inventory.mjs OUTPUT_JSON ROOT [ROOT ...]
import { spawnSync } from 'node:child_process';
import { lstat, realpath, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

export async function inventory(roots) {
  const entries = [], failures = [], seen = new Set();
  for (const requested of roots) {
    const root = await realpath(requested);
    if (!(await lstat(root)).isDirectory()) throw new Error('Input root must be a directory');
    const result = spawnSync('rg', ['--files', '--hidden', '--no-ignore', '--null',
      '-g', '*.[wW][aA][vV]', '-g', '*.[aA][iI][fF]', '-g', '*.[aA][iI][fF][fF]',
      '-g', '*.[fF][lL][aA][cC]', '-g', '*.[mM][pP]3', '-g', '*.[mM]4[aA]',
      '-g', '!**/node_modules/**', '-g', '!**/.git/**', '-g', '!**/target/**',
      '-g', '!**/build/**', '-g', '!**/build-universal/**', '-g', '!**/vendor/**',
      '-g', '!**/JUCE/**', '-g', '!**/.Trash/**', '-g', '!**/.Trashes/**',
      '-g', '!**/.Spotlight-V100/**', '-g', '!**/.fseventsd/**', root],
    { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
    if (result.error || ![0, 1].includes(result.status))
      throw new Error(`File discovery failed for root: ${root}`);
    for (const file of result.stdout.split('\0').filter(Boolean).sort()) {
      if (path.basename(file).startsWith('._')) continue;
      if (seen.has(file)) continue;
      seen.add(file);
      try {
        const stat = await lstat(file);
        if (!stat.isFile() || stat.isSymbolicLink()) continue;
        const relative = path.relative(root, file);
        entries.push({ path: file, root, relative, bytes: stat.size,
          modified_ms: stat.mtimeMs, extension: path.extname(file).toLowerCase(),
          top_directory: relative.split(path.sep)[0],
          assignment: 'unassigned', song_identity: null, annotation: 'not_checked',
          isolation: 'metadata_only_not_a_fresh_holdout_claim' });
      } catch { failures.push({ path: file, reason: 'metadata_unavailable' }); }
    }
  }
  const byRoot = {}, byExtension = {};
  for (const entry of entries) {
    byRoot[entry.root] ??= { files: 0, bytes: 0 };
    byRoot[entry.root].files++;
    byRoot[entry.root].bytes += entry.bytes;
    byExtension[entry.extension] = (byExtension[entry.extension] ?? 0) + 1;
  }
  return { schema: 'hypha.research.audio-inventory.v1', payload_read: false,
    product_qualified: false, usage_permission: 'must_be_confirmed_for_selected_sources',
    files: entries.length, bytes: entries.reduce((sum, item) => sum + item.bytes, 0),
    by_root: byRoot, by_extension: byExtension, failures, entries };
}

async function main() {
  const [output, ...roots] = process.argv.slice(2);
  if (!output || roots.length === 0) throw new Error('Usage: audio_inventory.mjs OUTPUT_JSON ROOT [ROOT ...]');
  const report = await inventory(roots);
  // Exclusive create protects existing reports and any accidentally supplied source file.
  await writeFile(output, `${JSON.stringify(report, null, 2)}\n`, { flag: 'wx', mode: 0o600 });
  console.log(JSON.stringify({ files: report.files, bytes: report.bytes,
    by_root: report.by_root, by_extension: report.by_extension, failures: report.failures.length }));
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href)
  main().catch(error => { console.error(error.message); process.exitCode = 1; });
