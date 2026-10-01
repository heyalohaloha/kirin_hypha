// Candidate-bound evidence. Reports are attestations backed by retained files, never auto-PASS.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { buildTreeManifest } from './aax_submission_archive.mjs';

export const SCHEMA = 'kirin-hypha-release-pipeline-v1';
export const HOST_TESTS = [
  ...['arm64', 'x86_64'].flatMap(cpu => ['AU', 'VST3', 'AAX'].map(f => `macOS/${cpu}/${f}`)),
  'Windows/x64/VST3', 'Windows/x64/AAX',
  ...['PRE-alone', 'POST-alone', 'same-pair', 'wrong-pair', 'multiple-pairs', 'duplicate',
    'peer-loss', 'peer-return', 'reopen-order', 'processor-recreation'].map(t => `pair/${t}`),
  'normal-bit-identical', 'LISTEN-clock-PDC', 'Live-Blind-anonymity', 'Exact-4-S-samples',
  'state-automation-recall', 'rate-buffer-layout-matrix', 'stress-UI', 'Windows-installer-lifecycle',
];
export const DISTRIBUTION_TESTS = ['A4-to-A5-payload-continuity', 'macOS-clean-install',
  'Windows-clean-install', 'all-signatures', 'rollback-preparation'];
export const POST_TESTS = ['user-download-install', 'macOS-AU-VST3-AAX-scan',
  'Windows-VST3-AAX-scan', 'Pro-Tools-insert-pair-normal-audio', 'session-reopen'];
export const SAFETY_EXPECTED = {
  'normal-bit-identical': { mismatchedSamples: 0, latencySamples: 0, nonFiniteSamples: 0 },
  'LISTEN-clock-PDC': { wrongSourceFrames: 0, unverifiedFramesOutputAsPRE: 0, unsafeGainRestores: 0 },
  'Live-Blind-anonymity': { substitutionsCountedAsRequestedSource: 0, identityLeaks: 0 },
  'Exact-4-S-samples': { frameLengthError: 0, unexpectedRestores: 0 },
};
export const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
export const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
export const digest = value => hash(JSON.stringify(value));
export const resolveInput = (state, value) => path.resolve(state.root, value || '');

export function atomicJson(file, value) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const temporary = `${file}.${process.pid}.tmp`;
  fs.writeFileSync(temporary, `${JSON.stringify(value, null, 2)}\n`, { flag: 'wx', mode: 0o600 });
  fs.renameSync(temporary, file);
}

export function safeStatePath(root, file) {
  const lexicalRoot = path.resolve(root);
  root = fs.realpathSync(root);
  const base = path.join(root, 'release_state');
  let resolved = path.resolve(lexicalRoot, file);
  if (resolved.startsWith(`${lexicalRoot}${path.sep}`)) resolved = path.resolve(root, path.relative(lexicalRoot, resolved));
  if (!resolved.startsWith(`${base}${path.sep}`) || !resolved.endsWith('.json')) {
    throw new Error('Pipeline state must be an ignored release_state/*.json path');
  }
  for (let current = resolved; current !== root && current !== path.dirname(current);
    current = path.dirname(current)) {
    if (fs.lstatSync(current, { throwIfNoEntry: false })?.isSymbolicLink()) {
      throw new Error('Pipeline state path must not traverse symlinks');
    }
  }
  return resolved;
}

export function fileFact(file) {
  const stat = fs.lstatSync(file);
  if (!stat.isFile() || stat.size === 0) throw new Error('Required artifact is not a nonempty regular file');
  return { path: path.resolve(file), bytes: stat.size, sha256: hash(fs.readFileSync(file)) };
}

export function treeFact(directory) {
  if (fs.lstatSync(directory).isSymbolicLink()) throw new Error('Payload root must not be a symlink');
  return { path: path.resolve(directory), treeSha256: digest(buildTreeManifest(directory)) };
}

export function checkFacts(facts) {
  if (!Array.isArray(facts) || facts.length === 0) throw new Error('Stage has no retained artifact evidence');
  for (const expected of facts) {
    const actual = expected.treeSha256 ? treeFact(expected.path) : fileFact(expected.path);
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
      throw new Error('Retained artifact changed; new candidate/affected gates required, not an automatic rebuild');
    }
  }
}

export function reportTemplate(state, gate, binding, ids) {
  return { schema: 'kirin-hypha-release-evidence-v1', candidate: state.candidate,
    gate, bindingSha256: binding, tests: ids.map(id => ({ id, result: 'PENDING',
      expected: state.expected?.[id] || { passed: true }, measured: null, oracle: '', evidence: [] })) };
}

