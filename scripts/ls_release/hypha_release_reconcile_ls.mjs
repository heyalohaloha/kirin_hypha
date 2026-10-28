// Read-only reconciliation of retained LS downloads and current API metadata.
import { digest, fileFact, readJson, resolveInput, safeStatePath, validateReport } from './hypha_release_contract.mjs';
import { artifactPaths } from './hypha_release_local.mjs';
import { loadMacShipBundleManifest } from './kirin_hypha_ship_bundles.mjs';
import { loadMacAaxBundleManifest } from './kirin_hypha_aax_bundles.mjs';

export async function verifyRetainedLs(state, run) {
  if (!run || !state.freeze) throw new Error('Frozen candidate and read-only runner required');
  const targetFile = resolveInput(state, state.inputs.lsState);
  const products = readJson(targetFile).lemonSqueezy?.products;
  if (!Array.isArray(products) || !products.length) throw new Error('LS product targets missing');
  const facts = validateReport(state, state.inputs.lsReport, 'A6-LS', digest(state.stages.packages.facts),
    products.map(product => `LS/${product.productId}`));
  const pkgHash = state.stages.packages.facts[0].sha256;
  for (const row of readJson(resolveInput(state, state.inputs.lsReport)).tests) {
    if (row.measured?.sha256 !== pkgHash || !row.evidence?.some(item => item.sha256 === pkgHash)) {
      throw new Error('LS retained downloads do not match the frozen package');
    }
  }
  const output = await run('node', ['scripts/ls_release/kirin_hypha_ls_dry_run.mjs', '--state', targetFile,
    '--with-ls-api', '--json'], { capture: true });
  const checks = JSON.parse(output);
  if (!checks.ok || !Array.isArray(checks.checks) || !checks.checks.length
      || checks.checks.some(item => item.ok !== true)
      || !Array.isArray(checks.currentArtifacts) || !checks.currentArtifacts.some(item => item.sha256 === pkgHash)
      || products.some(product => ![`LS/${product.productId}/product/id`, `LS/${product.productId}/variant/id`]
        .every(id => checks.checks.some(item => item.id === id && item.ok === true)))) {
    throw new Error('LS API read-back is not fully verified');
  }
  return { facts: [...facts, fileFact(targetFile)], evidence: { apiChecks: checks } };
}

// Inspect the retained LS preparation without rewriting the operator's state file.
export async function verifyPreparedLs(state, run) {
  const file = safeStatePath(state.root, resolveInput(state, state.inputs.lsState));
  const ls = readJson(file), pkgFile = artifactPaths(state)[0], pkg = readJson(`${pkgFile}.json`);
  if (ls.product?.version !== state.candidate.version || !ls.lemonSqueezy?.products?.length
      || ls.lemonSqueezy.products.some(p => !p.productName || !p.productId)) throw new Error('LS product targets differ');
  const artifacts = [{ label: 'Universal Installer', type: 'pkg', fileName: pkg.fileName, path: pkgFile,
    ...Object.fromEntries(['size', 'sha512', 'sha256', 'lsDisplaySize'].map(key => [key, pkg[key]])) }];
  const expectedPayloads = [...loadMacShipBundleManifest({ root: state.root }).bundles,
    ...loadMacAaxBundleManifest({ root: state.root }).bundles].map(bundle => bundle.install_relative);
  if (digest(ls.artifacts) !== digest(artifacts) || digest(ls.expectedPayloads) !== digest(expectedPayloads)) {
    throw new Error('Retained LS preparation differs from this candidate');
  }
  await run('node', ['scripts/ls_release/kirin_hypha_ls_dry_run.mjs', '--state', file, '--with-apple-verification'], { capture: true });
  return fileFact(file);
}
