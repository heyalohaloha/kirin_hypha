import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { contentManifest, isDaw, preflight } from './host_preflight.mjs';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-host-preflight-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const bundle = (base, name) => {
    const dir = path.join(base, name, 'Contents/MacOS'); fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'Kirin Hypha PRE'), 'binary'); fs.symlinkSync('MacOS/Kirin Hypha PRE', path.join(base, name, 'Contents/link'));
    return path.join(base, name);
  };
  const sourcePath = bundle(path.join(root, 'build'), 'Kirin Hypha PRE.vst3');
  const install = path.join(root, 'install'), home = path.join(root, 'home');
  const installed = bundle(path.join(install, 'Library/Audio/Plug-Ins/VST3'), 'Kirin Hypha PRE.vst3');
  fs.mkdirSync(path.join(home, 'Library/Audio/Plug-Ins/VST3'), { recursive: true });
  const options = { bundles: [{ sourcePath, install_relative: 'Library/Audio/Plug-Ins/VST3/Kirin Hypha PRE.vst3' }],
    installRoot: install, home, processes: ['/sbin/launchd', '/Applications/Finder.app/Contents/MacOS/Finder'], freeBytes: 50e9 };
  return { root, installed, home, options };
}

test('the candidate bytes installed, nothing shadowing, no DAW and room for traces pass', t => {
  const f = fixture(t);
  const rows = preflight(f.options);
  assert.ok(rows.every(row => row.ok), JSON.stringify(rows));
  // Modes and owners are not part of the comparison; bytes and links are.
  fs.chmodSync(path.join(f.installed, 'Contents/MacOS/Kirin Hypha PRE'), 0o755);
  assert.ok(preflight(f.options).every(row => row.ok));
});

test('changed or missing bytes, a user-level copy, a running DAW or little space fail', t => {
  const f = fixture(t);
  fs.writeFileSync(path.join(f.installed, 'Contents/MacOS/Kirin Hypha PRE'), 'older binary');
  assert.match(preflight(f.options).find(row => !row.ok).detail, /differ/);
  fs.rmSync(f.installed, { recursive: true });
  assert.match(preflight(f.options).find(row => !row.ok).detail, /missing/);
  const g = fixture(t);
  fs.mkdirSync(path.join(g.home, 'Library/Audio/Plug-Ins/VST3/Kirin Hypha PDC Validation Delay 4096.vst3'));
  assert.ok(preflight(g.options).every(row => row.ok), 'a validation helper is not a shadowing copy');
  fs.mkdirSync(path.join(g.home, 'Library/Audio/Plug-Ins/VST3/Kirin Hypha PRE.vst3'));
  assert.match(preflight(g.options).find(row => !row.ok).detail, /move aside/);
  const h = fixture(t);
  assert.match(preflight({ ...h.options, processes: ['/Applications/REAPER.app/Contents/MacOS/REAPER'] }).find(row => !row.ok).detail, /REAPER/);
  assert.equal(preflight({ ...h.options, freeBytes: 1e9 }).find(row => !row.ok).check, 'free space for traces');
});

test('DAW names match whole process names, not similar ones', () => {
  for (const name of ['/x/REAPER', '/x/Logic Pro', '/x/Logic Pro X', '/x/Pro Tools', '/x/Studio One 6', '/x/Live']) assert.ok(isDaw(name), name);
  for (const name of ['/x/LiveSync', '/x/reaper-helper', '/x/Pro Toolsmith', '/x/Finder']) assert.ok(!isDaw(name), name);
  assert.deepEqual(contentManifest(fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-empty-'))), []);
});
