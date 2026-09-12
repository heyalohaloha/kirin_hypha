#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import vm from 'node:vm';

import { evidenceSchema, evaluationEvidenceIdentitySha } from './evidence_contract.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const sha256 = value => createHash('sha256').update(value).digest('hex');
const coordinateRule = 'hypha.review.native-sample-boundary-half-open.v1';

let reviewModel;
async function model() {
  if (!reviewModel) {
    const context = vm.createContext({});
    vm.runInContext(await readFile(path.join(here, 'model.js'), 'utf8'), context);
    reviewModel = context.HyphaReviewModel;
  }
  return reviewModel;
}

function parse(bytes, label) {
  try {
    return JSON.parse(bytes);
  } catch {
    throw new Error(`Invalid ${label} JSON`);
  }
}

function exactPath(root, relative, label = 'Manifest audio') {
  const absoluteRoot = path.resolve(root);
  const absolute = path.resolve(absoluteRoot, relative);
  if (absolute !== absoluteRoot && !absolute.startsWith(`${absoluteRoot}${path.sep}`)) {
    throw new Error(`${label} path escapes its directory`);
  }
  return absolute;
}

export function reviewManifestIdentitySha(manifest) {
  const identity = { protocol: manifest.schema, identity: manifest.items };
  if (manifest.evaluation_evidence_sha256 !== undefined) {
    identity.evaluation_evidence_sha256 = manifest.evaluation_evidence_sha256;
  }
  return sha256(JSON.stringify(identity));
}

function applyCorrections(answers, items, sidecar) {
  if (sidecar.annotation_scope !== 'pilot_representative_marks_not_exhaustive') {
    throw new Error('Unsupported annotation scope');
  }
  const known = new Set(items.map(item => item.id));
  const seen = new Set();
  const corrections = new Map();
  for (const correction of sidecar.corrections ?? []) {
    if (!known.has(correction.item_id) || seen.has(correction.item_id)
      || !['present', 'none', 'uncertain', 'unavailable'].includes(correction.effective_decision)
      || correction.raw_decision !== answers[correction.item_id].decision
      || correction.raw_marks_retained !== true
      || typeof correction.authority !== 'string' || !correction.authority
      || (correction.effective_decision === 'none'
        && correction.marks_excluded_from_positive_intervals !== true)
      || !(correction.corrected_confidence === null
        || ['1', '2', '3', '4', '5'].includes(correction.corrected_confidence))) {
      throw new Error('Invalid or duplicate correction authority');
    }
    seen.add(correction.item_id);
    corrections.set(correction.item_id, correction);
  }
  return corrections;
}

function preferredMatch(left, right) {
  if (left.count !== right.count) return left.count > right.count ? left : right;
  if (left.cost !== right.cost) return left.cost < right.cost ? left : right;
  return left;
}

export function matchRepresentativeAttackPoints(marks, candidates, rate, toleranceMs) {
  const toleranceSamples = Math.round(rate * toleranceMs / 1000);
  const sortedMarks = marks.map((mark, index) => ({ ...mark, originalIndex: index }))
    .sort((left, right) => left.start - right.start || left.originalIndex - right.originalIndex);
  const sortedCandidates = candidates.map((sample, index) => ({ sample, originalIndex: index }))
    .sort((left, right) => left.sample - right.sample || left.originalIndex - right.originalIndex);
  const empty = { count: 0, cost: 0, pairs: [] };
  const table = Array.from({ length: sortedMarks.length + 1 }, () =>
    Array(sortedCandidates.length + 1).fill(empty));
  for (let markIndex = 1; markIndex <= sortedMarks.length; markIndex += 1) {
    for (let candidateIndex = 1; candidateIndex <= sortedCandidates.length;
      candidateIndex += 1) {
      let best = preferredMatch(
        table[markIndex - 1][candidateIndex], table[markIndex][candidateIndex - 1]);
      const mark = sortedMarks[markIndex - 1];
      const candidate = sortedCandidates[candidateIndex - 1];
      const delta = candidate.sample - mark.start;
      if (Math.abs(delta) <= toleranceSamples) {
        const previous = table[markIndex - 1][candidateIndex - 1];
        const matched = { count: previous.count + 1,
          cost: previous.cost + Math.abs(delta),
          pairs: [...previous.pairs, { mark, candidate, delta }] };
        best = preferredMatch(matched, best);
      }
      table[markIndex][candidateIndex] = best;
    }
  }
  const selected = table[sortedMarks.length][sortedCandidates.length].pairs;
  const usedMarks = new Set(selected.map(pair => pair.mark.originalIndex));
  const usedCandidates = new Set(selected.map(pair => pair.candidate.originalIndex));
  const nearbyCandidates = new Set();
  for (const mark of marks) {
    candidates.forEach((candidate, index) => {
      if (Math.abs(candidate - mark.start) <= toleranceSamples) nearbyCandidates.add(index);
    });
  }
  return { tolerance_ms: toleranceMs, tolerance_samples: toleranceSamples,
    matches: selected.map(pair => ({ annotation_sample: pair.mark.start,
      candidate_sample: pair.candidate.sample, delta_samples: pair.delta,
      delta_ms: pair.delta * 1000 / rate })),
    unmatched_annotation_samples: marks.filter((_, index) => !usedMarks.has(index)).map(mark => mark.start),
    nearby_unassigned_candidate_count: [...nearbyCandidates]
      .filter(index => !usedCandidates.has(index)).length,
    candidate_count_in_window: candidates.length,
    unmatched_candidates_are_false_positives: false };
}

