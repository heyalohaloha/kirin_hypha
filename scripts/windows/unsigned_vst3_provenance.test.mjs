import { PLUGINVAL_SHA256 } from '../provenance/unsigned_qualification.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { RECORD, parseJuceModuleInfo, verifyUnsignedWindowsHandoff, writeUnsignedWindowsHandoff } from './unsigned_vst3_provenance.mjs';
import { sha256 } from '../provenance/asset_gate.mjs';
import { stageUnsignedWindowsArtifact } from './unsigned_vst3_artifact.mjs';
import { writeSourceZip } from '../provenance/source_zip_writer.mjs';
import { readSourceZip } from '../provenance/source_zip.mjs';

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
      const bundle = `juce_shell/build-windows/KirinHypha${role}_artefacts/Release/VST3/Kirin Hypha ${role}.vst3`;
      return { role, ...put(`${bundle}/Contents/x86_64-win/Kirin Hypha ${role}.vst3`, bytes), moduleInfo: put(`${bundle}/Contents/Resources/moduleinfo.json`, JSON.stringify({ Version: '1.2.3' })) };
    }) };
  const snapshot = record.binaries.map(binary => [
    { path: 'Contents/Resources/moduleinfo.json', sha256: binary.moduleInfo.sha256 },
    { path: `Contents/x86_64-win/Kirin Hypha ${binary.role}.vst3`, sha256: binary.sha256 },
  ]);
  record.pluginval = put('pluginval.json', JSON.stringify({ schema: 'hypha-pluginval-qualification-v1', toolSha256: PLUGINVAL_SHA256,
    before: snapshot, after: snapshot, results: record.binaries.map(b => ({bundle: `Kirin Hypha ${b.role}.vst3`, exitCode: 0})) }));
  record.rawSource = put('raw-source.json', JSON.stringify({ schema: 'hypha-raw-source-v2', commit,
    tree: '1'.repeat(40), juceCommit: '2'.repeat(40), jucePatchedTree: '3'.repeat(40),
    rustc: 'rustc 1.94.1 (e408947bf 2026-03-25)', files: [{ path: 'fixture', blob: '4'.repeat(40), sha256: '5'.repeat(64) }] }));
  const map = role => fs.readFileSync(new URL(`../provenance/fixtures/${role}-native-symbols.map`, import.meta.url), 'utf8');
  record.nativeLinkMap = put('juce_shell/build-windows/link-evidence/KirinHyphaPOST.map', map('post'));
  record.nativePreLinkMap = put('juce_shell/build-windows/link-evidence/KirinHyphaPRE.map', map('pre'));
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

test('import rejects the old noncanonical raw record and extra shipping evidence even if hashed', t => {
  const f = fixture(t);
  const raw = JSON.parse(fs.readFileSync(path.join(f.root, f.record.rawSource.path)));
  f.record.rawSource = f.put('raw-source.json', JSON.stringify({ ...raw, schema: 'hypha-raw-source-v1' })); f.save();
  assert.throws(() => verifyUnsignedWindowsHandoff(f.root, f.expected), /Canonical raw source/);
  f.record.rawSource = f.put('raw-source.json', JSON.stringify(raw)); f.save();
  f.put(path.posix.dirname(f.record.binaries[0].path) + '/PRE.map', 'inert map');
  assert.throws(() => verifyUnsignedWindowsHandoff(f.root, f.expected), /Unexpected VST3 shipping/);
});

test('producer record stages the exact upload tree and both external maps survive ZIP and import', t => {
  const f = fixture(t), staging = path.join(f.root, 'upload'), archive = path.join(f.root, 'artifact.zip');
  f.put('unrecorded-private-file', 'not uploaded');
  const files = stageUnsignedWindowsArtifact(f.root, staging);
  for (const key of ['nativeLinkMap','nativePreLinkMap']) assert.ok(files.some(file => file.path === f.record[key].path));
  assert.ok(!files.some(file => file.path === 'unrecorded-private-file'));
  writeSourceZip(archive, files.map(file => ({ archive: file.path, file: path.join(staging, file.path) })));
  const imported = path.join(f.root, 'imported');
  for (const [relative, bytes] of readSourceZip(archive)) {
    const target = path.join(imported, relative); fs.mkdirSync(path.dirname(target), { recursive: true }); fs.writeFileSync(target, bytes);
  }
  verifyUnsignedWindowsHandoff(imported, f.expected);
  assert.throws(() => stageUnsignedWindowsArtifact(f.root, staging), /already exists/);
  const map = path.join(imported, f.record.nativeLinkMap.path);
  fs.appendFileSync(map, 'changed'); assert.throws(() => verifyUnsignedWindowsHandoff(imported, f.expected), /bytes changed/);
  fs.rmSync(map); assert.throws(() => verifyUnsignedWindowsHandoff(imported, f.expected), /ENOENT/);
});

