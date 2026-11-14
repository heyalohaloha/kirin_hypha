// Release-specific rows and owner decisions; frozen expectations remain unchanged.
import fs from 'node:fs';
import { HOST_TESTS, DISTRIBUTION_TESTS, POST_TESTS, SAFETY_EXPECTED, readJson, fileFact, checkFacts, digest, Checkpoint } from './hypha_release_contract.mjs';
import { CONDITIONAL_HOST_TESTS } from './release_scope.mjs';

export const FIX_ROWS_SCHEMA = 'hypha-release-fix-rows-v2';
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

// The promises of a release are the list items of the English release notes (before the first
// `---`): `-`, `*`, `+` or numbered, at any indent, with continuation lines joined.
export function notePromises(file) {
  if (!file || !fs.statSync(file, { throwIfNoEntry: false })?.isFile()) return null;
  const english = fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '').split(/^ {0,3}(?:-{3,}|\*{3,}|_{3,})\s*$/m)[0];
  const promises = [];
  let open = false;
  for (const line of english.split(/\r?\n/)) {
    const item = line.match(/^\s*(?:[-*+]|\d+[.)])\s+(.+?)\s*$/);
    if (item) { promises.push(item[1]); open = true; }
    // Markdown continues an item on any following non-blank line (indented or not) until a blank line or heading.
    else if (open && /\S/.test(line) && !/^ {0,3}#{1,6}([ \t]|$)/.test(line)) promises[promises.length - 1] += ` ${line.trim()}`;
    else open = false;
  }
  return promises;
}

// Every promise needs a host row, and every row names the promise(s) it proves. A host
// row names the DAW, the format(s) and the chain it runs. Checked at --init and on every stage.
export function fixRows(file, candidate, reserved = HOST_TESTS, notes = '') {
  const spec = readJson(file);
  if (spec.schema !== FIX_ROWS_SCHEMA || spec.release !== candidate.version || !Array.isArray(spec.rows)) {
    throw new Error('Fix rows must name this release with the supported schema');
  }
  const promises = notePromises(notes);
  if (!promises) throw new Error('Fix rows need the release notes whose promises they prove');
  const seen = new Set([...HOST_TESTS, ...CONDITIONAL_HOST_TESTS, ...DISTRIBUTION_TESTS, ...POST_TESTS, ...reserved]);
  for (const row of spec.rows) {
    if (!/^[a-zA-Z0-9][a-zA-Z0-9._/-]{0,95}$/.test(row.id || '') || seen.has(row.id)
        || !['host', 'ci'].includes(row.kind) || row.depth !== 'deep'
        || !Array.isArray(row.hosts) || row.hosts.some(host => !text(host))
        || (row.kind === 'host' && (!row.hosts.length || !text(row.daw) || !text(row.chain))) || !text(row.oracle)
        || (row.kind === 'ci' && !Object.hasOwn(CI_ORACLES, row.oracle))
        || !row.expected || typeof row.expected !== 'object' || Array.isArray(row.expected)
        || Object.keys(row.expected).length === 0) {
      throw new Error('Fix row needs a unique id, kind, hosts, DAW and chain (host), deep oracle and expected values');
    }
    const names = row.kind === 'host' && Object.hasOwn(row, 'promises') ? row.promises : [row.promise];
    if ((Object.hasOwn(row, 'promises') && (row.kind !== 'host' || Object.hasOwn(row, 'promise')))
        || !Array.isArray(names) || !names.length || names.some(name => !text(name))
        || new Set(names).size !== names.length || names.some(name => !promises.includes(name))) {
      throw new Error(`Fix row ${row.id} names no promise of the release notes`);
    }
    seen.add(row.id);
  }
  const namesPromise = (row, promise) => row.promise === promise || row.promises?.includes(promise);
  const unproved = promises.find(promise => !spec.rows.some(row => namesPromise(row, promise)));
  if (unproved) throw new Error(`Release-note promise has no acceptance row: ${unproved}`);
  const missingHost = promises.find(promise => !spec.rows.some(row => row.kind === 'host' && namesPromise(row, promise)));
  if (missingHost) throw new Error(`Release-note promise has no real-host acceptance row: ${missingHost}`);
  return { rows: spec.rows, decision: fileFact(file) };
}

// Re-read on every stage and on --bind-publication: the current notes may not promise anything
// the bound rows do not prove (changing a promise needs new rows from --init).
export function checkFixRows(state) {
  if (!state.fixRows) {
    const promised = notePromises(state.inputs?.notes);
    if (promised?.length) throw new Error(`Release-note promise has no acceptance row: ${promised[0]}`);
    return;
  }
  checkFacts([state.fixRows.decision]);
  const actual = fixRows(state.fixRows.decision.path, state.candidate, state.requiredHostTests, state.inputs?.notes);
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
