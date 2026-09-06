import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, mkdir, readFile, rm, unlink, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { evaluateReview, matchRepresentativeAttackPoints,
  reviewManifestIdentitySha } from './evaluate_review_answers.mjs';

const sha256 = value => createHash('sha256').update(value).digest('hex');

async function fixture() {
  const root = await mkdtemp(path.join(os.tmpdir(), 'hypha-review-evaluation-'));
  const pack = path.join(root, 'pack');
  const research = path.join(root, 'research');
  await mkdir(path.join(pack, 'audio'), { recursive: true });
  await mkdir(research);
  const html = Buffer.from('<!doctype html><title>review</title>');
  await writeFile(path.join(pack, 'index.html'), html);
  const definitions = [
    { id: 'space_development-01', mode: 'space', marks: [{ start: 200, end: 300 }],
      events: [{ onset_sample: 150, rejection: 'fewer_than_ten_points', facts: {} },
        { onset_sample: 250, rejection: 'insufficient_observed_fall', facts: {} }] },
    { id: 'attack_development-01', mode: 'attack', marks: [{ start: 400, end: 400 },
      { start: 700, end: 700 }], events: [{ sample: 405 }, { sample: 406 }, { sample: 750 }] },
  ];
  const items = [];
  const provenance = [];
  const answers = {};
  for (const definition of definitions) {
    const audio = Buffer.from(`audio-${definition.id}`);
    const audioHash = sha256(audio);
    const audioRelative = `audio/${definition.id}.wav`;
    await writeFile(path.join(pack, audioRelative), audio);
    const item = { id: definition.id, mode: definition.mode, title: definition.id,
      audio: audioRelative, audio_sha256: audioHash, rate: 1000, channels: 2,
      frames: 1000, bytes: audio.length, source_start_sample: 3000,
      preview: [100, 900] };
    items.push(item);
    const researchWav = path.join(research, `${definition.id}.wav`);
    const result = { item: { id: definition.id, artist_group_sha256: `artist-${definition.id}` },
      source_sha256: `source-${definition.id}`, source_unchanged_after_decode: true,
      facts: { input_sha256: audioHash, sample_rate: item.rate, frames: item.frames,
        mode: item.mode, events: definition.mode === 'attack' ? definition.events : [],
        space_observations: definition.mode === 'space' ? definition.events : [] } };
    const resultBytes = Buffer.from(`${JSON.stringify(result)}\n`);
    await writeFile(researchWav.replace(/\.wav$/, '.json'), resultBytes);
    provenance.push({ id: definition.id, artist_group_sha256: `artist-${definition.id}`,
      source_path: `/private/${definition.id}`, research_excerpt: researchWav,
      research_result_sha256: sha256(resultBytes), source_sha256: `source-${definition.id}`,
      audio_sha256: audioHash, byte_identity_to_research_excerpt: true });
    answers[definition.id] = { decision: 'present', confidence: '4', note: 'private note',
      marks: definition.marks, complete: true, played_seconds: 2,
      updated_at: '2026-09-07T00:00:00.000Z' };
  }
  const manifest = { schema: 'hypha.pilot-review.v1', pack_id: 'fixture-pack',
    protocol: 'development-first-pass-v1', candidate_exposure: false,
    product_qualified: false, items, html_sha256: sha256(html), provenance };
  manifest.manifest_sha256 = reviewManifestIdentitySha(manifest);
  const manifestPath = path.join(pack, 'manifest.json');
  await writeFile(manifestPath, `${JSON.stringify(manifest)}\n`);
  const answer = { schema: 'hypha.review.answers.v1', pack_id: manifest.pack_id,
    manifest_sha256: manifest.manifest_sha256, protocol: manifest.protocol,
    reviewer: 'private reviewer', candidate_exposure: false, volume: 0.35,
    active_id: items[0].id, answers };
  const answerPath = path.join(root, 'answers.json');
  const answerBytes = Buffer.from(`${JSON.stringify(answer)}\n`);
  await writeFile(answerPath, answerBytes);
  const sidecar = { answer_sha256: sha256(answerBytes), pack_id: manifest.pack_id,
    manifest_sha256: manifest.manifest_sha256,
    annotation_scope: 'pilot_representative_marks_not_exhaustive', corrections: [{
      item_id: items[0].id, raw_decision: 'present', effective_decision: 'none',
      raw_marks_retained: true, marks_excluded_from_positive_intervals: true,
      authority: 'fixture correction', corrected_confidence: null,
    }] };
  const sidecarPath = path.join(root, 'sidecar.json');
  await writeFile(sidecarPath, `${JSON.stringify(sidecar)}\n`);
  return { root, pack, manifestPath, answerPath, sidecarPath, answer, sidecar };
}

