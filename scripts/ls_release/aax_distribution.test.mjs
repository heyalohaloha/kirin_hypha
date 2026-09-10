import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { verifyAaxBundle } from './aax_bundle_verify.mjs';
import { loadMacAaxBundleManifest } from './kirin_hypha_aax_bundles.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');

test('AAX manifest resolves exactly PRE and POST outside the ordinary ship set', () => {
  const manifest = loadMacAaxBundleManifest({ root: repoRoot });
  assert.equal(manifest.bundles.length, 2);
  assert.deepEqual(manifest.bundles.map((bundle) => bundle.role), ['PRE', 'POST']);
  assert.ok(manifest.bundles.every((bundle) => bundle.kind === 'aax'));
  assert.ok(manifest.bundles.every((bundle) => bundle.sourcePath.startsWith(manifest.defaultBuildRoot)));
  assert.ok(manifest.bundles.every((bundle) => (
    path.dirname(bundle.install_relative) === 'Library/Application Support/Avid/Audio/Plug-Ins'
  )));
});

test('AAX manifest rejects an install path outside the Avid directory', (context) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'kirin-hypha-aax-manifest-'));
  context.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const source = JSON.parse(fs.readFileSync(
    path.join(repoRoot, 'config/hypha_macos_aax_bundles.json'),
    'utf8',
  ));
  source.bundles[0].install_relative = 'Library/Audio/Plug-Ins/VST3/Kirin Hypha PRE.aaxplugin';
  fs.mkdirSync(path.join(root, 'config'), { recursive: true });
  fs.writeFileSync(
    path.join(root, 'config/hypha_macos_aax_bundles.json'),
    `${JSON.stringify(source)}\n`,
  );
  assert.throws(
    () => loadMacAaxBundleManifest({ root }),
    /outside the Avid plug-in directory/,
  );
});

test('AAX verifier fails closed when the requested bundle is missing', () => {
  assert.throws(
    () => verifyAaxBundle({
      bundlePath: '/tmp/kirin-hypha-aax-missing.aaxplugin',
      executableName: 'Kirin Hypha PRE',
      bundleIdentifier: 'com.kirinmastering.hypha.pre',
      version: '1.1.49',
    }),
    /AAX bundle missing/,
  );
});

test('self-hosted macOS AAX CI uses the Universal build entry point', () => {
  const workflow = fs.readFileSync(path.join(repoRoot, '.github/workflows/aax-phase-a.yml'), 'utf8');
  assert.match(workflow, /scripts\/build_aax_universal\.sh/);
  assert.match(workflow, /x86_64-apple-darwin aarch64-apple-darwin/);
  assert.doesNotMatch(workflow, /Build macOS Universal AAX[\s\S]*--sign/);
});
