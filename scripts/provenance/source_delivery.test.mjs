import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { prepareSourceDelivery } from './source_delivery.mjs';
import { writeSourceZip } from './source_zip_writer.mjs';
import { readSourceZip } from './source_zip.mjs';
import { stageLegalDelivery, verifyLegalDelivery } from './legal_delivery.mjs';
import { REGISTRY, sha256 } from './asset_gate.mjs';

const commit = 'a'.repeat(40);
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-source-delivery-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const put = (name, text) => {
    const file = path.join(root, name); fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text); return file;
  };
  const names = ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_LICENSES/MoSQITo-1.2.1-Apache-2.0.txt', 'source.rs'];
  const files = names.map(archive => ({ archive, file: put(archive, `fixture bytes: ${archive}`) }));
  const cmake = put('juce_shell/CMakeLists.txt', 'set(KIRIN_HYPHA_DATA_SOURCES\n ${CMAKE_CURRENT_SOURCE_DIR}/../asset.png)\n');
  const asset = put('asset.png', 'owned inert fixture');
  put(REGISTRY, JSON.stringify({ schema: 'hypha-asset-distribution-registry-v1',
    embedContractSha256: sha256(fs.readFileSync(cmake)), assets: [{ path: 'asset.png',
      sha256: sha256(fs.readFileSync(asset)), status: 'verified', rightsEvidence: 'disposable fixture', allowedUses: ['binary', 'source'] }] }));
  const components = ['MoSQITo@1.2.1', 'JUCE@7.0.12'].map(id => ({ id,
    license: id.startsWith('MoSQITo') ? 'Apache-2.0' : 'GPL-3.0', source: 'inert fixture upstream',
    modificationNotice: 'inert fixture only', licenseFiles: [names[0]] }));
  const args = { root, commit, version: '1.2.3', reportPath: path.join(root, 'private/report.json'),
    directory: path.join(root, 'private/source'), requirements: () => ({ files, packages: [] }),
    checkSource: () => ({ commit }),
    componentInventory: () => components };
  return { root, put, files, components, args };
}

test('source ZIP round trips binary/UTF-8 bytes deterministically and preserves existing output', t => {
  const f = fixture(t); f.files.push({ archive: '日本語.bin', file: f.put('utf8.bin', Buffer.from([0, 255, 3])) });
  const a = path.join(f.root, 'a.zip'), b = path.join(f.root, 'b.zip');
  writeSourceZip(a, f.files); writeSourceZip(b, [...f.files].reverse());
  assert.deepEqual(fs.readFileSync(a), fs.readFileSync(b));
  assert.deepEqual(readSourceZip(a).get('日本語.bin'), Buffer.from([0, 255, 3]));
  assert.throws(() => writeSourceZip(a, f.files), /EEXIST/);
});

test('unsafe and duplicate ZIP paths fail without leaving a partial output', t => {
  const f = fixture(t);
  for (const archive of ['../secret', '/secret', 'C:/secret', 'a\\secret', 'a//secret']) {
    const file = path.join(f.root, 'bad.zip');
    assert.throws(() => writeSourceZip(file, [{ ...f.files[0], archive }]), /Unsafe/);
    assert.equal(fs.existsSync(file), false);
  }
  assert.throws(() => writeSourceZip(path.join(f.root, 'duplicate.zip'), [f.files[0], f.files[0]]), /duplicate/);
});

test('source/legal staging binds one candidate, keeps signed bundles unchanged and preserves changed destination', t => {
  const f = fixture(t); const result = prepareSourceDelivery(f.args);
  const report = JSON.parse(fs.readFileSync(f.args.reportPath));
  assert.deepEqual(report.payloads, [], 'source preparation must not fabricate extraction acceptance');
  const target = path.join(f.root, 'payload/Legal');
  const plugin = f.put('payload/plugin.bin', 'signed inert fixture');
  const before = fs.readFileSync(plugin);
  stageLegalDelivery(result.legalDir, target, commit);
  stageLegalDelivery(result.legalDir, target, commit);
  assert.deepEqual(fs.readFileSync(plugin), before);
  assert.equal(verifyLegalDelivery(target, commit).sourceSha256, result.facts[0].sha256);
  assert.throws(() => verifyLegalDelivery(target, 'b'.repeat(40)), /identity/);
  fs.writeFileSync(path.join(target, 'LICENSE'), 'different previous candidate');
  assert.throws(() => stageLegalDelivery(result.legalDir, target, commit), /overwrite/);
  assert.equal(fs.readFileSync(path.join(target, 'LICENSE'), 'utf8'), 'different previous candidate');
});

test('source reuse rejects changed source, missing components and unresolved grants', t => {
  const f = fixture(t); prepareSourceDelivery(f.args);
  f.put('source.rs', 'changed');
  assert.throws(() => prepareSourceDelivery(f.args), /no longer matches/);
  fs.rmSync(f.args.reportPath); fs.rmSync(f.args.directory, { recursive: true });
  f.components.pop();
  assert.throws(() => prepareSourceDelivery(f.args), /JUCE/);
  f.components[0].license = 'NOASSERTION';
  assert.throws(() => prepareSourceDelivery(f.args), /unresolved/);
});

test('importing a canonical Windows source ZIP preserves its bytes and rejects another source', t => {
  const f = fixture(t); const a = prepareSourceDelivery(f.args);
  const imported = prepareSourceDelivery({ ...f.args, directory: path.join(f.root, 'imported'), archiveInput: a.sourceArchive });
  assert.equal(imported.facts[0].sha256, a.facts[0].sha256);
  f.put('source.rs', 'changed');
  assert.throws(() => prepareSourceDelivery({ ...f.args, directory: path.join(f.root, 'wrong'), archiveInput: a.sourceArchive }), /Imported source/);
});

test('legal manifest and destination symlinks are rejected', t => {
  const f = fixture(t); const { legalDir } = prepareSourceDelivery(f.args);
  const actual = path.join(legalDir, 'legal-delivery.json');
  const other = f.put('other.json', fs.readFileSync(actual));
  fs.rmSync(actual); fs.symlinkSync(other, actual);
  assert.throws(() => verifyLegalDelivery(legalDir, commit), /symlink/);
});
