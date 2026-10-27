#!/usr/bin/env node
// Resumable coordinator: approved producers -> exact-candidate gates -> LS/GitHub/HP -> A7.
import fs from 'node:fs';
import path from 'node:path';
import childProcess from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { sourceSnapshot } from './hypha_build_source.mjs';
import { requireCleanReleaseSource } from './ls_release/release_source_identity.mjs';
import { SCHEMA, HOST_TESTS, SAFETY_EXPECTED, DISTRIBUTION_TESTS, POST_TESTS, assertState, safeStatePath,
  atomicJson, digest, checkFacts, fileFact, treeFact, readJson, resolveInput, validateReport,
  reportTemplate, authorization, Checkpoint, ownerOverride, acceptanceCriteria, fixRows, hostTestIds } from './ls_release/hypha_release_contract.mjs';
import { bindOwnerDecisions, checkOwnerDecisions, checkFixRows, ciFixRows } from './ls_release/hypha_release_criteria.mjs';
import { confirmLsReadback } from './ls_release/hypha_release_ls.mjs';
import { pkgRecoveryEvidence, requirePkgRecovery } from './ls_release/pkg_release_recovery.mjs';
import { assertRustRuntime } from './provenance/runtime_guard.mjs';
import { produceMac, verifyMac, verifyCi, verifyWindows, packageAll, verifyPackages,
  prepareLs } from './ls_release/hypha_release_local.mjs';
import { hpPreflight, publishGithub, publishHp, publicDownloadFacts,
  verifyPublicHp } from './ls_release/hypha_release_hp.mjs';
import { assetDecision, embeddedAssets } from './provenance/asset_gate.mjs';
import { verifyReleaseProvenance } from './provenance/distribution_gate.mjs';
import { prepareSourceDelivery } from './provenance/source_delivery.mjs';
import { updateBinding, validatePublicKey, assertUpdateBinding } from './updates/update_key_binding.mjs';
import { requireFactoryIdentity, requireFactoryDelivery } from './provenance/factory_artifact.mjs';

const MODULE_PATH = fileURLToPath(import.meta.url);
export const ROOT = path.resolve(path.dirname(MODULE_PATH), '..');
export const STAGES = ['provenance-inputs', 'ci', 'source-delivery', 'macos-au-vst3', 'macos-aax', 'windows', 'freeze', 'hosts',
  'packages', 'provenance', 'distribution', 'ls', 'github', 'hp', 'postrelease'];
export const HELP = `Usage: node scripts/build_hypha.mjs --release --state release_state/NAME.json [options]

One workflow through HP upload: macOS Universal AU/VST3/AAX + approved Windows x64
VST3/AAX installer, signing/notarization, A3/A4/A5, LS verification, immutable GitHub
Release, EN/JA HP links, standard staged Vercel deployment, public download read-back.

  --init                    Create private candidate/profile; never overwrite existing state
  --sdk PATH --license-confirmed  External AAX SDK for macOS approved signing producer
  --ci-run ID               Reuse exact-commit complete green CI, never auto-dispatch/rerun
  --windows-installer-dir DIR  Approved same-commit signed-full factory artifact
  --windows-factory-repository OWNER/REPO  Reviewed private factory origin
  --windows-factory-commit SHA  Reviewed exact factory commit for candidate and promotion
  --windows-promotion-run ID    Successful promotion run containing signed-full
  --windows-artifact-archive FILE  Retained signed-full ZIP, verified against GitHub digest
  --bind-windows-promotion ID  Bind delivery once before Windows verification; no execution
  --hp-root DIR             Existing HP main checkout (must be clean/current before publish)
  --ls-state FILE           Existing private LS product-target state
  --notes FILE              Reviewed public release notes
  --provenance-report FILE  Retained exact-payload NOTICE/license/source evidence (private)
  --source-archive FILE     Reuse the private Windows producer's verified source ZIP bytes
  --update-public-key KEY    Approved pinned RSA public key; default empty disables checking
  --fix-rows FILE           Release-specific deep host/CI rows (--init only)
  --owner-decision FILE     Defer named non-safety rows at hosts after freeze (repeatable)
  --owner-override FILE     Owner decision lifting one named host row for this release (--init only; one per row)
  --date YYYY-MM-DD         Release date (default: today UTC)
  --resume-pkg-staple        Explicitly resume a retained package ticket checkpoint (--execute)
  --execute                 Execute/resume; default or --dry-run only prints the stage plan
  --until packages|hp|complete  Default hp; no stage/gate is skipped
  --publish-approved ID     Operator authorization for this exact candidate's public writes
  --help                    Show usage

Missing CI, Windows artifacts, retained host/A5/LS/A7 reports stop at a checkpoint.
Host tests and LS browser upload remain operator actions; no fabricated PASS.
State provides generated report templates and retains hashes/receipts at every stage.
No iLok moves, account budget changes, product pushes or automatic Rollback.
`;

