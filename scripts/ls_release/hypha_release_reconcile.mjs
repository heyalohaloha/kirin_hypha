// Reconcile retained outputs with read-only verifiers; never call a producer or publication action.
import fs from 'node:fs';
import path from 'node:path';
import { sourceSnapshot } from '../hypha_build_source.mjs';
import { assertUpdateBinding } from '../updates/update_key_binding.mjs';
import { verifyReleaseProvenance } from '../provenance/distribution_gate.mjs';
import { assetDecision, embeddedAssets, REGISTRY } from '../provenance/asset_gate.mjs';
import { requireFactoryIdentity } from '../provenance/factory_artifact.mjs';
import { acceptanceCriteria, assertState, atomicJson, checkFacts, digest, DISTRIBUTION_TESTS, POST_TESTS, fileFact, hash, hostTestIds, readJson, recordedEvidenceFacts, treeFact, validateReport } from './hypha_release_contract.mjs';
import { verifyCi, verifyMac, verifyWindows } from './hypha_release_local.mjs';
import { publicDownloadFacts, verifyPublicHp } from './hypha_release_hp.mjs';
import { verifyPublishedGithub } from './hypha_release_github.mjs';
import { ciFixRows } from './hypha_release_criteria.mjs';
import { verifyPreparedLs, verifyRetainedLs } from './hypha_release_reconcile_ls.mjs';

const eligible = new Set(['provenance-inputs', 'ci', 'macos-aax', 'windows', 'hosts', 'provenance', 'distribution', 'ls', 'github', 'hp', 'postrelease']);
const stages = ['provenance-inputs', 'ci', 'source-delivery', 'macos-au-vst3', 'macos-aax', 'windows', 'freeze', 'hosts', 'packages', 'provenance', 'distribution', 'ls', 'github', 'hp', 'postrelease'];

async function verifyPriorStages(state, stage, { run }) {
  if (stages.indexOf(stage) <= stages.indexOf('ci')) return null;
  if (!run) throw new Error('Read-only CI verification runner required for prior gate');
  return verifyCi(state, run);
}

