// Wrap the approved producers; raw diagnostic output is never fed into the signing factory.
import fs from 'node:fs';
import path from 'node:path';
import { loadMacShipBundleManifest } from './kirin_hypha_ship_bundles.mjs';
import { loadMacAaxBundleManifest } from './kirin_hypha_aax_bundles.mjs';
import { requireWindowsInstaller } from './build_kirin_hypha_release_set.mjs';
import { AAX_APPLE_AUTHORITY } from './aax_bundle_verify.mjs';
import { fileFact, treeFact, readJson, resolveInput, safeStatePath, Checkpoint, atomicJson, CI_ORACLES, checkFacts, assertIndependentDeliveryTree } from './hypha_release_contract.mjs';
import { inspectUpdateBinary, verifyUpdatePlist, assertPackageUpdateBinding } from '../updates/update_key_binding.mjs';
import { verifyLegalDelivery } from '../provenance/legal_delivery.mjs';
import { verifyPkgResume } from './pkg_staple.mjs';
import { pkgRecoveryContext, requirePkgRecovery } from './pkg_release_recovery.mjs';
import { verifyFactoryDelivery, requireFactoryDelivery } from '../provenance/factory_artifact.mjs';

export function macBundles(state, aax = false) {
  return (aax ? loadMacAaxBundleManifest({ root: state.root })
    : loadMacShipBundleManifest({ root: state.root })).bundles;
}

export async function verifyMac(state, run, aax = false) {
  const bundles = macBundles(state, aax);
  for (const b of bundles) {
    const binary = path.join(b.sourcePath, 'Contents/MacOS', b.executable_name);
    inspectUpdateBinary(binary, state.inputs.updatePublicKey, { universal: true });
    const plist = JSON.parse(await run('plutil', ['-convert', 'json', '-o', '-',
      path.join(b.sourcePath, 'Contents/Info.plist')], { capture: true }));
    verifyUpdatePlist(plist, state.inputs.updatePublicKey, b.kind === 'au');
    const architectures = (await run('lipo', ['-archs', binary], { capture: true })).trim().split(/\s+/).sort();
    if (architectures.join(',') !== 'arm64,x86_64') throw new Error('macOS payload is not Universal');
    const version = (await run('/usr/libexec/PlistBuddy', ['-c', 'Print :CFBundleShortVersionString',
      path.join(b.sourcePath, 'Contents/Info.plist')], { capture: true })).trim();
    if (version !== state.candidate.version) throw new Error('macOS payload version mismatch');
    await run('codesign', ['--verify', '--deep', '--strict', '--check-notarization', b.sourcePath]);
    const signature = await run('codesign', ['-d', '--verbose=4', b.sourcePath], { capture: true, stderr: true });
    if (!signature.includes(`Authority=${AAX_APPLE_AUTHORITY}`) || !signature.includes('Timestamp=')) {
      throw new Error('macOS Developer ID/timestamp is invalid');
    }
  }
  if (aax) await run('node', ['scripts/ls_release/aax_notarization_receipt.mjs', 'verify',
    '--artifact-dir', 'build-aax-universal']);
  return [...bundles.map(b => treeFact(b.sourcePath)), ...(aax ?
    [fileFact(path.join(state.root, 'build-aax-universal/kirin-hypha-macos-aax-notarization.json'))] : [])];
}

export async function produceMac(state, run, aax = false) {
  await run('node', ['scripts/provenance/runtime_guard.mjs']);
  // An interrupted or foreign signed product must never be erased by a producer on resume.
  for (const bundle of macBundles(state, aax)) {
    if (fs.existsSync(path.join(bundle.sourcePath, 'Contents/_CodeSignature'))) {
      throw new Checkpoint('Existing signed macOS output has no pipeline receipt; preserve it and explicitly qualify/import it first');
    }
  }
  if (aax) {
    if (!state.inputs.licenseConfirmed || !state.inputs.sdk) throw new Checkpoint('External SDK/license confirmation required');
    await run('bash', ['scripts/build_aax_universal.sh', '--sdk', state.inputs.sdk, '--license-confirmed', '--sign'],
      { env: { KIRIN_HYPHA_UPDATE_PUBLIC_KEY: state.inputs.updatePublicKey || '' } });
  } else {
    await run('bash', ['scripts/build_juce_universal.sh'], { env: { KIRIN_HYPHA_UPDATE_PUBLIC_KEY: state.inputs.updatePublicKey || '' } });
    await run('cargo', ['run', '--package', 'xtask', '--', 'notarize'],
      { env: { KIRIN_HYPHA_UPDATE_PUBLIC_KEY: state.inputs.updatePublicKey || '' } });
  }
  return verifyMac(state, run, aax);
}