export function parseReleaseArgs(argv) {
  const options = { until: 'hp', execute: false };
  const fields = { '--state': 'state', '--sdk': 'sdk', '--ci-run': 'ciRun',
    '--windows-installer-dir': 'windowsInstallerDir', '--hp-root': 'hpRoot',
    '--ls-state': 'lsState', '--notes': 'notes', '--date': 'date', '--until': 'until',
    '--publish-approved': 'publishApproved', '--provenance-report': 'provenanceReport',
    '--fix-rows': 'fixRows', '--source-archive': 'sourceArchive', '--update-public-key': 'updatePublicKey',
    '--windows-factory-commit': 'windowsFactoryCommit', '--windows-factory-repository': 'windowsFactoryRepository', '--windows-promotion-run': 'windowsPromotionRun',
    '--windows-artifact-archive': 'windowsArtifactArchive', '--bind-windows-promotion': 'bindWindowsPromotion' };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (fields[arg]) {
      const value = argv[++i];
      if (!value || value.startsWith('--')) throw new Error(`${arg} requires a value`);
      options[fields[arg]] = value;
    } else if (arg === '--owner-override' || arg === '--owner-decision') {
      const value = argv[++i];
      if (!value || value.startsWith('--')) throw new Error(`${arg} requires a value`);
      const key = arg === '--owner-override' ? 'ownerOverride' : 'ownerDecision';
      options[key] = [...[options[key]].flat().filter(Boolean), value];
    } else if (arg === '--init') options.init = true;
    else if (arg === '--execute') options.execute = true;
    else if (arg === '--resume-pkg-staple') options.resumePkgStaple = true;
    else if (arg === '--dry-run') options.dryRun = true;
    else if (arg === '--license-confirmed') options.licenseConfirmed = true;
    else if (arg === '--help' || arg === '-h') options.help = true;
    else throw new Error(`Unknown release option: ${arg}`);
  }
  if (!['packages', 'hp', 'complete'].includes(options.until)) throw new Error('Invalid --until');
  if (options.execute && (options.dryRun || options.init)) throw new Error('Init/dry-run must not execute');
  if (options.resumePkgStaple && (!options.execute || options.init || options.bindWindowsPromotion)) throw new Error('--resume-pkg-staple requires execution of the retained packages checkpoint');
  if (options.ownerOverride && !options.init) throw new Error('--owner-override binds only at --init');
  if (options.fixRows && !options.init) throw new Error('--fix-rows binds only at --init');
  if (options.ownerDecision && (!options.execute || options.init)) throw new Error('--owner-decision requires a hosts execution');
  if (options.bindWindowsPromotion && (options.execute || options.init || options.dryRun
      || options.windowsPromotionRun || !options.windowsArtifactArchive)) {
    throw new Error('--bind-windows-promotion requires an archive and cannot init, execute or plan');
  }
  if (options.init && Boolean(options.windowsPromotionRun) !== Boolean(options.windowsArtifactArchive)) {
    throw new Error('Initial Windows delivery requires both promotion run and archive, or neither');
  }
  if (!options.help && !options.state) throw new Error('--state is required');
  return options;
}

