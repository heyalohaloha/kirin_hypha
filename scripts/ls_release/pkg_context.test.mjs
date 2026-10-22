import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { pkgRecoveryContext } from './pkg_release_recovery.mjs';
import { readReleaseSourceIdentity } from './release_source_identity.mjs';
import { digest } from './hypha_release_contract.mjs';

test('producer read-only context agrees with the frozen coordinator, including canonical paths', t => {
  const root = path.resolve(import.meta.dirname, '../..');
  const temporary = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-pkg-context-'));
  t.after(() => fs.rmSync(temporary, { recursive: true, force: true }));
  const output = path.join(temporary, 'output'); fs.mkdirSync(output);
  const alias = path.join(temporary, 'alias'); fs.symlinkSync(output, alias);
  const hostsReport = 'release_state/context-fixture/hosts.json', binding = 'c'.repeat(64);
  const receipt = path.join(root, 'release_state/context-fixture/pkg-staple/receipt.json');
  const result = spawnSync(process.execPath, ['scripts/ls_release/build_kirin_hypha_pkg.mjs', '--with-aax', '--print-staple-context'], {
    cwd: root, encoding: 'utf8', env: { ...process.env, KIRIN_RELEASE_DIR: alias, KIRIN_PKG_STAPLE_STATE: receipt,
      KIRIN_PKG_STAPLE_BINDING: binding, KIRIN_SKIP_PKG_SIGN: '0', KIRIN_SKIP_PKG_NOTARIZE: '0', KIRIN_HYPHA_UPDATE_PUBLIC_KEY: '' },
  });
  assert.equal(result.status, 0, result.stderr);
  const producer = JSON.parse(result.stdout);
  const source = readReleaseSourceIdentity({ root });
  const version = fs.readFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'utf8').match(/^version\s*=\s*"([^"]+)"/m)[1];
  assert.deepEqual(producer.identity.source, { commit: source.commit, bNumber: source.bNumber });
  // The coordinator's ordinary output root is fixed. Compare that same root via a symlinked checkout fixture.
  const checkout = path.join(temporary, 'checkout'); fs.mkdirSync(path.join(checkout, 'dist'), { recursive: true });
  fs.symlinkSync(output, path.join(checkout, 'dist/LS_UPLOAD'));
  const coordinator = pkgRecoveryContext({ root: checkout, freeze: { sha256: binding },
    candidate: { ...source, version }, inputs: { hostsReport, updatePublicKey: '' } });
  const projection = context => ({ packagePath: context.packagePath, binding: context.binding, identity: context.identity });
  assert.equal(digest(projection(producer)), digest(projection(coordinator)));
  for (const field of ['version', 'aaxIncluded', 'updatePublicKey']) {
    const changed = structuredClone(producer); changed.identity[field] = `changed-${field}`;
    assert.notEqual(digest(projection(changed)), digest(projection(coordinator)));
  }
  assert.equal(fs.existsSync(receipt), false);
  assert.equal(fs.readdirSync(output).length, 0);
});

test('unsigned smoke can repeat without touching a signed receipt; signed output still refuses replacement', async t => {
  const { requireFreshPackage } = await import('./pkg_release_recovery.mjs');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-pkg-smoke-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const context = { packagePath: path.join(root, 'fixture-UNSIGNED-DO-NOT-UPLOAD.pkg'), statePath: path.join(root, 'signed-receipt.json') };
  fs.writeFileSync(context.statePath, 'signed receipt preserved');
  for (let attempt = 0; attempt < 2; attempt++) {
    requireFreshPackage(context, { unsignedSmoke: true }); fs.writeFileSync(context.packagePath, 'unsigned fixture');
  }
  assert.equal(fs.readFileSync(context.statePath, 'utf8'), 'signed receipt preserved');
  assert.throws(() => requireFreshPackage(context));
  assert.throws(() => requireFreshPackage({ ...context, packagePath: path.join(root, 'signed.pkg') }, { unsignedSmoke: true }));
});