async function verifyRetained(state, stage, { run, fetcher = fetch }) {
  if (stage === 'provenance-inputs') {
    requireFactoryIdentity(state);
    const assets = embeddedAssets(state.root), decision = assetDecision(state.root, assets);
    if (!decision.approved) throw new Error('Embedded materials are not approved');
    return { facts: [REGISTRY, 'juce_shell/CMakeLists.txt', ...assets].map(file => fileFact(path.join(state.root, file))), evidence: decision };
  }
  if (stage === 'ci') {
    if (!run) throw new Error('Read-only CI verification runner required');
    return { facts: [fileFact(path.join(state.root, '.github/workflows/ci.yml'))], evidence: await verifyCi(state, run) };
  }
  if (stage === 'macos-aax') {
    if (!run) throw new Error('Read-only verification runner required');
    return { facts: await verifyMac(state, run, true) };
  }
  if (stage === 'windows') {
    if (!run) throw new Error('Read-only verification runner required');
    return { facts: await verifyWindows(state, run) };
  }
  if (stage === 'hosts') {
    if (!state.freeze) throw new Error('Frozen candidate required for host acceptance');
    const rows = ciFixRows(state);
    const facts = validateReport(state, state.inputs.hostsReport, 'A4', state.freeze.sha256, hostTestIds(state));
    return { facts: [...facts, ...rows.filter(row => row.decision).map(row => row.decision)], evidence: { ciFixRows: rows } };
  }
  if (stage === 'provenance') return verifyReleaseProvenance(state);
  if (stage === 'distribution') {
    if (!run || !state.freeze) throw new Error('Frozen candidate and read-only runner required');
    const facts = [...await verifyMac(state, run), ...await verifyMac(state, run, true), ...await verifyWindows(state, run)];
    facts.push(await verifyPreparedLs(state, run));
    facts.push(...validateReport(state, state.inputs.distributionReport, 'A5', digest(state.stages.packages.facts), DISTRIBUTION_TESTS));
    return { facts };
  }
  if (stage === 'ls') return verifyRetainedLs(state, run);
  if (stage === 'github') {
    if (!run || !state.freeze) throw new Error('Frozen candidate and read-only runner required');
    return verifyPublishedGithub(state, run);
  }
  if (stage === 'postrelease') {
    if (!state.freeze) throw new Error('Frozen candidate required for postrelease');
    await verifyPublicHp(state, fetcher);
    const downloads = await publicDownloadFacts(state, [...state.stages.packages.facts, ...state.stages.provenance.publicFacts], fetcher);
    const facts = validateReport(state, state.inputs.postreleaseReport, 'A7', digest(state.stages.packages.facts), POST_TESTS);
    return { facts, evidence: { downloads } };
  }
  if (stage === 'hp') {
    if (!state.hpCommit || !state.freeze) throw new Error('HP candidate identity is incomplete');
    const facts = [...state.stages.packages.facts, ...state.stages.provenance.publicFacts];
    await verifyPublicHp(state, fetcher);
    const downloads = await publicDownloadFacts(state, facts, fetcher);
    return { facts, evidence: { hpCommit: state.hpCommit, downloads } };
  }
  throw new Error('Unsupported reconciliation stage');
}
function readCandidate(statePath, snapshot) {
  const bytes = fs.readFileSync(statePath), state = readJson(statePath);
  assertState(state);
  if (digest(snapshot(state.root)) !== digest(state.source)) throw new Error('Candidate source changed');
  if (state.freeze) {
    const { sha256, ...rest } = state.freeze;
    if (sha256 !== digest(rest) || state.freeze.inputs !== digest(state.inputs)
        || state.freeze.criteriaSha256 !== digest(acceptanceCriteria(state))) throw new Error('Frozen candidate changed');
    if (digest(state.freeze.source) !== digest(state.source)) throw new Error('Frozen source changed');
    assertUpdateBinding(state.freeze.updateCheck, state.inputs.updatePublicKey);
    if (!state.freeze.publicationNotes) throw new Error('Frozen publication notes are missing');
    checkFacts([state.freeze.publicationNotes]);
    checkFacts(state.freeze.payloads);
  }
  for (const saved of Object.values(state.stages)) if (saved.status === 'PASS' && saved.facts) checkFacts(saved.facts);
  return { state, profileSha256: hash(bytes) };
}
function verifyRetainedAttempts(state, saved, mappings = []) {
  if (!Array.isArray(mappings)) throw new Error('Retained evidence mappings must be an array');
  const seen = new Set(), used = new Set(), preserved = [];
  for (const mapping of mappings) {
    if (typeof mapping.originalPath !== 'string' || typeof mapping.retainedPath !== 'string' || seen.has(mapping.originalPath)) {
      throw new Error('Retained evidence mapping needs unique original and retained paths');
    }
    seen.add(mapping.originalPath);
  }
  for (const original of recordedEvidenceFacts(saved)) {
    try { checkFacts([original]); preserved.push({ original, retained: original }); continue; }
    catch { /* find an independently retained copy below */ }
    const mapping = mappings.find(entry => entry.originalPath === original.path);
    if (!mapping) throw new Error('Failed attempt evidence changed; provide the retained original');
    const retainedPath = path.resolve(state.root, mapping.retainedPath);
    const retained = original.treeSha256 ? treeFact(retainedPath) : fileFact(retainedPath);
    if (retained.sha256 !== original.sha256 || retained.bytes !== original.bytes
        || retained.treeSha256 !== original.treeSha256) throw new Error('Retained failed evidence differs');
    used.add(mapping.originalPath); preserved.push({ original, retained });
  }
  if (used.size !== mappings.length) throw new Error('Unused retained evidence mapping');
  return preserved;
}
function reconciliationStage(state) {
  const running = Object.entries(state.stages).filter(([, saved]) => saved?.status === 'RUNNING');
  if (!running.length) return state.blocker?.stage;
  if (running.length !== 1) throw new Error('Ambiguous interrupted stages require operator review');
  const stage = running[0][0], index = stages.indexOf(stage);
  if (index < 0 || stages.find(name => state.stages[name]?.status !== 'PASS') !== stage
      || (state.blocker && state.blocker.stage !== stage)
      || stages.slice(index + 1).some(name => state.stages[name])) {
    throw new Error('Ambiguous interrupted stage or inconsistent prerequisites');
  }
  return stage;
}

function requirePublicProvenance(state) {
  const source = state.stages['source-delivery']?.evidence;
  const ci = state.stages.ci;
  if (!source || Object.keys(source).sort().join(',') !== 'legalDir' || typeof source.legalDir !== 'string' || !source.legalDir
      || ci?.previousProfileReceipt || ci?.evidence?.previousProfileReceipt) {
    throw new Error('Factory provenance reconciliation is unsupported; unrecognized source evidence is also refused');
  }
}