export async function verifyCi(state, run) {
  const id = String(state.inputs.ciRun || '');
  if (!/^\d+$/.test(id)) throw new Checkpoint('Same-commit complete CI run ID required; no automatic dispatch/retry');
  const base = 'repos/heyalohaloha/kirin_hypha/actions/runs';
  const record = JSON.parse(await run('gh', ['api', `${base}/${id}`], { capture: true }));
  if (record.head_sha !== state.candidate.commit || record.status !== 'completed'
      || record.conclusion !== 'success' || record.path !== '.github/workflows/ci.yml'
      || record.name !== 'CI' || !['push', 'workflow_dispatch'].includes(record.event)
      || record.head_repository?.full_name !== 'heyalohaloha/kirin_hypha') {
    throw new Checkpoint('CI is not a successful exact-commit ci.yml run');
  }
  if (!Number.isSafeInteger(record.run_attempt) || record.run_attempt < 1) throw new Checkpoint('CI run attempt is unavailable');
  // latest retains successful jobs from earlier attempts when only failed jobs were rerun.
  // https://docs.github.com/en/rest/actions/workflow-jobs#list-jobs-for-a-workflow-run
  const listJobs = async endpoint => {
    const jobs = [];
    let page = 1, total;
    do {
      const batch = JSON.parse(await run('gh', ['api', `${endpoint}${endpoint.includes('?') ? '&' : '?'}per_page=100&page=${page++}`], { capture: true }));
      if (!Array.isArray(batch.jobs) || !Number.isSafeInteger(batch.total_count)
          || batch.total_count < 0 || (!batch.jobs.length && batch.total_count !== 0)
          || (total != null && total !== batch.total_count)) {
        throw new Checkpoint('CI latest jobs pagination is incomplete or changed');
      }
      total = batch.total_count; jobs.push(...batch.jobs);
    } while (jobs.length < total);
    if (jobs.length !== total) throw new Checkpoint('CI latest jobs pagination is inconsistent');
    return jobs;
  };
  const jobs = await listJobs(`${base}/${id}/jobs?filter=latest`);
  const selected = new Map();
  const requiredJob = name => {
    const matches = jobs.filter(job => job.name === name), job = matches[0];
    if (matches.length !== 1 || job.status !== 'completed' || job.conclusion !== 'success'
        || !Number.isSafeInteger(job.id) || job.id < 1 || String(job.run_id) !== id || job.head_sha !== state.candidate.commit
        || !Number.isSafeInteger(job.run_attempt) || job.run_attempt < 1 || job.run_attempt > record.run_attempt) {
      throw new Checkpoint(`Required CI job is not green or has foreign attempt metadata: ${name}`);
    }
    selected.set(name, { name, id: job.id, attempt: job.run_attempt, conclusion: job.conclusion });
    return job;
  };
  for (const name of ['public history identity', 'release source contract (macos)',
    'auval arm64 (AU validation)', 'windows VST3 preflight']) {
    requiredJob(name);
  }
  const oracles = Object.entries(CI_ORACLES).map(([name, oracle]) => ({ name, tests: oracle.tests,
    checks: oracle.checks.map(check => {
      const job = requiredJob(check.job);
      const steps = job.steps?.filter(step => step.name === check.step) || [];
      if (steps.length !== 1 || steps[0].status !== 'completed' || steps[0].conclusion !== 'success') {
        throw new Checkpoint(`Required CI oracle is not green: ${name}/${check.job}/${check.step}`);
      }
      return { ...check, jobId: job.id, attempt: job.run_attempt, conclusion: steps[0].conclusion };
    }) }));
  return { runId: id, headSha: record.head_sha, workflow: record.path, attempt: record.run_attempt, jobs: [...selected.values()], oracles };
}

