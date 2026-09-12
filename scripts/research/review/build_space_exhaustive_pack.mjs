#!/usr/bin/env node

// Builds a candidate-blind, exhaustive SPACE interval annotation pack from an already verified
// private review pack. Source paths and prior judgements are never copied into the new pack.
import { constants } from 'node:fs';
import { copyFile, mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
import path from 'node:path';

import { inlineJson } from './build_review_pack.mjs';
import { readFloatWav } from './wav.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const hash = value => createHash('sha256').update(value).digest('hex');
const protocol = 'development-space-exhaustive-v2';
const annotationScope = 'development_exhaustive_all_trackable_decay_intervals_30s_v1';

function isInside(parent, child) {
  const relative = path.relative(parent, child);
  return relative === '' || (!relative.startsWith(`..${path.sep}`) && relative !== '..'
    && !path.isAbsolute(relative));
}

export function exhaustiveSpaceItem(source, number, audio, facts) {
  if (source.mode !== 'space' || !/^[a-f0-9]{64}$/.test(source.audio_sha256)
    || facts.frames !== source.frames || facts.rate !== source.rate
    || facts.channels !== source.channels || hash(audio) !== source.audio_sha256
    || facts.frames !== facts.rate * 30 || facts.peak <= 0 || facts.peak > 1) {
    throw new Error(`Unverified SPACE source item: ${source.id ?? number}`);
  }
  const display = `SPACE ${String(number + 1).padStart(2, '0')}`;
  return {
    id: `space_exhaustive-${String(number + 1).padStart(2, '0')}`,
    mode: 'space', title: display, display_label: display, task_kind: 'SPACE / EXHAUSTIVE',
    question: '音が立ち上がった後、小さくなる様子を聴感で追える区間はありますか？',
    guide: '30秒全体を確認し、追える区間をすべて記録してください。立ち上がり後の張りや響きが小さくなる範囲です。残響かどうかや解析値を当てる必要はありません。次の音と重なり終点を追えない場合は、無理に印を付けずメモに残してください。',
    present_label: '追える減衰区間が1つ以上ある（全区間を記録）',
    none_label: '30秒全体に追える減衰区間がない',
    hide_source_title: true,
    audio: `audio/space_exhaustive-${String(number + 1).padStart(2, '0')}.wav`,
    audio_sha256: source.audio_sha256, rate: facts.rate, channels: facts.channels,
    frames: facts.frames, step: facts.step, wave: facts.wave, peak: facts.peak, rms: facts.rms,
    bytes: audio.length, source_start_sample: source.source_start_sample,
    preview: [0, facts.frames], minimum_listened_fraction: 0.95,
  };
}

export async function buildSpaceExhaustivePack(sourceDirectory, output, evidenceOutput) {
  const sourceRoot = path.resolve(sourceDirectory), reviewRoot = path.resolve(output);
  const evidenceRoot = path.resolve(evidenceOutput);
  if ([sourceRoot, reviewRoot, evidenceRoot].some((entry, index, values) =>
    values.some((other, otherIndex) => index !== otherIndex
      && (isInside(entry, other) || isInside(other, entry))))) {
    throw new Error('Source, review and evidence directories must be separate and non-nested');
  }
  const sourceManifestBytes = await readFile(path.join(sourceRoot, 'manifest.json'));
  const sourceManifest = JSON.parse(sourceManifestBytes);
  if (!Array.isArray(sourceManifest.items) || sourceManifest.items.length !== 6
    || new Set(sourceManifest.items.map(item => item.id)).size !== 6
    || sourceManifest.items.some(item => item.mode !== 'space')) {
    throw new Error('Expected six distinct SPACE items in the source pack');
  }

  const items = [], sources = [];
  for (const [number, source] of sourceManifest.items.entries()) {
    const audioPath = path.join(sourceRoot, source.audio);
    const audio = await readFile(audioPath), facts = readFloatWav(audio);
    items.push(exhaustiveSpaceItem(source, number, audio, facts));
    sources.push({ source_id: source.id, source_audio: audioPath });
  }
  const packId = 'hypha-space-exhaustive-development-20260911-01';
  const identity = items.map(({ wave, ...item }) => item);
  const evidenceIdentity = {
    schema: 'hypha.space-exhaustive.evidence.v1', pack_id: packId,
    source_pack_id: sourceManifest.pack_id,
    source_manifest_file_sha256: hash(sourceManifestBytes),
    items: items.map((item, number) => ({ id: item.id, source_id: sources[number].source_id,
      audio_sha256: item.audio_sha256 })),
  };
  const evaluationEvidenceSha256 = hash(JSON.stringify(evidenceIdentity));
  const manifestSha256 = hash(JSON.stringify({ protocol, annotationScope, identity,
    evaluation_evidence_sha256: evaluationEvidenceSha256 }));
  const pack = {
    schema: 'hypha.pilot-review.v1', pack_id: packId, protocol,
    annotation_scope: annotationScope, candidate_exposure: false, product_qualified: false,
    evaluation_evidence_sha256: evaluationEvidenceSha256, manifest_sha256: manifestSha256,
    ui: {
      document_title: 'Hypha｜SPACE全区間判定 01',
      eyebrow: 'KIRIN HYPHA / SPACE EXHAUSTIVE STUDY 01',
      heading: '30秒を聴いて、追える減衰をすべて残す。',
      subtitle: 'SPACE 6件。解析候補を表示しない開発用の網羅判定です。途中で閉じて続きから再開できます。',
      about_primary: '正解当てではありません。解析候補、閾値、先行回答は表示せず、聴感で追える全区間だけを記録します。',
      about_secondary: '各30秒の95%以上を実際に再生すると回答を確定できます。回答は開発集の規則設計に使い、未使用holdoutの合格を意味しません。',
      footer: 'LOCAL ONLY / 音源・回答は送信しません。回答JSONとaudioフォルダを一緒に保持してください。',
    },
    items,
  };
  const assets = {};
  for (const name of ['shell.html', 'review.css', 'model.js', 'wave.js', 'app.js'])
    assets[name] = await readFile(path.join(here, name), 'utf8');
  let html = assets['shell.html'].replace('/* REVIEW_STYLE */', assets['review.css']);
  html = html.replace('<!-- REVIEW_DATA -->', `<script id="review-data" type="application/json">${inlineJson(pack)}</script>`);
  html = html.replace('/* REVIEW_SCRIPTS */', [assets['model.js'], assets['wave.js'], assets['app.js']].join('\n'));

  await mkdir(reviewRoot, { recursive: false, mode: 0o700 });
  await mkdir(path.join(reviewRoot, 'audio'), { mode: 0o700 });
  for (const [number, item] of items.entries()) {
    const destination = path.join(reviewRoot, item.audio);
    await copyFile(sources[number].source_audio, destination, constants.COPYFILE_EXCL);
    if (hash(await readFile(destination)) !== item.audio_sha256) throw new Error('Copy hash mismatch');
  }
  await writeFile(path.join(reviewRoot, 'index.html'), html, { flag: 'wx', mode: 0o600 });
  await writeFile(path.join(reviewRoot, 'manifest.json'), `${JSON.stringify({ ...pack, items: identity,
    html_sha256: hash(html) }, null, 2)}\n`, { flag: 'wx', mode: 0o600 });
  await writeFile(path.join(reviewRoot, 'はじめに.txt'),
    'Hypha SPACE 全区間判定 01\n\nindex.htmlをChromeで開いてください。ネット接続は不要です。\n'
    + '各30秒を聴き、追える減衰区間があればすべて指定してください。\n'
    + '途中でも回答JSONを保存できます。完了後のJSONをCodexへ添付してください。\n'
    + '音源は私用の検証用です。audioフォルダをindex.htmlと一緒に保持し、再配布しないでください。\n',
    { flag: 'wx', mode: 0o600 });
  await mkdir(evidenceRoot, { recursive: false, mode: 0o700 });
  await writeFile(path.join(evidenceRoot, 'evidence-manifest.json'),
    `${JSON.stringify({ ...evidenceIdentity, manifest_sha256: manifestSha256,
      evaluation_evidence_sha256: evaluationEvidenceSha256 }, null, 2)}\n`,
    { flag: 'wx', mode: 0o600 });
  return {
    output: reviewRoot, evidence_output: evidenceRoot, items: items.length,
    audio_bytes: items.reduce((sum, item) => sum + item.bytes, 0),
    html_bytes: (await stat(path.join(reviewRoot, 'index.html'))).size,
    manifest_sha256: manifestSha256,
    sources: items.map(({ id, rate, channels, frames, peak, rms }) =>
      ({ id, rate, channels, frames, peak, rms })),
  };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  if (process.argv.length !== 5)
    throw new Error('Usage: build_space_exhaustive_pack.mjs SOURCE_PACK NEW_REVIEW_DIRECTORY NEW_PRIVATE_EVIDENCE_DIRECTORY');
  process.umask(0o077);
  console.log(JSON.stringify(await buildSpaceExhaustivePack(
    path.resolve(process.argv[2]), path.resolve(process.argv[3]), path.resolve(process.argv[4])), null, 2));
}
