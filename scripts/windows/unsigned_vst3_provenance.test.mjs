import { PLUGINVAL_SHA256 } from '../provenance/unsigned_qualification.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { RECORD, verifyUnsignedWindowsHandoff, writeUnsignedWindowsHandoff } from './unsigned_vst3_provenance.mjs';
import { sha256 } from '../provenance/asset_gate.mjs';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-unsigned-provenance-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const put = (relative, bytes) => {
    const file = path.join(root, relative); fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, bytes);
    return { path: relative, sha256: sha256(fs.readFileSync(file)) };
  };
  const commit = 'a'.repeat(40), factoryCommit = 'b'.repeat(40);
  const sourceArchive = put('source.zip', 'opaque inert source fixture');
  const legalFiles = ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_LICENSES/MoSQITo-1.2.1-Apache-2.0.txt']
    .map(name => ({ ...put(`legal/${name}`, 'inert license fixture'), path: name }));
  const delivery = { commit, sha256: sourceArchive.sha256,
    url: 'https://github.com/heyalohaloha/kirin_hypha/releases/download/v1.2.3/source.zip' };
  for (const [name, value] of [['source-delivery.json', delivery], ['component-notices.json', {
    schema: 'hypha-delivered-components-v1', commit, sourceSha256: sourceArchive.sha256, components: [{ id: 'inert-fixture' }] }]]) {
    legalFiles.push({ ...put(`legal/${name}`, JSON.stringify(value)), path: name });
  }
  put('legal/legal-delivery.json', JSON.stringify({ schema: 'hypha-legal-delivery-v1', commit,
    sourceSha256: sourceArchive.sha256, sourceDownload: delivery.url, files: legalFiles }));
  const record = { schema: 'hypha-private-windows-vst3-build-v1', commit, factoryCommit,
    ciRun: '123', factoryRun: '456', bNumber: 'B-1', version: '1.2.3', platform: 'windows-x64',
    updatePublicKey: '', kimeraEmbedded: false, legalDir: 'legal', sourceArchive,
    cmakeCache: put('CMakeCache.txt', 'KIRIN_HYPHA_KIMERA_FONT_FILE:FILEPATH=\n'), ffiArchive: put('ffi.lib', 'inert archive'),
    binaries: ['PRE', 'POST'].map(role => {
      const bytes = Buffer.alloc(256); bytes.write('MZ'); bytes.writeUInt32LE(128, 0x3c);
      bytes.write('PE\0\0', 128); bytes.writeUInt16LE(0x8664, 132);
      bytes.write('KirinHyphaUpdateKeySha256=disabled;', 160);
      return { role, ...put(`bundles/Kirin Hypha ${role}.vst3/Contents/x86_64-win/Kirin Hypha ${role}.vst3`, bytes), moduleInfo: put(`bundles/Kirin Hypha ${role}.vst3/Contents/Resources/moduleinfo.json`, JSON.stringify({ Version: '1.2.3' })) };
    }) };
  const snapshot = record.binaries.map(binary => [
    { path: 'Contents/Resources/moduleinfo.json', sha256: binary.moduleInfo.sha256 },
    { path: `Contents/x86_64-win/Kirin Hypha ${binary.role}.vst3`, sha256: binary.sha256 },
  ]);
  record.pluginval = put('pluginval.json', JSON.stringify({ schema: 'hypha-pluginval-qualification-v1', toolSha256: PLUGINVAL_SHA256,
    before: snapshot, after: snapshot, results: record.binaries.map(b => ({bundle: `Kirin Hypha ${b.role}.vst3`, exitCode: 0})) }));
  record.rawSource = put('raw-source.json', JSON.stringify({ schema: 'hypha-raw-source-v1', files: [] }));
  record.nativeLinkMap = put('POST.map', 'Publics by Value\n FLAC__decode ogg_stream_reset vorbis_info_init jpeg_read png_read inflateEnd\n');
  const save = () => put(RECORD, JSON.stringify(record)); save();
  const expected = { commit, factoryCommit, ciRun: '123', factoryRun: '456' };
  return { root, put, record, save, expected };
}

test('private unsigned handoff binds PRE/POST bytes, exact source CI, factory and legal source pointer', t => {
  const f = fixture(t); assert.equal(verifyUnsignedWindowsHandoff(f.root, f.expected).record.binaries.length, 2);
  for (const field of ['commit', 'factoryCommit', 'ciRun', 'factoryRun']) {
    assert.throws(() => verifyUnsignedWindowsHandoff(f.root, { ...f.expected, [field]: 'wrong' }), /identity/);
  }
  fs.appendFileSync(path.join(f.root, f.record.binaries[0].path), 'altered');
  assert.throws(() => verifyUnsignedWindowsHandoff(f.root, f.expected), /bytes changed/);
});

test('unsigned handoff rejects a missing role, host/font drift, module version and unsafe paths', t => {
  const f = fixture(t); f.record.binaries[1].role = 'PRE'; f.save();
  assert.throws(() => verifyUnsignedWindowsHandoff(f.root, f.expected), /Unique/);
  f.record.binaries[1].role = 'POST';
  f.record.cmakeCache = f.put('CMakeCache.txt', 'KIRIN_HYPHA_KIMERA_FONT_FILE:FILEPATH=unreviewed.otf\n'); f.save();
  assert.throws(() => verifyUnsignedWindowsHandoff(f.root, f.expected), /font/);
  f.record.cmakeCache = f.put('CMakeCache.txt', 'KIRIN_HYPHA_KIMERA_FONT_FILE:FILEPATH=\n');
  f.record.binaries[0].moduleInfo = f.put('PRE.json', JSON.stringify({ Version: '1.2.2' })); f.save();
  assert.throws(() => verifyUnsignedWindowsHandoff(f.root, f.expected), /version/);
  f.record.binaries[0].path = '../outside'; f.save();
  assert.throws(() => verifyUnsignedWindowsHandoff(f.root, f.expected), /Unsafe/);
});

test('writing producer receipts cannot run on the Mac or outside the private Windows workflow', () => {
  if (process.platform !== 'win32') assert.throws(() => writeUnsignedWindowsHandoff('123'), /private Windows/);
});