export async function verifyWindows(state, run) {
  requireFactoryDelivery(state);
  if (!state.inputs.windowsInstallerDir) throw new Checkpoint('Approved same-commit Windows signed-full installer required');
  assertIndependentDeliveryTree(resolveInput(state, state.inputs.windowsInstallerDir));
  if (state.windowsDeliveryBinding?.tree) checkFacts([state.windowsDeliveryBinding.tree]);
  const installer = requireWindowsInstaller(resolveInput(state, state.inputs.windowsInstallerDir), {
    version: state.candidate.version, commit: state.candidate.commit, bNumber: state.candidate.bNumber,
  }, { requireAax: true, updatePublicKey: state.inputs.updatePublicKey || '' });
  const manifest = readJson(`${installer}.json`);
  const sourceHash = state.stages['source-delivery']?.facts?.[0]?.sha256;
  if (!sourceHash || manifest.legalDelivery?.sourceSha256 !== sourceHash
      || manifest.legalDelivery?.commit !== state.candidate.commit) {
    throw new Error('Windows and macOS must deliver the same exact Corresponding Source archive');
  }
  const sourceRun = manifest.source.github_actions_run.split('/').at(-1);
  if (sourceRun !== String(state.inputs.ciRun)) throw new Error('Windows source CI differs from the pinned CI run');
  const factoryFacts = await verifyFactoryDelivery(state, run, manifest, path.dirname(installer));
  return [installer, `${installer}.sha256`, `${installer}.json`,
    path.join(path.dirname(installer), `Kirin-Hypha-${state.candidate.version}-Windows-x64-AAX.json`)].map(fileFact)
    .concat(factoryFacts, [treeFact(path.dirname(installer))]);
}

// The ZIP producer (xtask release_gate GIT_CLEAN_STATE) records a clean source as this exact string;
// "true: ..." and "unknown: ..." describe dirty or unreadable sources.
export const ZIP_CLEAN_GIT_STATE = 'false';

export function artifactPaths(state) {
  const name = `Kirin-Hypha-${state.candidate.version}`;
  const pkg = path.join(state.root, 'dist/LS_UPLOAD', `${name}-macOS-Universal.pkg`);
  const zip = path.join(state.root, 'dist', `${name}-macOS-Universal.zip`);
  const win = path.join(resolveInput(state, state.inputs.windowsInstallerDir), `${name}-Windows-x64-Setup.exe`);
  return [pkg, `${pkg}.sha256`, `${pkg}.json`, zip, `${zip}.sha256`,
    path.join(state.root, 'dist/release-manifest.json'), win, `${win}.sha256`, `${win}.json`,
    path.join(path.dirname(win), `${name}-Windows-x64-AAX.json`)];
}

export function verifyPackages(state) {
  const files = artifactPaths(state);
  const pkg = readJson(`${files[0]}.json`);
  const zip = readJson(files[5]);
  const macIds = ['AU/PRE', 'AU/POST', 'VST3/PRE', 'VST3/POST', 'AAX/PRE', 'AAX/POST'];
  assertPackageUpdateBinding(pkg.updateCheck, state.inputs.updatePublicKey, macIds, { universal: true });
  assertPackageUpdateBinding(zip.updateCheck, state.inputs.updatePublicKey, macIds, { universal: true });
  for (const record of pkg.updateCheck.binaries) {
    const other = zip.updateCheck.binaries.find(b => b.role === record.role && b.format === record.format);
    if (record.binarySha256 !== other.binarySha256 || record.architectures.some(arch =>
      other.architectures.find(a => a.architecture === arch.architecture).sha256 !== arch.sha256)) {
      throw new Error('PKG and ZIP update payload provenance differs');
    }
  }
  const win = readJson(files[8]);
  assertPackageUpdateBinding(win.updateCheck, state.inputs.updatePublicKey,
    ['VST3/PRE', 'VST3/POST', 'AAX/PRE', 'AAX/POST']);
  for (const record of win.updateCheck.binaries) {
    const payload = [...(win.installer?.payload || []), ...(win.installer?.aax_payload || [])]
      .find(p => p.role === record.role && p.format === record.format);
    if (payload?.binary_sha256 !== record.binarySha256) throw new Error('Windows distributed update binary hash mismatch');
  }
  if (pkg.source?.commit !== state.candidate.commit || pkg.source?.bNumber !== state.candidate.bNumber
      || pkg.version !== state.candidate.version || !pkg.signed || !pkg.notarized || !pkg.aaxIncluded
      || pkg.sha256 !== fileFact(files[0]).sha256) throw new Error('PKG release identity/signing/hash mismatch');
  if (zip.version !== state.candidate.version || zip.commit !== state.candidate.commit
      || zip.unsigned_smoke_test !== false || zip.aax_included !== true || zip.git_dirty !== ZIP_CLEAN_GIT_STATE
      || zip.sha256 !== fileFact(files[3]).sha256) throw new Error('ZIP release identity/signing/hash mismatch');
  for (const file of [files[0], files[3], files[6]]) {
    const text = fs.readFileSync(`${file}.sha256`, 'utf8').trim();
    if (text !== `${fileFact(file).sha256}  ${path.basename(file)}`) throw new Error('Package checksum sidecar mismatch');
  }
  return files.map(fileFact);
}

