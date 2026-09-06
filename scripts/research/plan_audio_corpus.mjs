// Local-only proposed partitions. Folder names are not proof of independent song identity.
import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const hash = value => createHash('sha256').update(value).digest('hex');
export function planCorpus(inventory, root) {
  if (inventory.schema !== 'hypha.research.audio-inventory.v1') throw new Error('Unknown inventory');
  const groups = new Map(), excluded = [];
  for (const entry of inventory.entries) {
    if (entry.root !== root || !['.flac', '.wav', '.aif', '.aiff'].includes(entry.extension)) continue;
    const parts = entry.relative.split(path.sep);
    if (parts.some(part => part === '..')) throw new Error('Invalid inventory relative path');
    if (parts.length < 3) {
      excluded.push({ path: entry.path, reason: 'artist_album_layout_unconfirmed' });
      continue;
    }
    const artist = parts[0].normalize('NFKC').toLowerCase();
    if (!groups.has(artist)) groups.set(artist, []);
    groups.get(artist).push(entry);
  }
  if (groups.size < 64) throw new Error('Need at least 64 artist groups before proposing partitions');
  const sorted = [...groups.entries()].sort((a, b) => hash(`hypha-corpus-v1:${a[0]}`)
    .localeCompare(hash(`hypha-corpus-v1:${b[0]}`)));
  const partitions = [], counts = {};
  let cursor = 0;
  for (const [purpose, count] of [['attack_development', 20], ['attack_evaluation_reserved', 20],
    ['space_development', 12], ['space_evaluation_reserved', 12]]) {
    counts[purpose] = count;
    for (let i = 0; i < count; i++) {
      const [artist, entries] = sorted[cursor++];
      entries.sort((a, b) => a.relative.localeCompare(b.relative));
      const selected = entries[0];
      partitions.push({ id: `${purpose}-${String(i + 1).padStart(2, '0')}`, purpose,
        artist_group_sha256: hash(artist), path: selected.path, bytes: selected.bytes,
        other_artist_files: entries.slice(1).map(entry => entry.path),
        excerpt_start_seconds: 30, excerpt_duration_seconds: 30,
        source_duration_verified: false, song_identity_verified: false,
        prior_exposure: 'unknown', payload_read_by_this_planner: false,
        annotation: 'pending_independent_human_annotation',
        eligibility: 'proposal_only_not_authorized_for_final_holdout_score' });
    }
  }
  return { schema: 'hypha.research.corpus-proposal.v1', inventory_sha256: hash(JSON.stringify(inventory)),
    product_qualified: false, counts, total_proposed: partitions.length, available_artist_groups: groups.size,
    caveat: 'Artist-folder isolation is provisional. Verify song identity, related versions, earlier exposure, duration, coverage and human annotations before freezing any evaluation set.',
    excluded, partitions };
}

async function main() {
  const [input, root, output] = process.argv.slice(2);
  if (!input || !root || !output || process.argv.length !== 5)
    throw new Error('Usage: plan_audio_corpus.mjs INVENTORY_JSON QOBUZ_ROOT OUTPUT_JSON');
  const report = planCorpus(JSON.parse(await readFile(input, 'utf8')), root);
  await writeFile(output, `${JSON.stringify(report, null, 2)}\n`, { flag: 'wx', mode: 0o600 });
  console.log(JSON.stringify({ counts: report.counts, total_proposed: report.total_proposed,
    available_artist_groups: report.available_artist_groups, product_qualified: false }));
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href)
  main().catch(error => { console.error(error.message); process.exitCode = 1; });
