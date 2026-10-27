import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import * as release from '../release_hypha.mjs';
import { assertState, atomicJson, fileFact } from './hypha_release_contract.mjs';
import { verifyWindows } from './hypha_release_local.mjs';

function fixture(t, kind = 'normal') {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-delivery-')));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), 'version = "1.2.3"\n');
  const source = { commit: 'a'.repeat(40), bNumber: 'B-1', state: 'clean source', fingerprint: 'b'.repeat(64) };
  const snapshot = () => source, statePath = path.join(root, 'release_state/state.json');
  const state = release.makeState(root, { state: statePath, windowsFactoryRepository: 'fixture/factory', windowsFactoryCommit: 'c'.repeat(40) }, snapshot);
  atomicJson(statePath, state);
  const archive = path.join(root, 'delivery.zip'), destination = path.join(root, 'release_state', state.candidate.id, 'windows-delivery');
  execFileSync('python3', ['-c', `import zipfile,sys\nz=zipfile.ZipFile(sys.argv[1],'w')\nk=sys.argv[2]\nif k=='traversal': z.writestr('../escape','bad')\nelif k=='symlink':\n i=zipfile.ZipInfo('link');i.external_attr=0o120777<<16;z.writestr(i,'target')\nelse: z.writestr('payload/file','signed bytes')\nz.close()`, archive, kind]);
  const options = { bindWindowsPromotion: '123', windowsArtifactArchive: archive, windowsInstallerDir: destination };
  const record = { id: 123, head_sha: 'c'.repeat(40), head_repository: { full_name: 'fixture/factory' }, path: '.github/workflows/hypha-windows-signing.yml', name: 'Hypha Windows signing factory', event: 'workflow_dispatch', status: 'completed', conclusion: 'success' };
  const jobs = ['Hypha unsigned Windows VST3', 'Hypha signed Windows installer', 'Hypha validated Windows installer promotion'].map((name, i) => ({ name, status: 'completed', conclusion: i === 2 ? 'success' : 'skipped' }));
  const artifact = { name: 'KirinHypha-Windows-signed-full', id: 55, expired: false, digest: `sha256:${fileFact(archive).sha256}`, workflow_run: { id: 123, head_sha: record.head_sha } };
  const run = async (tool, args) => { assert.equal(tool, 'gh'); return JSON.stringify(args[1].includes('/jobs?') ? { jobs } : args[1].includes('/artifacts?') ? { artifacts: [artifact] } : record); };
  return { statePath, state, options, snapshot, destination, run, root };
}

test('promotion binding verifies API digest and extracts into a previously absent dedicated directory', async t => {
  const f = fixture(t); assert.equal(fs.existsSync(f.destination), false);
  const state = await release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.run });
  assert.equal(state.inputs.windowsInstallerDir, f.destination);
  assert.equal(fs.readFileSync(path.join(f.destination, 'payload/file'), 'utf8'), 'signed bytes');
});
for (const kind of ['traversal', 'symlink']) test(`promotion binding rejects ${kind} ZIP`, async t => {
  const f = fixture(t, kind), before = fs.readFileSync(f.statePath);
  await assert.rejects(async () => release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.run }), /unsafe|zip/i);
  assert.deepEqual(fs.readFileSync(f.statePath), before); assert.equal(fs.existsSync(f.destination), false);
});
test('promotion binding refuses to merge an existing different tree', async t => {
  const f = fixture(t); fs.mkdirSync(f.destination, { recursive: true }); fs.writeFileSync(path.join(f.destination, 'foreign'), 'keep');
  const before = fs.readFileSync(f.statePath);
  await assert.rejects(async () => release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.run }), /different|existing|artifact/i);
  assert.equal(fs.readFileSync(path.join(f.destination, 'foreign'), 'utf8'), 'keep');
  assert.deepEqual(fs.readFileSync(f.statePath), before);
});

test('a mismatched API digest is rejected before extraction and leaves the profile unchanged', async t => {
  const f = fixture(t), before = fs.readFileSync(f.statePath);
  const badRun = async (tool, args) => {
    const data = JSON.parse(await f.run(tool, args));
    if (data.artifacts) data.artifacts[0].digest = `sha256:${'0'.repeat(64)}`;
    return JSON.stringify(data);
  };
  await assert.rejects(() => release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: badRun }), /digest/i);
  assert.equal(fs.existsSync(f.destination), false); assert.deepEqual(fs.readFileSync(f.statePath), before);
});