export async function inspectRelease(statePath, { snapshot = sourceSnapshot } = {}) {
  const { state, profileSha256 } = readCandidate(statePath, snapshot);
  return { schema: 'hypha-release-inspection-v1', candidateId: state.candidate.id, profileSha256,
    releaseState: state.releaseState, blocker: state.blocker || null,
    stages: Object.fromEntries(Object.entries(state.stages).map(([name, value]) => [name, value.status])) };
}
export async function planReleaseReconciliation(statePath, inspection, { snapshot = sourceSnapshot, verify = verifyRetained, verifyPrerequisites = verifyPriorStages, run, fetcher, retained = [] } = {}) {
  const current = await inspectRelease(statePath, { snapshot });
  if (digest(current) !== digest(inspection)) throw new Error('Inspection changed; inspect again');
  const { state } = readCandidate(statePath, snapshot), stage = reconciliationStage(state);
  if (state.releaseState === 'RELEASE_INCIDENT' && !['ls', 'github', 'hp', 'postrelease'].includes(stage)) throw new Error('Publication incident requires a supported public read-back reconciliation');
  if (!eligible.has(stage) || !['FAIL', 'RUNNING', 'PENDING'].includes(state.stages[stage]?.status)) {
    throw new Error('No supported failed/interrupted retained payload to reconcile');
  }
  if (stage === 'provenance') requirePublicProvenance(state);
  const preserved = verifyRetainedAttempts(state, state.stages[stage], retained);
  for (const prior of stages.slice(0, stages.indexOf(stage))) {
    if (state.stages[prior]?.status !== 'PASS') throw new Error(`Prior stage ${prior} is not PASS`);
  }
  const priorCi = await verifyPrerequisites(state, stage, { run, fetcher });
  const result = await verify(state, stage, { run, fetcher });
  checkFacts(result.facts);
  if (result.publicFacts) checkFacts(result.publicFacts);
  const after = readCandidate(statePath, snapshot);
  if (after.profileSha256 !== current.profileSha256
      || digest(verifyRetainedAttempts(after.state, after.state.stages[stage], retained)) !== digest(preserved)) {
    throw new Error('Profile or retained evidence changed during reinspection');
  }
  const plan = { schema: 'hypha-release-reconciliation-plan-v1', inspection: current, stage, result, preserved, priorCi };
  return { ...plan, sha256: digest(plan) };
}
export async function applyReleaseReconciliation(statePath, plan, expectedHash, { snapshot = sourceSnapshot, verify = verifyRetained, verifyPrerequisites = verifyPriorStages, run, fetcher } = {}) {
  const { sha256, ...body } = plan || {};
  if (!/^[0-9a-f]{64}$/.test(expectedHash || '') || sha256 !== expectedHash || digest(body) !== sha256) {
    throw new Error('Reconciliation plan hash mismatch');
  }
  const lock = `${statePath}.lock`, fd = fs.openSync(lock, 'wx');
  try {
    if (plan.stage === 'provenance') requirePublicProvenance(readJson(statePath));
    const current = await inspectRelease(statePath, { snapshot });
    if (digest(current) !== digest(plan.inspection)) throw new Error('Profile changed since plan');
    const retained = (plan.preserved || []).filter(item => item.original.path !== item.retained.path)
      .map(item => ({ originalPath: item.original.path, retainedPath: item.retained.path }));
    const fresh = await planReleaseReconciliation(statePath, current, { snapshot, verify, verifyPrerequisites, run, fetcher, retained });
    if (digest(fresh) !== digest(plan)) throw new Error('Reinspection changed evidence since plan');
    const oldBytes = fs.readFileSync(statePath), state = readJson(statePath);
    if (hash(oldBytes) !== plan.inspection.profileSha256) throw new Error('Profile changed during reinspection');
    const backup = `${statePath}.reconcile-${plan.inspection.profileSha256}.json`;
    if (fs.existsSync(backup)) {
      if (hash(fs.readFileSync(backup)) !== plan.inspection.profileSha256) throw new Error('Existing profile backup differs');
    } else fs.writeFileSync(backup, oldBytes, { flag: 'wx', mode: 0o600 });
    let result = plan.result;
    if (plan.stage === 'ls') {
      const checks = `${statePath}.reconcile-${sha256}.ls-api.json`;
      const content = `${JSON.stringify(plan.result.evidence.apiChecks, null, 2)}\n`;
      if (fs.existsSync(checks)) {
        if (fileFact(checks).sha256 !== hash(content)) throw new Error('Existing LS evidence differs');
      } else fs.writeFileSync(checks, content, { flag: 'wx', mode: 0o600 });
      const apiFact = fileFact(checks);
      result = { ...result, facts: [...result.facts, apiFact], evidence: { ...result.evidence, apiChecks: apiFact } };
    }
    const { previous = [], ...attempt } = state.stages[plan.stage];
    state.stages[plan.stage] = { status: 'PASS', completedAt: new Date().toISOString(),
      previous: [...previous, attempt], facts: result.facts,
      ...(result.evidence ? { evidence: result.evidence } : {}),
      ...(result.publicFacts ? { publicFacts: result.publicFacts } : {}),
      ...(result.aliases ? { aliases: result.aliases } : {}),
      reconciliation: { planSha256: sha256, profile: fileFact(backup), preserved: plan.preserved, priorCi: plan.priorCi } };
    if (plan.stage === 'hosts') state.releaseState = 'HOST_ACCEPTED';
    if (plan.stage === 'distribution' || plan.stage === 'ls') state.releaseState = 'RELEASE_READY';
    if (plan.stage === 'github') { state.githubRelease.status = 'published'; state.releaseState = 'RELEASE_READY'; }
    if (plan.stage === 'hp') state.releaseState = 'RELEASED';
    if (plan.stage === 'postrelease') state.releaseState = 'RELEASE_COMPLETE';
    delete state.blocker;
    assertState(state);
    atomicJson(statePath, state);
    assertState(state);
    return state;
  } finally { fs.closeSync(fd); fs.unlinkSync(lock); }
}