function compareExpected(expected, measured) {
  if (expected && typeof expected === 'object') {
    return measured && typeof measured === 'object'
      && Object.entries(expected).every(([key, value]) => compareExpected(value, measured[key]));
  }
  return expected === measured;
}

export function validateReport(state, reportPath, gate, binding, ids) {
  if (!reportPath || !fs.existsSync(resolveInput(state, reportPath))) {
    throw new Checkpoint(`${gate}: retained exact-candidate report is required`);
  }
  const file = resolveInput(state, reportPath);
  const report = readJson(file);
  if (report.schema !== 'kirin-hypha-release-evidence-v1' || report.gate !== gate
      || digest(report.candidate) !== digest(state.candidate) || report.bindingSha256 !== binding) {
    throw new Error(`${gate}: report candidate/payload binding mismatch`);
  }
  const facts = [fileFact(file)];
  if (!Array.isArray(report.tests) || new Set(report.tests.map(t => t.id)).size !== report.tests.length) {
    throw new Error(`${gate}: missing or duplicate test records`);
  }
  for (const id of ids) {
    const row = report.tests.find(t => t.id === id);
    if (row?.result === 'FAIL') throw new Error(`${gate}: ${id} has a confirmed acceptance failure`);
    if (!row || row.result !== 'PASS') throw new Checkpoint(`${gate}: ${id} is not PASS (SKIP cannot compensate)`);
    if (row.expected == null || row.measured == null || !row.oracle?.trim()
        || !Array.isArray(row.evidence) || row.evidence.length === 0) {
      throw new Error(`${gate}: ${id} needs expected/measured, independent oracle and retained evidence`);
    }
    if (state.expected?.[id] && digest(row.expected) !== digest(state.expected[id])) {
      throw new Error(`${gate}: ${id} expected values differ from the frozen contract`);
    }
    if (!compareExpected(row.expected, row.measured)) throw new Error(`${gate}: ${id} measured values fail expected conditions`);
    for (const evidence of row.evidence) {
      const actual = fileFact(resolveInput(state, evidence.path));
      if (actual.sha256 !== evidence.sha256) throw new Error(`${gate}: evidence hash mismatch`);
      facts.push(actual);
    }
  }
  return facts;
}

export class Checkpoint extends Error { constructor(message) { super(message); this.name = 'Checkpoint'; } }

export function assertState(state) {
  if (state.schema !== SCHEMA || !/^[0-9a-f]{40}$/.test(state.candidate?.commit || '')
      || !/^B-\d+$/.test(state.candidate?.bNumber || '')
      || !/^\d+\.\d+\.\d+$/.test(state.candidate?.version || '')
      || !/^[a-zA-Z0-9._-]{1,96}$/.test(state.candidate?.id || '')
      || !/^[0-9a-f]{64}$/.test(state.source?.fingerprint || '')) throw new Error('Invalid candidate state');
  if (state.source.commit !== state.candidate.commit || state.source.bNumber !== state.candidate.bNumber) {
    throw new Error('Candidate identity differs from the retained source identity');
  }
  for (const id of HOST_TESTS) if (!state.requiredHostTests?.includes(id)) {
    throw new Error('Required host matrix cannot be reduced');
  }
  for (const [id, expected] of Object.entries(SAFETY_EXPECTED)) {
    if (digest(state.expected?.[id]) !== digest(expected)) throw new Error('Minimum quantitative safety criteria cannot be weakened');
  }
  if (!/^\d{4}-\d{2}-\d{2}$/.test(state.inputs?.date || '')) throw new Error('Release date is required');
}

export function authorization(state, options) {
  if (options.publishApproved !== state.candidate.id) {
    throw new Checkpoint('Public writes require --publish-approved with this exact candidate ID');
  }
}

export function readBudgetPolicy(root) {
  // Re-read at every push/tag/release mutation. Never modify the account budget or dispatch CI here.
  const common = path.join(process.env.HOME || '', '.codex', 'AGENTS.md');
  const files = [common, path.join(root, 'AGENTS.md')].filter(fs.existsSync);
  if (!files.some(file => /GitHub予算|GitHub budget/.test(fs.readFileSync(file, 'utf8')))) {
    throw new Error('Shared GitHub budget policy is unavailable; publication preflight required');
  }
  for (const file of files) fs.readFileSync(file, 'utf8');
}