test('an identical existing delivery is rechecked and reused without replacement', async t => {
  const f = fixture(t); fs.mkdirSync(path.join(f.destination, 'payload'), { recursive: true });
  const file = path.join(f.destination, 'payload/file'); fs.writeFileSync(file, 'signed bytes');
  const inode = fs.statSync(file).ino;
  await release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.run });
  assert.equal(fs.statSync(file).ino, inode); assert.equal(fs.readFileSync(file, 'utf8'), 'signed bytes');
});

test('a previous installer path and profile are retained without mixing the old tree', async t => {
  const f = fixture(t), old = path.join(f.root, 'old-delivery'); fs.mkdirSync(old); fs.writeFileSync(path.join(old, 'keep'), 'old');
  f.state.inputs.windowsInstallerDir = old; atomicJson(f.statePath, f.state);
  const before = fs.readFileSync(f.statePath);
  const result = await release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.run });
  assert.equal(result.windowsDeliveryBinding.previousInstallerDir, old);
  assert.deepEqual(fs.readFileSync(result.windowsDeliveryBinding.previousProfile.path), before);
  assert.equal(fs.readFileSync(path.join(old, 'keep'), 'utf8'), 'old');
  assert.equal(result.inputs.windowsInstallerDir, f.destination);
});

test('binding refuses profile changes while it awaits API data', async t => {
  const f = fixture(t); let changed = false;
  const run = async (tool, args) => {
    const value = await f.run(tool, args);
    if (!changed) { changed = true; atomicJson(f.statePath, { ...f.state, note: 'concurrent change' }); }
    return value;
  };
  await assert.rejects(() => release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run }), /profile changed/i);
  assert.equal(JSON.parse(fs.readFileSync(f.statePath)).note, 'concurrent change');
  assert.equal(fs.existsSync(f.destination), false);
});

test('existing delivery with different file permissions is not merged', async t => {
  const f = fixture(t); fs.mkdirSync(path.join(f.destination, 'payload'), { recursive: true });
  const file = path.join(f.destination, 'payload/file'); fs.writeFileSync(file, 'signed bytes');
  const differentMode = (0o666 & ~process.umask()) ^ 0o100; fs.chmodSync(file, differentMode);
  const before = fs.readFileSync(f.statePath);
  await assert.rejects(() => release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.run }), /permissions/i);
  assert.equal(fs.statSync(file).mode & 0o777, differentMode); assert.deepEqual(fs.readFileSync(f.statePath), before);
});


test('a bound delivery tree is rechecked before subsequent operations', async t => {
  const f = fixture(t);
  const result = await release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.run });
  assertState(result); fs.writeFileSync(path.join(f.destination, 'payload/file'), 'changed bytes');
  assert.throws(() => assertState(result), /changed/i);
});


test('an existing delivery with an external hardlink is refused without altering either link', async t => {
  const f = fixture(t), outside = path.join(f.root, 'external');
  fs.mkdirSync(path.join(f.destination, 'payload'), { recursive: true }); fs.writeFileSync(outside, 'signed bytes');
  const delivery = path.join(f.destination, 'payload/file'); fs.linkSync(outside, delivery);
  const inode = fs.statSync(outside).ino, before = fs.readFileSync(f.statePath);
  await assert.rejects(release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.run }), /hardlink/i);
  assert.equal(fs.statSync(outside).ino, inode); assert.equal(fs.statSync(delivery).ino, inode);
  assert.equal(fs.statSync(outside).nlink, 2); assert.equal(fs.readFileSync(outside, 'utf8'), 'signed bytes');
  assert.deepEqual(fs.readFileSync(f.statePath), before);
});

test('a hardlink added after binding is rejected by state and Windows verification', async t => {
  const f = fixture(t), state = await release.bindWindowsPromotion(f.statePath, f.options, f.snapshot, { run: f.run });
  const file = path.join(f.destination, 'payload/file'), outside = path.join(f.root, 'external');
  fs.linkSync(file, outside); const inode = fs.statSync(file).ino;
  assert.throws(() => assertState(state), /hardlink/i);
  await assert.rejects(verifyWindows(state, f.run), /hardlink/i);
  assert.equal(fs.statSync(file).ino, inode); assert.equal(fs.statSync(outside).ino, inode);
  assert.equal(fs.statSync(outside).nlink, 2);
});