function summarizeSpace(observations) {
  const rejection_counts = {};
  let accepted = 0;
  for (const observation of observations) {
    if (observation.rejection == null && observation.facts?.fit?.d20_seconds != null) accepted += 1;
    else {
      const reason = observation.rejection ?? 'd20_unavailable';
      rejection_counts[reason] = (rejection_counts[reason] ?? 0) + 1;
    }
  }
  return { observation_count_in_window: observations.length,
    accepted_d20_count_in_window: accepted, rejection_counts };
}

async function verifyResearchResult(item, evidence, evidenceRoot) {
  let resultPath;
  if (evidenceRoot) resultPath = exactPath(evidenceRoot, evidence.result, 'Evidence result');
  else if (typeof evidence.research_excerpt === 'string') {
    resultPath = evidence.research_excerpt.replace(/\.wav$/i, '.json');
  } else throw new Error(`Durable evaluation evidence required: ${item.id}`);
  let bytes;
  try {
    bytes = await readFile(resultPath);
  } catch (error) {
    if (!evidenceRoot && error?.code === 'ENOENT') {
      throw new Error(`Durable evaluation evidence required: ${item.id}`);
    }
    throw error;
  }
  if (sha256(bytes) !== evidence.research_result_sha256) {
    throw new Error(`Research result hash mismatch: ${item.id}`);
  }
  const result = parse(bytes, 'research result');
  if (result.item?.id !== item.id || result.item?.artist_group_sha256 !== evidence.artist_group_sha256
    || result.source_sha256 !== evidence.source_sha256 || result.facts?.input_sha256 !== item.audio_sha256
    || result.facts?.sample_rate !== item.rate || result.facts?.frames !== item.frames
    || result.facts?.mode !== item.mode || result.source_unchanged_after_decode !== true) {
    throw new Error(`Research result identity mismatch: ${item.id}`);
  }
  return result.facts;
}