export function makeState(root, options, snapshot = sourceSnapshot) {
  const source = snapshot(root);
  const updatePublicKey = validatePublicKey(options.updatePublicKey || '');
  const keySuffix = updatePublicKey ? `-update-${updateBinding(updatePublicKey).publicKeySha256.slice(0, 12)}` : '';
  const version = fs.readFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'utf8')
    .match(/^version\s*=\s*"(\d+\.\d+\.\d+)"/m)?.[1];
  const candidate = { id: `${source.bNumber}-${version}-${source.commit.slice(0, 12)}${keySuffix}`,
    commit: source.commit, bNumber: source.bNumber, version };
  const inputPath = value => value ? path.resolve(root, value) : '';
  const directory = path.relative(root, path.dirname(path.resolve(root, options.state)));
  const prefix = path.join(directory, candidate.id);
  const inputs = { date: options.date || new Date().toISOString().slice(0, 10),
    sdk: inputPath(options.sdk), licenseConfirmed: !!options.licenseConfirmed, ciRun: options.ciRun || '',
    windowsInstallerDir: inputPath(options.windowsInstallerDir), hpRoot: inputPath(options.hpRoot),
    windowsFactoryRepository: options.windowsFactoryRepository || '', windowsFactoryCommit: options.windowsFactoryCommit || '', windowsPromotionRun: options.windowsPromotionRun || '',
    windowsArtifactArchive: inputPath(options.windowsArtifactArchive),
    hpBaseCommit: '', hpProjectSha256: '', lsState: inputPath(options.lsState), notes: inputPath(options.notes),
    provenanceReport: inputPath(options.provenanceReport), sourceArchive: inputPath(options.sourceArchive), updatePublicKey,
    hostsReport: path.join(prefix, 'hosts.json'), distributionReport: path.join(prefix, 'distribution.json'),
    lsReport: path.join(prefix, 'ls.json'), postreleaseReport: path.join(prefix, 'postrelease.json') };
  if (inputs.hpRoot) inputs.hpBaseCommit = childProcess.execFileSync('git', ['rev-parse', 'HEAD'],
    { cwd: inputs.hpRoot, encoding: 'utf8' }).trim();
  if (inputs.hpRoot && fs.existsSync(path.join(inputs.hpRoot, '.vercel/project.json'))) {
    inputs.hpProjectSha256 = fileFact(path.join(inputs.hpRoot, '.vercel/project.json')).sha256;
  }
  const ownerOverrides = [options.ownerOverride].flat().filter(Boolean).map(file => ownerOverride(inputPath(file), candidate));
  if (new Set(ownerOverrides.map(o => o.testId)).size !== ownerOverrides.length) throw new Error('Owner override names a row twice');
  const fixes = options.fixRows ? fixRows(inputPath(options.fixRows), candidate) : undefined;
  return { schema: SCHEMA, ...(fixes ? { fixRows: fixes } : {}), root: fs.realpathSync(root), candidate, source, inputs,
    requiredHostTests: [...HOST_TESTS], expected: Object.fromEntries(HOST_TESTS.map(id => [id,
      SAFETY_EXPECTED[id] || { passed: true }]).concat((fixes?.rows || []).map(row => [row.id, row.expected]))), ...(ownerOverrides.length ? { ownerOverrides } : {}),
    stages: {}, releaseState: 'DIAGNOSTIC' };
}

export function nativeRunner(root) {
  return async (tool, args, options = {}) => {
    // stdoutFile: binary output goes straight to a new owned file (never decoded or overwritten).
    const output = options.stdoutFile ? fs.openSync(options.stdoutFile, 'wx', 0o600) : null;
    const stdio = output !== null ? ['ignore', output, 'pipe'] : options.capture ? ['ignore', 'pipe', 'pipe'] : 'inherit';
    let result;
    try {
      result = childProcess.spawnSync(tool, args, { cwd: options.cwd || root, encoding: 'utf8',
        env: { ...process.env, ...(options.env || {}) }, stdio, maxBuffer: 32 * 1024 * 1024 });
    } finally { if (output !== null) fs.closeSync(output); }
    if (result.error || result.status !== 0) {
      if (options.optional404 && /HTTP 404/.test(result.stderr || '')) return '';
      const error = new Error(`${tool} failed; inspect retained tool output (exit ${result.status ?? 'unavailable'})`);
      error.exitCode = result.status; throw error;
    }
    return (result.stdout || '') + (options.stderr ? result.stderr || '' : '');
  };
}

function template(state, input, gate, binding, ids) {
  const file = safeStatePath(state.root, resolveInput(state, state.inputs[input]));
  if (!fs.existsSync(file)) atomicJson(file, reportTemplate(state, gate, binding, ids));
}

