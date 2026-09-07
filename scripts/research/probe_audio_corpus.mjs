// Execute only proposed DEVELOPMENT excerpts. Reserved evaluation audio is never opened here.
// Sources: https://ffmpeg.org/ffmpeg.html and https://ffmpeg.org/ffprobe.html
import { spawnSync } from 'node:child_process';
import { createReadStream } from 'node:fs';
import { readFile, writeFile, stat, mkdtemp } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

export function selectDevelopment(proposal, mode) {
  if (proposal.schema !== 'hypha.research.corpus-proposal.v1' || !['attack', 'space'].includes(mode))
    throw new Error('Only attack/space development proposals are accepted');
  const items = proposal.partitions.filter(item => item.purpose === `${mode}_development`);
  if (items.length !== (mode === 'attack' ? 20 : 12)) throw new Error('Incomplete development partition');
  for (const item of items) {
    if (!new RegExp(`^${mode}_development-\\d{2}$`).test(item.id)
      || !path.isAbsolute(item.path) || item.excerpt_start_seconds !== 30
      || item.excerpt_duration_seconds !== 30) throw new Error('Invalid proposed excerpt');
  }
  if (new Set(items.map(item => item.id)).size !== items.length) throw new Error('Duplicate item ID');
  const paths = new Set(items.map(item => path.resolve(item.path)));
  if (paths.size !== items.length) throw new Error('Duplicate development path');
  const groups = new Set(items.map(item => item.artist_group_sha256));
  if (groups.has(undefined) || groups.size !== items.length) throw new Error('Invalid artist isolation');
  for (const item of proposal.partitions.filter(item => item.purpose !== `${mode}_development`)) {
    if (groups.has(item.artist_group_sha256)
      || [item.path, ...(item.other_artist_files ?? [])].some(file => paths.has(path.resolve(file))))
      throw new Error('Development overlaps another partition');
  }
  return items;
}

function run(command, args, maxBuffer = 16 * 1024 * 1024) {
  const result = spawnSync(command, args, { encoding: 'utf8', maxBuffer, timeout: 120000 });
  if (result.error || result.status !== 0) throw new Error(`${command} failed: ${result.error?.message ?? result.stderr}`);
  return result.stdout;
}
async function sha256(file) {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(file)) hash.update(chunk);
  return hash.digest('hex');
}
async function main() {
  const [proposalPath, mode, parametersPath, executable, outputParent] = process.argv.slice(2);
  if (process.argv.length !== 7)
    throw new Error('Usage: probe_audio_corpus.mjs PROPOSAL_JSON attack|space PARAMETERS_JSON PROBE_EXE OUTPUT_PARENT');
  const proposal = JSON.parse(await readFile(proposalPath, 'utf8'));
  const parameters = JSON.parse(await readFile(parametersPath, 'utf8'));
  if (parameters.purpose !== 'development_only') throw new Error('Non-development parameters refused');
  const items = selectDevelopment(proposal, mode);
  process.umask(0o077);
  const output = await mkdtemp(path.join(outputParent, `${mode}-development-`));
  const results = [];
  for (const item of items) {
    const before = await stat(item.path);
    const sourceHash = await sha256(item.path);
    const metadata = JSON.parse(run('ffprobe', ['-v', 'error', '-select_streams', 'a:0',
      '-show_entries', 'stream=sample_rate,channels:format=duration', '-of', 'json', item.path]));
    const stream = metadata.streams?.[0];
    const duration = Number(metadata.format?.duration);
    const rate = Number(stream?.sample_rate), channels = Number(stream?.channels);
    if (!Number.isFinite(duration) || duration < 60 || ![1, 2].includes(channels)
      || ![44100, 48000, 88200, 96000, 176400, 192000].includes(rate))
      throw new Error(`Unsupported source or incomplete excerpt: ${item.id}`);
    const excerpt = path.join(output, `${item.id}.wav`);
    // Output-side seek; no normalization, resampling, downmix or editing of the source.
    run('ffmpeg', ['-nostdin', '-v', 'error', '-n', '-i', item.path, '-ss', '30', '-t', '30',
      '-map', '0:a:0', '-vn', '-map_metadata', '-1', '-c:a', 'pcm_f32le', excerpt]);
    const after = await stat(item.path);
    if (before.size !== after.size || before.mtimeMs !== after.mtimeMs
      || sourceHash !== await sha256(item.path)) throw new Error(`Source changed: ${item.id}`);
    const facts = JSON.parse(run(executable, [excerpt, mode, parametersPath]));
    if (facts.product_qualified !== false || facts.human_annotation_evaluated !== false
      || facts.frames !== rate * 30 || facts.channels !== channels || facts.sample_rate !== rate
      || facts.pcm_bytes !== rate * 30 * channels * 4) throw new Error(`Probe contract mismatch: ${item.id}`);
    const record = { item, source_sha256: sourceHash, source_duration_seconds: duration,
      source_unchanged_after_decode: true, excerpt, facts };
    await writeFile(path.join(output, `${item.id}.json`), `${JSON.stringify(record, null, 2)}\n`, { flag: 'wx' });
    const early = facts.space_observations.filter(value => value.facts.early_db !== null).length;
    const decay = facts.space_observations.filter(value => value.rejection === null).length;
    const summary = { id: item.id, sample_rate: rate, channels, event_count: facts.event_count,
      early_count: early, decay_count: decay, compute_ms: facts.compute_elapsed_ms,
      space_skipped_capacity: facts.space_skipped_capacity };
    results.push(summary);
    console.log(JSON.stringify(summary));
  }
  await writeFile(path.join(output, 'summary.json'), `${JSON.stringify({ mode, product_qualified: false,
    human_annotation_evaluated: false, reserved_evaluation_files_opened: 0, results }, null, 2)}\n`, { flag: 'wx' });
  // Templates contain no candidate positions or scores. They are requests, not completed labels.
  for (const annotator of ['human_1', 'human_2'])
    await writeFile(path.join(output, `${annotator}-annotation-pending.json`), `${JSON.stringify({
      schema: 'hypha.research.annotation-request.v1', mode, annotator_identity: null,
      independence_confirmed: false, status: 'pending',
      items: items.map(item => ({ id: item.id, audio_file: `${item.id}.wav`,
        excerpt_start_in_source_seconds: 30, duration_seconds: 30, status: 'pending',
        ...(mode === 'attack' ? { onset_seconds_in_excerpt: null } : { eligible_intervals_in_excerpt: null }) }))
    }, null, 2)}\n`, { flag: 'wx' });
  console.log(JSON.stringify({ output, completed: results.length, product_qualified: false }));
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href)
  main().catch(error => { console.error(error.message); process.exitCode = 1; });
