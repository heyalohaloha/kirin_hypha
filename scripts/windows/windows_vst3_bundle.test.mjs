import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { qualifiedWindowsVst3Files } from './windows_vst3_bundle.mjs';
import { bundleRecord, VERSION } from './build-installer.mjs';

test('shipping bundle allows only payload, metadata and a complete optional icon pair', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-shipping-set-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const bundle = path.join(root, 'Kirin Hypha PRE.vst3');
  const put = (name, bytes) => {
    const file = path.join(bundle, name); fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, bytes); return file;
  };
  const binary = put('Contents/x86_64-win/Kirin Hypha PRE.vst3', 'inert DLL');
  const info = put('Contents/Resources/moduleinfo.json', JSON.stringify({ Version: VERSION }, null, 2));
  assert.equal(qualifiedWindowsVst3Files(bundle, 'PRE').length, 2); bundleRecord(root, 'PRE');
  const icon = put('Plugin.ico', 'inert icon');
  assert.throws(() => qualifiedWindowsVst3Files(bundle, 'PRE'), /Incomplete/);
  put('desktop.ini', '[.ShellClassInfo]'); assert.equal(qualifiedWindowsVst3Files(bundle, 'PRE').length, 4);
  for (const name of ['Contents/x86_64-win/PRE.map', 'Contents/Resources/PRE.pdb', 'readme.txt']) {
    const extra = put(name, 'evidence belongs outside the bundle');
    assert.throws(() => qualifiedWindowsVst3Files(bundle, 'PRE'), /Unexpected/);
    assert.throws(() => bundleRecord(root, 'PRE'), /Unexpected/); fs.rmSync(extra);
  }
  fs.rmSync(info); assert.throws(() => qualifiedWindowsVst3Files(bundle, 'PRE'), /Missing/);
  put('Contents/Resources/moduleinfo.json', '{}');
  fs.writeFileSync(binary, ''); assert.throws(() => qualifiedWindowsVst3Files(bundle, 'PRE'), /Empty/);
  fs.writeFileSync(binary, 'inert'); fs.rmSync(icon); fs.symlinkSync(binary, icon);
  assert.throws(() => qualifiedWindowsVst3Files(bundle, 'PRE'), /Unexpected/);
});