export function realActions(state, options, run, save, fetcher = fetch) {
  const report = (input, gate, binding, ids) => {
    template(state, input, gate, binding, ids);
    return { facts: validateReport(state, state.inputs[input], gate, binding, ids) };
  };
  return {
    'provenance-inputs': async () => {
      requireFactoryIdentity(state);
      const decision = assetDecision(state.root, embeddedAssets(state.root));
      if (!decision.approved) throw new Checkpoint('Embedded materials have distribution holds; verify rights or review replacements before producing a release');
      return { evidence: decision };
    },
    ci: async () => ({ evidence: await verifyCi(state, run) }),
    'source-delivery': async () => {
      if (!state.inputs.provenanceReport) throw new Checkpoint('Private provenance report path required before producing containers');
      const reportPath = safeStatePath(state.root, resolveInput(state, state.inputs.provenanceReport));
      const result = prepareSourceDelivery({ root: state.root, commit: state.candidate.commit,
        version: state.candidate.version, reportPath,
        archiveInput: state.inputs.sourceArchive,
        directory: path.join(path.dirname(reportPath), state.candidate.id, 'source-delivery') });
      return { facts: result.facts, evidence: { legalDir: result.legalDir } };
    },
    'macos-au-vst3': async () => ({ facts: await produceMac(state, run) }),
    'macos-aax': async () => ({ facts: await produceMac(state, run, true) }),
    windows: async () => ({ facts: await verifyWindows(state, run) }),
    freeze: async () => {
      const payloads = ['macos-au-vst3', 'macos-aax', 'windows'].flatMap(s => state.stages[s].facts);
      checkFacts(payloads);
      state.freeze = { source: state.source, payloads, updateCheck: updateBinding(state.inputs.updatePublicKey), requiredHostTests: state.requiredHostTests, expected: state.expected,
        criteriaSha256: digest(acceptanceCriteria(state)),
        inputs: digest(state.inputs), publicationNotes: fileFact(resolveInput(state, state.inputs.notes)), toolchain: {
          node: process.version, xcode: (await run('xcodebuild', ['-version'], { capture: true })).trim(),
          rustc: assertRustRuntime(await run('rustc', ['--version'], { capture: true })),
          cargo: (await run('cargo', ['--version'], { capture: true })).trim(),
          cmake: (await run('cmake', ['--version'], { capture: true })).trim(),
          sdk: treeFact(state.inputs.sdk),
        } };
      state.freeze.sha256 = digest(state.freeze);
      return { facts: payloads };
    },
    hosts: async () => {
      const rows = ciFixRows(state);
      template(state, 'hostsReport', 'A4', state.freeze.sha256, hostTestIds(state));
      state.stages.hosts.facts = [fileFact(resolveInput(state, state.inputs.hostsReport))];
      save(); // Retain the attempted report identity even when validation fails.
      const result = report('hostsReport', 'A4', state.freeze.sha256, hostTestIds(state));
      result.facts.push(...rows.filter(row => row.decision).map(row => row.decision));
      return { ...result, evidence: { ciFixRows: rows } };
    },
    packages: async () => ({ facts: await packageAll(state, run, { resumeStaple: options.resumePkgStaple }),
      evidence: pkgRecoveryEvidence(state) }),
    provenance: async () => verifyReleaseProvenance(state),
    distribution: async () => {
      checkFacts(state.freeze.payloads);
      await verifyMac(state, run); await verifyMac(state, run, true);
      await verifyWindows(state, run);
      checkFacts(state.stages.packages.facts);
      await prepareLs(state, run);
      return report('distributionReport', 'A5', digest(state.stages.packages.facts), DISTRIBUTION_TESTS);
    },
    ls: async () => {
      authorization(state, options);
      await hpPreflight(state, run); // fail before any public release if HP is knowingly unusable
      const products = readJson(resolveInput(state, state.inputs.lsState)).lemonSqueezy.products;
      const result = report('lsReport', 'A6-LS', digest(state.stages.packages.facts), products.map(p => `LS/${p.productId}`));
      const uploaded = readJson(resolveInput(state, state.inputs.lsReport));
      const pkgHash = state.stages.packages.facts[0].sha256;
      for (const row of uploaded.tests) {
        if (row.measured?.sha256 !== pkgHash || !row.evidence.some(e => e.sha256 === pkgHash)) {
          throw new Checkpoint('LS needs retained downloaded PKG bytes matching this candidate for every configured product');
        }
      }
      const apiChecks = await confirmLsReadback(state, run, save);
      return { ...result, evidence: { apiChecks } };
    },
    github: async () => {
      authorization(state, options); await hpPreflight(state, run);
      return { evidence: await publishGithub(state, run, save) };
    },
    hp: async () => {
      authorization(state, options);
      const downloads = await publicDownloadFacts(state,
        [...state.stages.packages.facts, ...state.stages.provenance.publicFacts], fetcher);
      const evidence = await publishHp(state, run, save, fetcher);
      return { evidence: { ...evidence, downloads } };
    },
    postrelease: async () => {
      await verifyPublicHp(state, fetcher);
      await publicDownloadFacts(state,
        [...state.stages.packages.facts, ...state.stages.provenance.publicFacts], fetcher);
      return report('postreleaseReport', 'A7', digest(state.stages.packages.facts), POST_TESTS);
    },
  };
}

