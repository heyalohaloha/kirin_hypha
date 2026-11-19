// Actual DAW attestations are distinct from CI logs and from planned host names.
import fs from 'node:fs';
import crypto from 'node:crypto';
import { isDeepStrictEqual } from 'node:util';
import { LIGHT_FIX_ROWS_SCHEMA } from './hypha_release_light_policy.mjs';
const text = value => typeof value === 'string' && value.trim().length > 0;
export function validateLightHostRuns(state, reportRow, binding, tools) {
  if (state.fixRows?.schema !== LIGHT_FIX_ROWS_SCHEMA) return [];
  const plan = state.fixRows.rows.find(row => row.id === reportRow.id && row.kind === 'host');
  if (!plan) {
    if (reportRow.evidence.some(item => item.kind === 'host-run')) throw new Error('host-run needs a verified planned A4 row');
    return [];
  }
  const runs = reportRow.evidence.filter(item => item.kind === 'host-run');
  if (runs.length !== plan.hosts.length || new Set(runs.map(r => r.host)).size !== runs.length) {
    throw new Error('Light host row needs one actual DAW run per planned format');
  }
  const observationSets = [];
  const facts = [], ids = new Set(), hashes = new Set();
  for (const host of plan.hosts) {
    const ref = runs.find(run => run.host === host);
    if (!ref || !text(ref.path) || !/^[0-9a-f]{64}$/.test(ref.sha256 || '')) {
      throw new Error('Light host run reference needs host, path and sha256');
    }
    const fact = tools.fileFact(tools.resolveInput(state, ref.path));
    if (fact.sha256 !== ref.sha256 || hashes.has(fact.sha256)) throw new Error('Light host run hash mismatch or reuse');
    const bytes = fs.readFileSync(fact.path);
    if (crypto.createHash('sha256').update(bytes).digest('hex') !== ref.sha256) throw new Error('Light host run bytes changed');
    const run = JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, ''));
    if (tools.fileFact(fact.path).sha256 !== fact.sha256 || run.schema !== 'hypha-real-daw-run-v1' || run.execution !== 'real-daw'
        || run.result !== 'PASS' || !text(run.runId) || ids.has(run.runId)
        || !isDeepStrictEqual(run.candidate, state.candidate) || run.bindingSha256 !== binding
        || run.rowId !== plan.id || run.daw !== plan.daw || run.os !== plan.os || run.host !== host
        || run.chain !== plan.chain || !text(run.observer) || !text(run.oracle)
        || !Number.isSafeInteger(run.processId) || run.processId < 1 || !text(run.hostVersion)
        || !Number.isFinite(Date.parse(run.startedAt)) || !Number.isFinite(Date.parse(run.finishedAt))
        || Date.parse(run.startedAt) >= Date.parse(run.finishedAt)
        || !isDeepStrictEqual(run.expected, plan.expected)
        || !tools.compareExpected(plan.expected, run.measured)) {
      throw new Error('Light real DAW run must bind candidate, row, DAW, OS, format and observed expectations');
    }
    // Retain a separately hashed actual-host observation, not merely a CI log in an attachment.
    if (!Array.isArray(run.observations) || !run.observations.length) throw new Error('Light run needs retained host observations');
    const observedHashes = new Set();
    for (const observation of run.observations) {
      if (!['screen', 'audio', 'host-log'].includes(observation.kind) || !text(observation.path)
          || !/^[0-9a-f]{64}$/.test(observation.sha256 || '')) throw new Error('Invalid actual-host observation');
      const proof = tools.fileFact(tools.resolveInput(state, observation.path));
      if (proof.sha256 !== observation.sha256) throw new Error('Actual-host observation hash mismatch');
      observedHashes.add(proof.sha256); facts.push(proof);
    }
    observationSets.push(observedHashes);
    ids.add(run.runId); hashes.add(fact.sha256); facts.push(fact);
  }
  for (const [index, observed] of observationSets.entries()) {
    const other = new Set(observationSets.filter((_, i) => i !== index).flatMap(set => [...set]));
    if (![...observed].some(hash => !other.has(hash))) throw new Error('Light format run needs a distinct retained observation');
  }
  return facts;
}
