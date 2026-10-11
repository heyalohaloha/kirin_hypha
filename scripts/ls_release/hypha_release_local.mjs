// Wrap the approved producers; raw diagnostic output is never fed into the signing factory.
import fs from 'node:fs';
import path from 'node:path';
import { loadMacShipBundleManifest } from './kirin_hypha_ship_bundles.mjs';
import { loadMacAaxBundleManifest } from './kirin_hypha_aax_bundles.mjs';
import { requireWindowsInstaller } from './build_kirin_hypha_release_set.mjs';
import { AAX_APPLE_AUTHORITY } from './aax_bundle_verify.mjs';
import { fileFact, treeFact, readJson, resolveInput, safeStatePath, Checkpoint, atomicJson } from './hypha_release_contract.mjs';
import { inspectUpdateBinary, verifyUpdatePlist, assertPackageUpdateBinding } from '../updates/update_key_binding.mjs';
import { verifyLegalDelivery } from '../provenance/legal_delivery.mjs';
import { verifyFactoryDelivery } from '../provenance/factory_artifact.mjs';

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
  const pages = JSON.parse(await run('gh', ['api', `${base}/${id}/jobs?per_page=100`], { capture: true }));
  for (const name of ['public history identity', 'release source contract (macos)',
    'auval arm64 (AU validation)', 'windows VST3 preflight']) {
    const matches = pages.jobs?.filter(j => j.name === name) || [];
    const job = matches[0];
    if (matches.length !== 1 || job.status !== 'completed' || job.conclusion !== 'success') throw new Checkpoint(`Required CI job is not green: ${name}`);
  }
  return { runId: id, headSha: record.head_sha, workflow: record.path };
}

export async function verifyWindows(state, run) {
  if (!state.inputs.windowsInstallerDir) throw new Checkpoint('Approved same-commit Windows signed-full installer required');
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
    path.join(path.dirname(installer), `Kirin-Hypha-${state.candidate.version}-Windows-x64-AAX.json`)].map(fileFact).concat(factoryFacts);
}

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
      || zip.unsigned_smoke_test !== false || zip.aax_included !== true || zip.git_dirty !== ''
      || zip.sha256 !== fileFact(files[3]).sha256) throw new Error('ZIP release identity/signing/hash mismatch');
  for (const file of [files[0], files[3], files[6]]) {
    const text = fs.readFileSync(`${file}.sha256`, 'utf8').trim();
    if (text !== `${fileFact(file).sha256}  ${path.basename(file)}`) throw new Error('Package checksum sidecar mismatch');
  }
  return files.map(fileFact);
}

export async function packageAll(state, run) {
  const legalDir = state.stages['source-delivery']?.evidence?.legalDir;
  if (!legalDir) throw new Checkpoint('Verified source/legal delivery stage required before packaging');
  verifyLegalDelivery(legalDir, state.candidate.commit);
  if (artifactPaths(state).slice(0, 6).some(fs.existsSync)) {
    throw new Checkpoint('Existing package bytes must be qualified/imported, never overwritten by an automatic retry');
  }
  await run('node', ['scripts/ls_release/build_kirin_hypha_release_set.mjs', '--with-aax',
    '--windows-installer-dir', resolveInput(state, state.inputs.windowsInstallerDir)],
    { env: { KIRIN_HYPHA_UPDATE_PUBLIC_KEY: state.inputs.updatePublicKey || '',
      KIRIN_HYPHA_LEGAL_DIR: legalDir } });
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
  await run('node', ['scripts/ls_release/kirin_hypha_ls_dry_run.mjs', '--state', file, '--with-apple-verification'], { capture: true });
}
