import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { verifyRetainedLs } from './hypha_release_reconcile_ls.mjs';
import { digest, fileFact, reportTemplate } from './hypha_release_contract.mjs';

test('LS reconciliation requires both retained exact download and successful read-only API checks', async t => {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-ls-reconcile-')));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const pkg = path.join(root, 'package.pkg'); fs.writeFileSync(pkg, 'signed bytes');
  const target = path.join(root, 'targets.json'); fs.writeFileSync(target, JSON.stringify({ lemonSqueezy: { products: [{ productId: 1 }] } }));
  const reportPath = path.join(root, 'report.json');
  const state = { root, freeze: { sha256: 'f'.repeat(64) }, candidate: { id: 'candidate' }, expected: {},
    inputs: { lsState: target, lsReport: reportPath }, stages: { packages: { facts: [fileFact(pkg)] } } };
  const report = reportTemplate(state, 'A6-LS', digest(state.stages.packages.facts), ['LS/1']);
  report.tests[0] = { id: 'LS/1', result: 'PASS', expected: { sha256: fileFact(pkg).sha256 }, measured: { sha256: fileFact(pkg).sha256 }, oracle: 'retained download', evidence: [fileFact(pkg)] };
  fs.writeFileSync(reportPath, JSON.stringify(report));
  let count = 0;
  const run = async (tool, args) => { count++; assert.equal(tool, 'node'); assert.ok(args.includes('--with-ls-api')); return JSON.stringify({ ok: true, currentArtifacts: [{ sha256: fileFact(pkg).sha256 }], checks: [{ id: 'LS/1/product/id', ok: true }, { id: 'LS/1/variant/id', ok: true }] }); };
  const result = await verifyRetainedLs(state, run); assert.equal(result.facts.length, 3); assert.equal(count, 1);
  const before = fs.readFileSync(reportPath); report.tests[0].measured.sha256 = '0'.repeat(64); fs.writeFileSync(reportPath, JSON.stringify(report));
  await assert.rejects(verifyRetainedLs(state, run)); assert.equal(count, 1);
  fs.writeFileSync(reportPath, before);
  await assert.rejects(verifyRetainedLs(state, async () => JSON.stringify({ ok: false, checks: [{ ok: false }] })), /not fully/);
});

test('distribution reconciliation checks the retained LS preparation without rewriting it', async t => {
  const { verifyPreparedLs } = await import('./hypha_release_reconcile_ls.mjs');
  const { loadMacShipBundleManifest } = await import('./kirin_hypha_ship_bundles.mjs');
  const { loadMacAaxBundleManifest } = await import('./kirin_hypha_aax_bundles.mjs');
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-ls-prepared-')));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'config')); fs.mkdirSync(path.join(root, 'release_state'));
  for (const name of ['hypha_macos_ship_bundles.json', 'hypha_macos_aax_bundles.json'])
    fs.copyFileSync(new URL(`../../config/${name}`, import.meta.url), path.join(root, 'config', name));
  const pkg = path.join(root, 'dist/LS_UPLOAD/Kirin-Hypha-1.2.3-macOS-Universal.pkg');
  fs.mkdirSync(path.dirname(pkg), { recursive: true });
  const meta = { fileName: path.basename(pkg), size: 12, sha512: 'fixture', sha256: 'a'.repeat(64), lsDisplaySize: 'fixture' };
  fs.writeFileSync(`${pkg}.json`, JSON.stringify(meta));
  const file = path.join(root, 'release_state/ls.json');
  const ls = { product: { version: '1.2.3' }, lemonSqueezy: { products: [{ productName: 'fixture', productId: 1 }] },
    artifacts: [{ label: 'Universal Installer', type: 'pkg', fileName: meta.fileName, path: pkg,
      ...Object.fromEntries(['size', 'sha512', 'sha256', 'lsDisplaySize'].map(k => [k, meta[k]])) }],
    expectedPayloads: [...loadMacShipBundleManifest({ root }).bundles, ...loadMacAaxBundleManifest({ root }).bundles].map(b => b.install_relative) };
  fs.writeFileSync(file, JSON.stringify(ls)); const before = fs.readFileSync(file);
  const state = { root, candidate: { version: '1.2.3' }, inputs: { lsState: file, windowsInstallerDir: root } };
  let calls = 0;
  const run = async (tool, args) => { calls++; assert.equal(tool, 'node'); assert.ok(args.includes('--with-apple-verification')); return ''; };
  await verifyPreparedLs(state, run); assert.deepEqual(fs.readFileSync(file), before); assert.equal(calls, 1);
  ls.artifacts[0].sha256 = 'b'.repeat(64); fs.writeFileSync(file, JSON.stringify(ls));
  await assert.rejects(verifyPreparedLs(state, run), /differs/i); assert.equal(calls, 1);
});