export async function packageAll(state, run, { resumeStaple = false } = {}) {
  const legalDir = state.stages['source-delivery']?.evidence?.legalDir;
  if (!legalDir) throw new Checkpoint('Verified source/legal delivery stage required before packaging');
  verifyLegalDelivery(legalDir, state.candidate.commit);
  const context = pkgRecoveryContext(state);
  const env = { KIRIN_HYPHA_UPDATE_PUBLIC_KEY: state.inputs.updatePublicKey || '', KIRIN_HYPHA_LEGAL_DIR: legalDir,
    KIRIN_PKG_STAPLE_STATE: context.statePath, KIRIN_PKG_STAPLE_BINDING: context.binding };
  if (resumeStaple) {
    requirePkgRecovery(state);
    if (artifactPaths(state).slice(1, 6).some(fs.existsSync)) throw new Checkpoint('Partial distribution containers need reconciliation before PKG resume');
    try {
      await run('node', ['scripts/ls_release/build_kirin_hypha_pkg.mjs', '--with-aax', '--resume-staple'], { env });
    } catch (error) {
      if (error.exitCode === 2) {
        try { verifyPkgResume(context); } catch { throw error; }
        throw new Checkpoint('PKG staple/validation is still unconfirmed; retain and explicitly resume');
      }
      throw error;
    }
    await run('node', ['scripts/ls_release/build_kirin_hypha_release_set.mjs', '--with-aax', '--skip-macos-pkg',
      '--windows-installer-dir', resolveInput(state, state.inputs.windowsInstallerDir)], { env });
    return verifyPackages(state);
  }
  if (artifactPaths(state).slice(0, 6).some(fs.existsSync) || fs.existsSync(context.statePath)) {
    throw new Checkpoint('Existing package bytes must be qualified/imported, never overwritten by an automatic retry');
  }
  try {
    await run('node', ['scripts/ls_release/build_kirin_hypha_release_set.mjs', '--with-aax',
      '--windows-installer-dir', resolveInput(state, state.inputs.windowsInstallerDir)], { env });
  } catch (error) {
    if (error.exitCode === 2) {
      try { verifyPkgResume(context); } catch { throw error; }
      throw new Checkpoint('PKG staple/validation is unconfirmed; retain the same submission for explicit resume');
    }
    throw error;
  }
  return verifyPackages(state);
}

export async function prepareLs(state, run) {
  const file = state.inputs.lsState && resolveInput(state, state.inputs.lsState);
  if (!file || !fs.existsSync(file)) throw new Checkpoint('Private LS product-target state required');
  safeStatePath(state.root, file);
  const ls = readJson(file);
  const pkgFile = artifactPaths(state)[0];
  const pkg = readJson(`${pkgFile}.json`);
  if (ls.product?.version !== state.candidate.version || !ls.lemonSqueezy?.products?.length
      || ls.lemonSqueezy.products.some(p => !p.productName || !p.productId)) {
    throw new Checkpoint('LS state version/configured products are incomplete');
  }
  ls.artifacts = [{ label: 'Universal Installer', type: 'pkg', fileName: pkg.fileName, path: pkgFile,
    ...Object.fromEntries(['size', 'sha512', 'sha256', 'lsDisplaySize'].map(k => [k, pkg[k]])) }];
  ls.expectedPayloads = [...loadMacShipBundleManifest({ root: state.root }).bundles,
    ...loadMacAaxBundleManifest({ root: state.root }).bundles].map(b => b.install_relative);
  atomicJson(file, ls);
  try {
    await run('node', ['scripts/ls_release/kirin_hypha_ls_dry_run.mjs', '--state', file, '--with-apple-verification'], { capture: true });
  } catch (error) {
    if (error.exitCode === 2) throw new Checkpoint('Apple online validation is unconfirmed; local verification remains retained');
    throw error;
  }
}