export async function executeRelease(state, options, { statePath, run = nativeRunner(state.root),
  snapshot = sourceSnapshot, clean = () => requireCleanReleaseSource({ root: state.root }),
  actions, fetcher = fetch, log = console.log, platform = process.platform } = {}) {
  assertState(state);
  if (!options.execute) { log(JSON.stringify({ candidate: state.candidate, until: options.until,
    stages: STAGES, currentState: state.releaseState, externalWrites: 'none (plan only)' }, null, 2)); return state; }
  if (platform !== 'darwin' && !actions) throw new Error('Release coordinator runs on macOS; Windows producer uses approved factory');
  clean();
  if (digest(snapshot(state.root)) !== digest(state.source)) throw new Error('Candidate source changed; initialize a new candidate');
  if (state.releaseState === 'RELEASE_INCIDENT') throw new Checkpoint('Incident requires operator reconciliation/Rollback, not automatic publication retry');
  fs.mkdirSync(path.dirname(statePath), { recursive: true });
  const lock = `${statePath}.lock`; const fd = fs.openSync(lock, 'wx');
  const save = () => atomicJson(statePath, state);
  const active = actions || realActions(state, options, run, save, fetcher);
  const end = { packages: 'packages', hp: 'hp', complete: 'postrelease' }[options.until];
  let stage; let startedThisAttempt = false;
  try {
    for (stage of STAGES.slice(0, STAGES.indexOf(end) + 1)) {
      startedThisAttempt = false;
      if (digest(snapshot(state.root)) !== digest(state.source)) throw new Error('Source changed during pipeline');
      if (state.freeze) {
        const { sha256, ...frozen } = state.freeze;
        if (sha256 !== digest(frozen)) throw new Error('Frozen candidate receipt changed');
      }
      if (state.freeze && state.freeze.inputs !== digest(state.inputs)) throw new Error('Frozen release inputs changed');
      if (state.freeze) assertUpdateBinding(state.freeze.updateCheck, state.inputs.updatePublicKey);
      if (state.freeze && state.freeze.criteriaSha256 !== digest(acceptanceCriteria(state))) {
        throw new Error('Frozen acceptance criteria changed');
      }
      if (state.freeze?.publicationNotes) checkFacts([state.freeze.publicationNotes]);
      checkFixRows(state);
      checkOwnerDecisions(state);
      if (stage === 'hosts' && options.ownerDecision) {
        if (state.stages.hosts?.status === 'PASS') throw new Error('Host acceptance is already complete');
        try { bindOwnerDecisions(state, options.ownerDecision.map(file => resolveInput(state, file))); }
        catch (error) { throw new Checkpoint(`Owner decision input needs correction: ${error.message}`); }
        save();
      }
      const saved = state.stages[stage];
      // factory の段（actions）でも、promotion を結ぶ前に windows の段を作らない。作ると後から結べなくなる。
      // 結んだ後に PASS になった段は、やり直し（PKG の staple など）で作り直さないのでそのまま使う。
      if (stage === 'windows' && (!actions || saved?.status !== 'PASS')) requireFactoryDelivery(state);
      if (saved?.status === 'PASS') {
        if (saved.facts) checkFacts(saved.facts);
        if (stage === 'ci' && !actions) { saved.evidence = await verifyCi(state, run); save(); }
        if (stage === 'hp' && !actions) await verifyPublicHp(state, fetcher);
        log(`[hypha-release] ${stage}: verified/reused`); continue;
      }
      const resumePackage = stage === 'packages' && options.resumePkgStaple && ['FAIL', 'PENDING'].includes(saved?.status);
      if (stage === 'packages' && options.resumePkgStaple) {
        if (!resumePackage) throw new Checkpoint('Only a retained failed/pending packages checkpoint can resume staple');
        requirePkgRecovery(state);
      }
      if (saved?.status === 'RUNNING' || (saved?.status === 'FAIL' && !resumePackage && !(stage === 'hosts' && options.ownerDecision))) {
        throw new Checkpoint(`${stage}: interrupted/failed output needs reconciliation; no blind rebuild/resign`);
      }
      const { previous = [], ...attempt } = saved || {};
      const history = saved?.status === 'FAIL' || resumePackage ? [...previous, attempt] : previous;
      state.stages[stage] = { status: 'RUNNING', startedAt: new Date().toISOString(),
        ...(resumePackage ? { evidence: saved.evidence } : {}),
        ...(history.length ? { previous: history } : {}) };
      startedThisAttempt = true; save();
      const result = await active[stage]();
      if (digest(snapshot(state.root)) !== digest(state.source)) throw new Error('Source changed during stage');
      state.stages[stage] = { status: 'PASS', completedAt: new Date().toISOString(),
        ...(history.length ? { previous: history } : {}), ...result };
      if (stage === 'hosts') state.releaseState = 'HOST_ACCEPTED';
      if (stage === 'distribution') state.releaseState = 'RELEASE_READY';
      if (stage === 'hp') state.releaseState = 'RELEASED';
      if (stage === 'postrelease') state.releaseState = 'RELEASE_COMPLETE';
      delete state.blocker; save(); log(`[hypha-release] ${stage}: PASS`);
    }
    return state;
  } catch (error) {
    const checkpoint = error instanceof Checkpoint;
    if (!checkpoint || startedThisAttempt) {
      state.stages[stage] = { ...state.stages[stage], status: checkpoint ? 'PENDING' : 'FAIL' };
    }
    if (stage === 'packages' && startedThisAttempt) {
      const evidence = pkgRecoveryEvidence(state);
      if (evidence) state.stages[stage].evidence = evidence;
    }
    state.blocker = { stage, reason: error.message };
    if (state.publicationStarted && !checkpoint) state.releaseState = 'RELEASE_INCIDENT';
    save(); log(`[hypha-release] ${stage}: ${checkpoint ? 'CHECKPOINT' : 'FAIL'}; ${error.message}`);
    return state;
  } finally { fs.closeSync(fd); fs.unlinkSync(lock); }
}