async function usingFixture(run) {
  const value = await fixture();
  try {
    await run(value);
  } finally {
    await rm(value.root, { recursive: true, force: true });
  }
}

test('ATTACK matching maximizes correspondence before minimizing total distance', () => {
  const result = matchRepresentativeAttackPoints(
    [{ start: 0 }, { start: 5 }], [4, 9], 1000, 5);
  assert.deepEqual(result.matches.map(match => [match.annotation_sample,
    match.candidate_sample]), [[0, 4], [5, 9]]);
  assert.deepEqual(result.unmatched_annotation_samples, []);
});

test('correction stays auditable and representative points produce diagnostic matches', () =>
  usingFixture(async paths => {
    const result = await evaluateReview({ ...paths, matchToleranceMs: 10 });
    assert.deepEqual(result.counts, { items: 2, complete: 2, corrections_applied: 1,
      attack_representative_points: 2, attack_matched_points: 1 });
    assert.equal(result.final_precision_recall_f1_permitted, false);
    assert.equal(result.items[0].raw_decision, 'present');
    assert.equal(result.items[0].effective_decision, 'none');
    assert.equal(result.items[0].raw_marks_retained, 1);
    assert.deepEqual(result.items[0].positive_intervals, []);
    assert.equal(result.items[1].correspondence.matches[0].delta_samples, 5);
    assert.deepEqual(result.items[1].correspondence.unmatched_annotation_samples, [700]);
    assert.equal(result.items[1].correspondence.nearby_unassigned_candidate_count, 1);
    assert.equal(result.items[1].correspondence.unmatched_candidates_are_false_positives, false);
    const serialized = JSON.stringify(result);
    assert(!serialized.includes('private reviewer'));
    assert(!serialized.includes('private note'));
    assert(!serialized.includes('/private/'));
  }));

test('partial answers remain partial instead of becoming negative evidence', () =>
  usingFixture(async paths => {
    paths.answer.answers['attack_development-01'].complete = false;
    const bytes = Buffer.from(`${JSON.stringify(paths.answer)}\n`);
    await writeFile(paths.answerPath, bytes);
    paths.sidecar.answer_sha256 = sha256(bytes);
    await writeFile(paths.sidecarPath, `${JSON.stringify(paths.sidecar)}\n`);
    const result = await evaluateReview(paths);
    assert.equal(result.counts.complete, 1);
    assert.equal(result.items[1].complete, false);
  }));

test('wrong hashes, duplicate corrections and missing audio fail closed', () =>
  usingFixture(async paths => {
    const originalSidecar = await readFile(paths.sidecarPath);
    paths.sidecar.answer_sha256 = '0'.repeat(64);
    await writeFile(paths.sidecarPath, `${JSON.stringify(paths.sidecar)}\n`);
    await assert.rejects(() => evaluateReview(paths), /identity mismatch/);
    await writeFile(paths.sidecarPath, originalSidecar);
    paths.sidecar = JSON.parse(originalSidecar);
    paths.sidecar.corrections.push({ ...paths.sidecar.corrections[0] });
    await writeFile(paths.sidecarPath, `${JSON.stringify(paths.sidecar)}\n`);
    await assert.rejects(() => evaluateReview(paths), /duplicate correction/);
    await writeFile(paths.sidecarPath, originalSidecar);
    await unlink(path.join(paths.pack, 'audio', 'attack_development-01.wav'));
    await assert.rejects(() => evaluateReview(paths), /ENOENT/);
  }));