export async function evaluateReview({ answerPath, manifestPath, sidecarPath,
  evidenceManifestPath, packDirectory = path.dirname(manifestPath), matchToleranceMs = 50 }) {
  const [answerBytes, manifestBytes, sidecarBytes, evidenceBytes] = await Promise.all([
    readFile(answerPath), readFile(manifestPath), readFile(sidecarPath),
    evidenceManifestPath ? readFile(evidenceManifestPath) : Promise.resolve(null),
  ]);
  const manifest = parse(manifestBytes, 'manifest');
  const sidecar = parse(sidecarBytes, 'correction sidecar');
  if (manifest.schema !== 'hypha.pilot-review.v1' || manifest.candidate_exposure !== false
    || manifest.product_qualified !== false || !Array.isArray(manifest.items)
    || reviewManifestIdentitySha(manifest) !== manifest.manifest_sha256) {
    throw new Error('Manifest identity mismatch');
  }
  if (new Set(manifest.items.map(item => item.id)).size !== manifest.items.length) {
    throw new Error('Duplicate manifest item ID');
  }
  if (sha256(answerBytes) !== sidecar.answer_sha256 || sidecar.pack_id !== manifest.pack_id
    || sidecar.manifest_sha256 !== manifest.manifest_sha256) {
    throw new Error('Answer or sidecar identity mismatch');
  }
  const answer = (await model()).validate(parse(answerBytes, 'answer'), manifest);
  const corrections = applyCorrections(answer.answers, manifest.items, sidecar);
  const evidenceManifest = evidenceBytes ? parse(evidenceBytes, 'evaluation evidence') : null;
  if (evidenceManifest && (evidenceManifest.schema !== evidenceSchema
    || evidenceManifest.pack_id !== manifest.pack_id
    || evidenceManifest.manifest_sha256 !== manifest.manifest_sha256
    || !Array.isArray(evidenceManifest.items)
    || evidenceManifest.items.length !== manifest.items.length
    || evaluationEvidenceIdentitySha(evidenceManifest) !== manifest.evaluation_evidence_sha256)) {
    throw new Error('Evaluation evidence identity mismatch');
  }
  const evidenceRows = evidenceManifest?.items ?? manifest.provenance;
  if (!Array.isArray(evidenceRows) || evidenceRows.length !== manifest.items.length) {
    throw new Error('Durable evaluation evidence required');
  }
  const evidenceById = new Map(evidenceRows.map(row => [row.id, row]));
  if (evidenceById.size !== manifest.items.length) {
    throw new Error('Duplicate evaluation evidence ID');
  }
  const evidenceRoot = evidenceManifestPath ? path.dirname(evidenceManifestPath) : null;
  const html = await readFile(exactPath(packDirectory, 'index.html'));
  if (sha256(html) !== manifest.html_sha256) throw new Error('Review HTML identity mismatch');

  const results = [];
  for (const item of manifest.items) {
    const evidence = evidenceById.get(item.id);
    if (!evidence || evidence.audio_sha256 !== item.audio_sha256
      || evidence.byte_identity_to_research_excerpt !== true) {
      throw new Error(`Missing or mismatched provenance: ${item.id}`);
    }
    const audio = await readFile(exactPath(packDirectory, item.audio));
    if (audio.length !== item.bytes || sha256(audio) !== item.audio_sha256) {
      throw new Error(`Audio identity mismatch: ${item.id}`);
    }
    const facts = await verifyResearchResult(item, evidence, evidenceRoot);
    const raw = answer.answers[item.id];
    const correction = corrections.get(item.id);
    const effectiveDecision = correction?.effective_decision ?? raw.decision;
    const effectiveConfidence = correction
      ? correction.corrected_confidence : raw.confidence || null;
    const base = { id: item.id, mode: item.mode, complete: raw.complete,
      raw_decision: raw.decision, effective_decision: effectiveDecision,
      effective_confidence: effectiveConfidence, correction_applied: Boolean(correction),
      coordinate_rule: coordinateRule };
    if (item.mode === 'attack') {
      const candidates = facts.events.map(event => event.sample)
        .filter(sample => sample >= item.preview[0] && sample < item.preview[1]);
      results.push({ ...base, representative_annotation: true,
        points: raw.marks.map(mark => ({ excerpt_sample: mark.start,
          source_sample: item.source_start_sample + mark.start })),
        correspondence: matchRepresentativeAttackPoints(
          raw.marks, candidates, item.rate, matchToleranceMs) });
    } else {
      const observations = facts.space_observations.filter(observation =>
        observation.onset_sample >= item.preview[0]
          && observation.onset_sample < item.preview[1]);
      const positiveIntervals = effectiveDecision === 'present'
        ? raw.marks.map(mark => ({ raw_start_sample: mark.start, raw_end_sample: mark.end,
          excerpt_start_sample: mark.start, excerpt_end_exclusive_sample: mark.end,
          source_start_sample: item.source_start_sample + mark.start,
          source_end_exclusive_sample: item.source_start_sample + mark.end })) : [];
      results.push({ ...base, raw_marks_retained: raw.marks.length,
        positive_intervals: positiveIntervals, detector: summarizeSpace(observations) });
    }
  }
  const attack = results.filter(result => result.mode === 'attack');
  return { schema: 'hypha.review.development-evaluation.v1', pack_id: manifest.pack_id,
    manifest_sha256: manifest.manifest_sha256, answer_sha256: sha256(answerBytes),
    annotation_scope: sidecar.annotation_scope, product_qualified: false,
    final_precision_recall_f1_permitted: false, coordinate_rule: coordinateRule,
    counts: { items: results.length, complete: results.filter(result => result.complete).length,
      corrections_applied: results.filter(result => result.correction_applied).length,
      attack_representative_points: attack.reduce((sum, result) => sum + result.points.length, 0),
      attack_matched_points: attack.reduce((sum, result) =>
        sum + result.correspondence.matches.length, 0) },
    items: results };
}

async function main() {
  if (![6, 7].includes(process.argv.length)) {
    throw new Error('Usage: evaluate_review_answers.mjs ANSWERS MANIFEST SIDECAR [EVIDENCE_MANIFEST] NEW_OUTPUT');
  }
  const [, , answerPath, manifestPath, sidecarPath, ...rest] = process.argv;
  const [evidencePath, outputPath] = rest.length === 2 ? rest : [undefined, rest[0]];
  const evaluation = await evaluateReview({ answerPath: path.resolve(answerPath),
    manifestPath: path.resolve(manifestPath), sidecarPath: path.resolve(sidecarPath),
    evidenceManifestPath: evidencePath ? path.resolve(evidencePath) : undefined });
  await writeFile(path.resolve(outputPath), `${JSON.stringify(evaluation, null, 2)}\n`,
    { flag: 'wx', mode: 0o600 });
  console.log(JSON.stringify(evaluation.counts));
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  await main();
}
