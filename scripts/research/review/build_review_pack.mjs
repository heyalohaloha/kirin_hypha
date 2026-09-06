// Private, offline review pack. Never opens reserved evaluation audio.
import { readFile, writeFile, readdir, mkdir, copyFile, stat } from 'node:fs/promises';
import { constants } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';
import { selectDevelopment } from '../probe_audio_corpus.mjs';
import { readFloatWav } from './wav.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const hash = value => createHash('sha256').update(value).digest('hex');
export const selection = [
  ['space', '12'], ['space', '03'], ['space', '01'],
  ['attack', '17'], ['attack', '05'], ['attack', '14'],
];
export function inlineJson(value) {
  return JSON.stringify(value).replaceAll('<', '\\u003c').replaceAll('>', '\\u003e')
    .replaceAll('&', '\\u0026').replaceAll('\u2028', '\\u2028').replaceAll('\u2029', '\\u2029');
}
export async function buildPack(root, output, options = {}) {
  const activeSelection = options.selection ?? selection;
  const proposal = JSON.parse(await readFile(path.join(root, 'corpus-proposal.json'), 'utf8'));
  const partitions = { space: selectDevelopment(proposal, 'space'), attack: selectDevelopment(proposal, 'attack') };
  const dirs = await readdir(root);
  const items = [], provenance = [];
  // Gather and verify everything before creating the output folder.
  for (const [mode, number] of activeSelection) {
    const id = `${mode}_development-${number}`;
    const candidate = partitions[mode].find(item => item.id === id);
    if (!candidate) throw new Error(`Missing development item ${id}`);
    const matches = dirs.filter(dir => dir.startsWith(`${mode}-development-`));
    if (matches.length !== 1) throw new Error(`Ambiguous development directory: ${mode}`);
    const audioPath = path.join(root, matches[0], `${id}.wav`);
    const audio = await readFile(audioPath), facts = readFloatWav(audio);
    if (facts.frames !== facts.rate * 30 || facts.peak <= 0 || facts.peak > 1)
      throw new Error(`Invalid duration, silence or above-full-scale source: ${id}`);
    const priorBytes = await readFile(path.join(root, matches[0], `${id}.json`));
    const prior = JSON.parse(priorBytes);
    if (prior.item?.id !== id || prior.item?.purpose !== `${mode}_development`
      || prior.item?.path !== candidate.path || prior.source_unchanged_after_decode !== true
      || !/^[a-f0-9]{64}$/.test(prior.source_sha256)) throw new Error(`Unverified excerpt provenance: ${id}`);
    const digest = hash(audio);
    // The probe records source identity outside its numerical analysis.
    const title = path.basename(candidate.path).replace(/\.flac$/i, '');
    const preview = options.reviewWindow
      ? options.reviewWindow({ id, mode, rate: facts.rate, frames: facts.frames })
      : [10 * facts.rate, 20 * facts.rate];
    if (!Array.isArray(preview) || preview.length !== 2
      || !preview.every(Number.isSafeInteger) || preview[0] < 0
      || preview[1] > facts.frames || preview[0] >= preview[1]) {
      throw new Error(`Invalid review window: ${id}`);
    }
    items.push({ id, mode, title, audio: `audio/${id}.wav`, audio_sha256: digest,
      rate: facts.rate, channels: facts.channels, frames: facts.frames, step: facts.step,
      wave: facts.wave, peak: facts.peak, rms: facts.rms, bytes: audio.length,
      source_start_sample: 30 * facts.rate, preview });
    provenance.push({ id, source_path: candidate.path, artist_group_sha256: candidate.artist_group_sha256,
      research_excerpt: audioPath, research_result_sha256: hash(priorBytes), source_sha256: prior.source_sha256,
      audio_sha256: digest,
      source_start_seconds: 30, duration_seconds: 30, byte_identity_to_research_excerpt: true,
      selection_reason: options.selectionReasons?.[id]
        ?? 'development pilot; contrasting musical arrangements; fixed central 10s, not candidate-centred' });
  }
  const assets = {};
  for (const name of ['shell.html', 'review.css', 'model.js', 'wave.js', 'app.js'])
    assets[name] = await readFile(path.join(here, name), 'utf8');
  const protocol = 'hypha.pilot-review.v1';
  const identity = items.map(({ wave, ...item }) => item);
  const pack = { schema: protocol, pack_id: options.packId ?? 'hypha-pilot-20260907-01',
    protocol: options.answerProtocol ?? 'development-first-pass-v1',
    candidate_exposure: false, product_qualified: false,
    manifest_sha256: hash(JSON.stringify({ protocol, identity })), items };
  let html = assets['shell.html'].replace('/* REVIEW_STYLE */', assets['review.css']);
  html = html.replace('<!-- REVIEW_DATA -->', `<script id="review-data" type="application/json">${inlineJson(pack)}</script>`);
  html = html.replace('/* REVIEW_SCRIPTS */', [assets['model.js'], assets['wave.js'], assets['app.js']].join('\n'));
  await mkdir(output, { recursive: false, mode: 0o700 });
  await mkdir(path.join(output, 'audio'), { mode: 0o700 });
  for (const item of items) {
    const source = provenance.find(row => row.id === item.id).research_excerpt;
    const destination = path.join(output, item.audio);
    await copyFile(source, destination, constants.COPYFILE_EXCL);
    if (hash(await readFile(destination)) !== item.audio_sha256) throw new Error('Copy hash mismatch');
  }
  await writeFile(path.join(output, 'index.html'), html, { flag: 'wx', mode: 0o600 });
  await writeFile(path.join(output, 'manifest.json'), JSON.stringify({ ...pack, items: identity,
    html_sha256: hash(html), provenance }, null, 2) + '\n', { flag: 'wx', mode: 0o600 });
  const readme = options.readme ?? ('Hypha 判定パック 01\n\nindex.html をChromeで開いてください。ネット接続は不要です。\n'
    + 'SPACE 3件、2MIX ATTACK 3件です。未回答から再開します。\n'
    + '途中でも「回答JSONを保存」で書き出し、そのJSONをCodexへ添付してください。\n'
    + 'audioフォルダはindex.htmlと一緒に保持してください。音源は私用の検証用で、再配布しないでください。\n'
    + 'これは開発用の初回判定です。製品の精度、Blind、Windows実動検証の合格を意味しません。\n');
  await writeFile(path.join(output, 'はじめに.txt'), readme, { flag: 'wx', mode: 0o600 });
  return { output, items: items.length, audio_bytes: items.reduce((sum, item) => sum + item.bytes, 0),
    html_bytes: (await stat(path.join(output, 'index.html'))).size, manifest_sha256: pack.manifest_sha256,
    sources: items.map(({ id, rate, channels, frames, peak, rms }) => ({ id, rate, channels, frames, peak, rms })) };
}
if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  if (process.argv.length !== 4) throw new Error('Usage: build_review_pack.mjs RESEARCH_ROOT NEW_OUTPUT_DIRECTORY');
  process.umask(0o077);
  console.log(JSON.stringify(await buildPack(path.resolve(process.argv[2]), path.resolve(process.argv[3])), null, 2));
}