export function bindWindowsPromotion(statePath, options, snapshot = sourceSnapshot) {
  const lock = `${statePath}.lock`, fd = fs.openSync(lock, 'wx');
  try {
    const state = readJson(statePath);
    assertState(state);
    if (state.freeze || state.stages.windows || state.inputs.windowsPromotionRun || state.inputs.windowsArtifactArchive) {
      throw new Error('Promotion may bind only once before Windows verification and freeze');
    }
    if (digest(snapshot(state.root)) !== digest(state.source)) throw new Error('Candidate source changed');
    const inputs = { ...state.inputs, windowsPromotionRun: options.bindWindowsPromotion,
      windowsArtifactArchive: path.resolve(state.root, options.windowsArtifactArchive) };
    requireFactoryDelivery({ ...state, inputs });
    fileFact(inputs.windowsArtifactArchive);
    state.inputs = inputs;
    atomicJson(statePath, state);
    return state;
  } finally { fs.closeSync(fd); fs.unlinkSync(lock); }
}

export async function runReleaseCli(argv, root = ROOT) {
  const options = parseReleaseArgs(argv);
  if (options.help) { console.log(HELP); return; }
  const statePath = safeStatePath(root, options.state);
  childProcess.execFileSync('git', ['check-ignore', '-q', '--', path.relative(root, statePath)], { cwd: root });
  if (options.init) {
    if (fs.existsSync(statePath)) throw new Error('Existing candidate state must not be replaced');
    const state = makeState(root, options); assertState(state);
    fs.mkdirSync(path.dirname(statePath), { recursive: true });
    fs.writeFileSync(statePath, `${JSON.stringify(state, null, 2)}\n`, { flag: 'wx', mode: 0o600 });
    console.log(`[hypha-release] initialized ${state.candidate.id}; no build/sign/upload`); return state;
  }
  const state = readJson(statePath);
  if (state.root !== fs.realpathSync(root)) throw new Error('State belongs to another checkout');
  if (options.bindWindowsPromotion) {
    const bound = bindWindowsPromotion(statePath, options);
    console.log('[hypha-release] Windows promotion bound; no build/sign/upload');
    return bound;
  }
  const result = await executeRelease(state, options, { statePath });
  if (result.blocker) process.exitCode = 2;
  return result;
}

if (process.argv[1] && path.resolve(process.argv[1]) === MODULE_PATH) {
  try { await runReleaseCli(process.argv.slice(2)); }
  catch (error) { console.error(`[hypha-release] ERROR: ${error.message}`); process.exitCode = 1; }
}
