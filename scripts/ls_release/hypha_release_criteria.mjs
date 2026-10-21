// Release-specific rows and owner decisions; frozen expectations remain unchanged.
import { HOST_TESTS, DISTRIBUTION_TESTS, POST_TESTS, SAFETY_EXPECTED, readJson, fileFact, checkFacts, digest, Checkpoint } from './hypha_release_contract.mjs';

export const FIX_ROWS_SCHEMA = 'hypha-release-fix-rows-v1';
export const OWNER_DECISION_SCHEMA = 'hypha-owner-acceptance-decision-v1';
export const OWNER_DEFERRED = 'OWNER_DEFERRED';
const shippingStep = 'Test shipping source contract';
export const CI_ORACLES = {
  'pair-recovery-parity': { checks: [
    { job: 'release source rust (macos)', step: shippingStep },
    { job: 'release source ffi (macos)', step: shippingStep }],
    tests: ['measurement_pair_recovery', 'parity', 'pairing_candidates'] },
  'editor-surface-render': { checks: [
    ...[1, 2, 3, 4].map(n => ({ job: `release source native${n} (macos)`, step: shippingStep }))],
    tests: ['kirin_editor_surface_product', 'kirin_ui_render_contract'] },
  'editor-reachability': { checks: [
    ...[1, 2, 3, 4].map(n => ({ job: `release source native${n} (macos)`, step: shippingStep }))],
    tests: ['kirin_editor_surface_product'] },
  'Windows-installer-lifecycle': { checks: [
    { job: 'windows VST3 preflight', step: 'Verify Windows installer install, same-version reinstall, signatures, and uninstall' },
    { job: 'windows VST3 preflight', step: 'Validate local PRE/POST Blind and complete product flow' },
    { job: 'windows VST3 preflight', step: 'Validate live PRE/POST compare correspondence' },
    { job: 'windows VST3 preflight', step: 'Validate REAPER LISTEN contracts and product flow' }],
    tests: ['scripts/windows/verify-installer.ps1', 'kirin_editor_surface_product',
      'kirin_live_compare_correspondence', 'kirin_reaper_listen_mono_product', 'kirin_reaper_listen_stereo_product'] },
};
const text = value => typeof value === 'string' && value.trim().length > 0;
export const hostTestIds = state => [...state.requiredHostTests,
  ...(state.fixRows?.rows || []).filter(row => row.kind === 'host').map(row => row.id)];

export function fixRows(file, candidate, reserved = HOST_TESTS) {
  const spec = readJson(file);
  if (spec.schema !== FIX_ROWS_SCHEMA || spec.release !== candidate.version || !Array.isArray(spec.rows)) {
    throw new Error('Fix rows must name this release with the supported schema');
  }
  const seen = new Set([...HOST_TESTS, ...DISTRIBUTION_TESTS, ...POST_TESTS, ...reserved]);
  for (const row of spec.rows) {
    if (!/^[a-zA-Z0-9][a-zA-Z0-9._/-]{0,95}$/.test(row.id || '') || seen.has(row.id)
        || !['host', 'ci'].includes(row.kind) || row.depth !== 'deep'
        || !Array.isArray(row.hosts) || row.hosts.some(host => !text(host))
        || (row.kind === 'host' && !row.hosts.length) || !text(row.oracle)
        || (row.kind === 'ci' && !Object.hasOwn(CI_ORACLES, row.oracle))
        || !row.expected || typeof row.expected !== 'object' || Array.isArray(row.expected)
        || Object.keys(row.expected).length === 0) {
      throw new Error('Fix row needs a unique id, kind, hosts, deep oracle and expected values');
    }
    seen.add(row.id);
  }
  return { rows: spec.rows, decision: fileFact(file) };
}

export function checkFixRows(state) {
  if (!state.fixRows) return;
  checkFacts([state.fixRows.decision]);
  const actual = fixRows(state.fixRows.decision.path, state.candidate, state.requiredHostTests);
  if (digest(actual) !== digest(state.fixRows)) throw new Error('Bound fix rows changed');
  for (const row of actual.rows) {
    if (digest(state.expected[row.id]) !== digest(row.expected)) throw new Error('Fix row expected values changed');
  }
}

function ownerDecision(file, state) {
  const spec = readJson(file);
  const ids = new Set([...hostTestIds(state), ...(state.fixRows?.rows || []).map(row => row.id)]);
  if (!state.freeze?.sha256 || spec.schema !== OWNER_DECISION_SCHEMA
      || spec.release !== state.candidate.version || spec.candidateId !== state.candidate.id
      || !Array.isArray(spec.rows) || spec.rows.length === 0) {
    throw new Error('Owner decision must name this frozen release and candidate');
  }
  const seen = new Set();
  for (const row of spec.rows) {
    if (!ids.has(row.id) || Object.hasOwn(SAFETY_EXPECTED, row.id) || seen.has(row.id)
        || !text(row.reason) || !text(row.resumeCondition) || !text(row.releaseNote)) {
      throw new Error('Owner deferred row needs a unique eligible id, reason, resume condition and release note');
    }
    seen.add(row.id);
  }
  return { candidateId: state.candidate.id, bindingSha256: state.freeze.sha256,
    rows: spec.rows, decision: fileFact(file) };
}

export function checkOwnerDecisions(state) {
  const seen = new Set();
  for (const bound of state.ownerDecisions || []) {
    checkFacts([bound.decision]);
    if (digest(ownerDecision(bound.decision.path, state)) !== digest(bound)) throw new Error('Bound owner decision changed');
    for (const row of bound.rows) {
      if (seen.has(row.id)) throw new Error('Owner decision names a row twice');
      seen.add(row.id);
    }
  }
}

export function bindOwnerDecisions(state, files) {
  checkOwnerDecisions(state);
  const decisions = [...(state.ownerDecisions || [])];
  for (const file of files) {
    const bound = ownerDecision(file, state);
    if (!decisions.some(old => digest(old) === digest(bound))) decisions.push(bound);
  }
  const proposed = { ...state, ownerDecisions: decisions };
  checkOwnerDecisions(proposed);
  state.ownerDecisions = decisions;
}

export function deferredRow(state, id) {
  checkOwnerDecisions(state);
  const decision = state.ownerDecisions?.find(bound => bound.rows.some(row => row.id === id));
  return decision ? { ...decision.rows.find(row => row.id === id), decision: decision.decision } : null;
}

export function ciFixRows(state) {
  return (state.fixRows?.rows || []).filter(row => row.kind === 'ci').map(row => {
    const deferred = deferredRow(state, row.id);
    if (deferred) return { id: row.id, result: OWNER_DEFERRED, ...deferred };
    if (digest(row.expected) !== digest({ passed: true })) {
      throw new Checkpoint('CI quantitative fix row needs measured oracle evidence');
    }
    const oracle = state.stages.ci?.evidence?.oracles?.find(oracle => oracle.name === row.oracle);
    if (!oracle || !oracle.checks.length || oracle.checks.some(check => check.conclusion !== 'success')) {
      throw new Error('CI fix row has no verified oracle evidence');
    }
    return { id: row.id, result: 'PASS', oracle: row.oracle, expected: row.expected };
  });
}