test('role-specific actual symbol inputs survive producer staging while missing required families fail', t => {
  const f = fixture(t);
  const pre = fs.readFileSync(path.join(f.root, f.record.nativePreLinkMap.path), 'utf8');
  const post = fs.readFileSync(path.join(f.root, f.record.nativeLinkMap.path), 'utf8');
  assert.doesNotMatch(pre, /FLAC__|ogg_stream_|vorbis_/);
  assert.match(post, /FLAC__/);
  f.record.nativeLinkMap = f.put(f.record.nativeLinkMap.path, pre); f.save();
  assert.throws(() => stageUnsignedWindowsArtifact(f.root, path.join(f.root, 'bad-post')), /FLAC.*POST/);
  f.record.nativeLinkMap = f.put(f.record.nativeLinkMap.path, post);
  f.record.nativePreLinkMap = f.put(f.record.nativePreLinkMap.path, pre.replace(/^.*png_.*$/gm, '')); f.save();
  assert.throws(() => verifyUnsignedWindowsHandoff(f.root, f.expected), /PNG.*PRE/);
});

test('actual JUCE PRE/POST moduleinfo keeps its bytes and parses with LF or Windows CRLF', () => {
  for (const role of ['PRE', 'POST']) {
    const bytes = fs.readFileSync(new URL(`./fixtures/juce-moduleinfo-${role}.json5`, import.meta.url));
    const before = sha256(bytes);
    for (const newline of ['\n', '\r\n']) {
      const text = bytes.toString('utf8').replace(/\r\n|\n/g, newline);
      assert.throws(() => JSON.parse(text));
      const info = parseJuceModuleInfo(text);
      assert.equal(info.Name, `Kirin Hypha ${role}`);
      assert.equal(info.Version, '1.1.51');
      assert.equal(info.Classes.length, 3);
      assert.deepEqual(info.Classes[0]['Sub Categories'], ['Fx', 'Analyzer']);
      assert.equal(info['Factory Info'].Flags['Component Non Discardable'], false);
    }
    assert.equal(sha256(bytes), before);
  }
});

test('JUCE metadata parsing preserves quoted punctuation, escapes and ordinary JSON', () => {
  const value = { Version: '1.1.51', text: 'comma,} quote" slash\\ and bracket,]', list: [true, false, null, {}, []] };
  assert.deepEqual(parseJuceModuleInfo(JSON.stringify(value)), value);
  assert.deepEqual(parseJuceModuleInfo('{"Version":"1.1.51","text":"comma,} bracket,]","list":[1,{"x":2,},],}'),
    { Version: '1.1.51', text: 'comma,} bracket,]', list: [1, { x: 2 }] });
  for (const suffix of ['"', '\\', '\\"', '\\\\"']) {
    const quoted = { text: `comma,} escape${suffix}` };
    assert.deepEqual(parseJuceModuleInfo(JSON.stringify(quoted).slice(0, -1) + ',}'), quoted);
  }
});

test('JUCE metadata parsing rejects malformed input rather than repairing missing values', () => {
  for (const text of ['{,}', '{"a":[,]}', '{"a":,}', '{"a":1,,}', '{"a":[1,,]}',
    '{"a":"unterminated}', '{"a":true false,}', '{"a":1', '{"a":1} extra',
    '{/*comment*/"a":1}', '{bare:1}', 'null', '[]', '42']) {
    assert.throws(() => parseJuceModuleInfo(text), /Invalid JUCE moduleinfo/, text);
  }
  assert.throws(() => parseJuceModuleInfo(null), /text required/);
});

test('actual JUCE metadata survives exact staging while version and hash mismatches still fail', t => {
  const f = fixture(t);
  f.record.version = '1.1.51';
  const qualification = JSON.parse(fs.readFileSync(path.join(f.root, f.record.pluginval.path), 'utf8'));
  f.record.binaries.forEach((binary, index) => {
    const bytes = fs.readFileSync(new URL(`./fixtures/juce-moduleinfo-${binary.role}.json5`, import.meta.url));
    binary.moduleInfo = f.put(binary.moduleInfo.path, bytes);
    for (const phase of ['before', 'after']) {
      qualification[phase][index].find(file => file.path === 'Contents/Resources/moduleinfo.json').sha256 = binary.moduleInfo.sha256;
    }
  });
  f.record.pluginval = f.put(f.record.pluginval.path, JSON.stringify(qualification)); f.save();
  verifyUnsignedWindowsHandoff(f.root, f.expected);
  const staging = path.join(f.root, 'actual-metadata-upload');
  stageUnsignedWindowsArtifact(f.root, staging);
  for (const binary of f.record.binaries) assert.equal(sha256(fs.readFileSync(path.join(staging, binary.moduleInfo.path))), binary.moduleInfo.sha256);
  f.record.version = '1.1.50'; f.save();
  assert.throws(() => verifyUnsignedWindowsHandoff(f.root, f.expected), /version mismatch/);
  f.record.version = '1.1.51'; f.save();
  fs.appendFileSync(path.join(f.root, f.record.binaries[0].moduleInfo.path), ' ');
  assert.throws(() => verifyUnsignedWindowsHandoff(f.root, f.expected), /bytes changed/);
});
