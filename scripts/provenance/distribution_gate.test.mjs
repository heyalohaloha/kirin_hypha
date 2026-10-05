import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import zlib from 'node:zlib';
import { assetDecision, embeddedAssets, rejectHeldBytes, REGISTRY, sha256 } from './asset_gate.mjs';
import { verifyDistributionEvidence } from './distribution_gate.mjs';
import { fileFact, Checkpoint } from '../ls_release/hypha_release_contract.mjs';

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function writeZip(file, entries) {
  const local = []; const central = []; let offset = 0;
  for (const [name, bytes] of entries) {
    const filename = Buffer.from(name); const compressed = zlib.deflateRawSync(bytes); const crc = crc32(bytes);
    const header = Buffer.alloc(30); header.writeUInt32LE(0x04034b50); header.writeUInt16LE(20, 4);
    header.writeUInt16LE(0x800, 6); header.writeUInt16LE(8, 8); header.writeUInt32LE(crc, 14);
    header.writeUInt32LE(compressed.length, 18); header.writeUInt32LE(bytes.length, 22); header.writeUInt16LE(filename.length, 26);
    const record = Buffer.alloc(46); record.writeUInt32LE(0x02014b50); record.writeUInt16LE(20, 4);
    record.writeUInt16LE(20, 6); record.writeUInt16LE(0x800, 8); record.writeUInt16LE(8, 10);
    record.writeUInt32LE(crc, 16); record.writeUInt32LE(compressed.length, 20); record.writeUInt32LE(bytes.length, 24);
    record.writeUInt16LE(filename.length, 28); record.writeUInt32LE(offset, 42);
    local.push(header, filename, compressed); central.push(record, filename);
    offset += header.length + filename.length + compressed.length;
  }
  const directory = Buffer.concat(central); const end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50);
  end.writeUInt16LE(entries.size, 8); end.writeUInt16LE(entries.size, 10); end.writeUInt32LE(directory.length, 12); end.writeUInt32LE(offset, 16);
  fs.writeFileSync(file, Buffer.concat([...local, directory, end]));
}
function put(root, name, bytes) {
  fs.mkdirSync(path.dirname(path.join(root, name)), { recursive: true }); fs.writeFileSync(path.join(root, name), bytes);
}
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-provenance-fixture-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const sources = new Map([['LICENSE', Buffer.from('GPL fixture')], ['THIRD_PARTY_NOTICES.md', Buffer.from('notice fixture')],
    ['THIRD_PARTY_LICENSES/MoSQITo-1.2.1-Apache-2.0.txt', Buffer.from('Apache fixture')], ['source.rs', Buffer.from('own fixture code')]]);
  for (const [name, bytes] of sources) put(root, name, bytes);
  put(root, 'juce_shell/CMakeLists.txt', 'set(KIRIN_HYPHA_DATA_SOURCES\n ${CMAKE_CURRENT_SOURCE_DIR}/../asset.png)\n');
  put(root, 'asset.png', 'own disposable image fixture');
  const registry = { schema: 'hypha-asset-distribution-registry-v1',
    embedContractSha256: fileFact(path.join(root, 'juce_shell/CMakeLists.txt')).sha256, assets: [{ path: 'asset.png',
    sha256: sha256(fs.readFileSync(path.join(root, 'asset.png'))), status: 'verified',
    rightsEvidence: 'test-only fixture creation', allowedUses: ['binary'] }] };
  put(root, REGISTRY, JSON.stringify(registry));
  const archive = path.join(root, 'source.zip'); writeZip(archive, sources);
  const report = { schema: 'hypha-distribution-provenance-v1', commit: 'a'.repeat(40), kimeraEmbedded: false,
    releaseTag: 'v1.2.3', sourceArchive: archive, sourceSha256: fileFact(archive).sha256,
    sourceDownload: 'https://github.com/heyalohaloha/kirin_hypha/releases/download/v1.2.3/source.zip',
    components: [{ id: 'MoSQITo@1.2.1', license: 'Apache-2.0', source: 'public fixture upstream',
      modificationNotice: 'Rust adaptation fixture', licenseFiles: ['THIRD_PARTY_LICENSES/MoSQITo-1.2.1-Apache-2.0.txt'] },
    { id: 'JUCE@7.0.12', license: 'GPL-3.0', source: 'pinned fixture', modificationNotice: 'tracked patches fixture', licenseFiles: ['LICENSE'] }], payloads: [] };
  const artifacts = [];
  for (const channel of ['macos-pkg', 'macos-zip', 'windows-exe']) {
    const artifact = path.join(root, `${channel}.fixture`); put(root, `${channel}.fixture`, channel);
    const extracted = path.join(root, channel); const legalFiles = {};
    for (const [name, bytes] of sources) if (name !== 'source.rs') { put(extracted, name, bytes); legalFiles[name] = name; }
    put(extracted, 'binary.fixture', 'inert extracted binary fixture');
    put(extracted, 'source.json', JSON.stringify({ commit: report.commit, sha256: report.sourceSha256, url: report.sourceDownload }));
    const evidence = path.join(root, `${channel}-extraction.json`);
    const payloadFiles = [...Object.keys(legalFiles), 'source.json', 'binary.fixture'].sort()
      .map(p => ({ path: p, sha256: fileFact(path.join(extracted, p)).sha256 }));
    put(root, path.basename(evidence), JSON.stringify({ schema: 'hypha-extraction-evidence-v1',
      artifactSha256: fileFact(artifact).sha256, extractor: 'disposable fixture, no production extraction',
      exitCode: 0, reviewer: 'independent disposable fixture', command: ['fixture-extract'], payloadFiles }));
    artifacts.push(fileFact(artifact));
    report.payloads.push({ channel, artifactSha256: fileFact(artifact).sha256, root: extracted,
      extractionResult: 'PASS', extractor: 'disposable fixture, no production extraction', extractionEvidence: evidence,
      extractionEvidenceSha256: fileFact(evidence).sha256, legalFiles, sourceDeliveryFile: 'source.json',
      binaryFiles: [{ path: 'binary.fixture', sha256: fileFact(path.join(extracted, 'binary.fixture')).sha256 }] });
    const cache = path.join(root, `${channel}-CMakeCache.txt`); put(root, path.basename(cache), 'KIRIN_HYPHA_KIMERA_FONT_FILE:FILEPATH=\n');
    const material = path.join(root, `${channel}-build.json`);
    put(root, path.basename(material), JSON.stringify({ schema: 'hypha-build-material-evidence-v1', commit: report.commit,
      reviewer: 'independent disposable fixture', binaryFiles: report.payloads.at(-1).binaryFiles,
      cmakeCaches: [fileFact(cache)] }));
    report.payloads.at(-1).buildMaterialEvidence = material;
    report.payloads.at(-1).buildMaterialEvidenceSha256 = fileFact(material).sha256;
  }
  const requiredFiles = [...sources.keys()].map(archive => ({ archive, file: path.join(root, archive) }));
  const args = { root, commit: report.commit, releaseTag: 'v1.2.3', artifacts, report, requirements: () => ({ files: requiredFiles, packages: [] }) };
  return { root, report, registry, sources, archive, args };
}

test('all three exact payloads deliver notices, source pointer and matching source bytes', t => {
  const f = fixture(t); const result = verifyDistributionEvidence(f.args);
  assert.equal(result.publicFacts.length, 1); assert.equal(result.publicFacts[0].sha256, f.report.sourceSha256);
  assert.equal(result.facts.length, 31);
});
test('asset holds block affected binary distribution but unrelated fixture has no global hold', t => {
  const f = fixture(t); assert.equal(assetDecision(f.root, embeddedAssets(f.root)).approved, true);
  f.registry.assets[0].status = 'hold'; put(f.root, REGISTRY, JSON.stringify(f.registry));
  assert.throws(() => verifyDistributionEvidence(f.args), Checkpoint);
  assert.equal(assetDecision(f.root, []).approved, true);
  const bytes = fs.readFileSync(path.join(f.root, 'asset.png'));
  assert.throws(() => rejectHeldBytes(f.registry, new Map([['renamed.png', bytes]])), /Held asset/);
});
test('changed asset hash and undocumented embedding never inherit permission', t => {
  const f = fixture(t); put(f.root, 'asset.png', 'changed fixture');
  assert.equal(assetDecision(f.root, embeddedAssets(f.root)).approved, false);
  put(f.root, 'juce_shell/CMakeLists.txt', 'set(KIRIN_HYPHA_DATA_SOURCES ${NEW_UNREVIEWED_INPUT})');
  assert.throws(() => embeddedAssets(f.root), /contract changed/);
});
test('additional embedding outside the source set requires a new reviewed build contract', t => {
  const f = fixture(t);
  fs.appendFileSync(path.join(f.root, 'juce_shell/CMakeLists.txt'), '\nlist(APPEND KIRIN_HYPHA_DATA_SOURCES new-unknown.png)\n');
  assert.throws(() => embeddedAssets(f.root), /contract changed/);
});
test('binary input permission does not grant rendered preview permission', t => {
  const f = fixture(t);
  assert.equal(assetDecision(f.root, embeddedAssets(f.root), 'binary').approved, true);
  assert.equal(assetDecision(f.root, embeddedAssets(f.root), 'preview').approved, false);
});
test('binary-only permission does not grant raw source redistribution, nor string-substring grants', t => {
  const f = fixture(t); const bytes = fs.readFileSync(path.join(f.root, 'asset.png'));
  assert.throws(() => rejectHeldBytes(f.registry, new Map([['renamed.png', bytes]]), 'source'), /Held asset/);
  f.registry.assets[0].allowedUses = 'not-for-binary'; put(f.root, REGISTRY, JSON.stringify(f.registry));
  assert.throws(() => assetDecision(f.root, ['asset.png'], 'binary'), /rights evidence and allowed uses/);
});
test('absent or incorrect notice, component license, source pointer and extraction fail', t => {
  for (const kind of ['notice', 'license', 'delivery', 'extraction', 'channel', 'font']) {
    const f = fixture(t); const p = f.report.payloads[0];
    if (kind === 'notice') put(p.root, 'THIRD_PARTY_NOTICES.md', 'wrong');
    if (kind === 'license') f.report.components[0].license = 'MIT';
    if (kind === 'delivery') put(p.root, 'source.json', '{}');
    if (kind === 'extraction') p.extractionResult = 'SKIP';
    if (kind === 'channel') f.report.payloads.pop();
    if (kind === 'font') f.report.kimeraEmbedded = true;
    assert.throws(() => verifyDistributionEvidence(f.args), kind === 'font' ? Checkpoint : Error, kind);
  }
});
test('source archive omission, changed bytes and private extras are rejected', t => {
  for (const kind of ['missing', 'changed', 'extra']) {
    const f = fixture(t);
    if (kind === 'missing') f.sources.delete('source.rs');
    if (kind === 'changed') f.sources.set('source.rs', Buffer.from('wrong code'));
    if (kind === 'extra') f.sources.set('private-audit.txt', Buffer.from('not part of reviewed public source'));
    writeZip(f.archive, f.sources); f.report.sourceSha256 = fileFact(f.archive).sha256;
    assert.throws(() => verifyDistributionEvidence(f.args), /Corresponding Source/);
  }
});
test('old commit, wrong release, archive hash, traversal and mutated binary fail before acceptance', t => {
  for (const kind of ['commit', 'release', 'hash', 'traversal', 'binary']) {
    const f = fixture(t);
    if (kind === 'commit') f.report.commit = 'f'.repeat(40);
    if (kind === 'release') {
      f.report.releaseTag = 'v9.9.9';
      f.report.sourceDownload = f.report.sourceDownload.replace('v1.2.3', 'v9.9.9');
    }
    if (kind === 'hash') f.report.sourceSha256 = '0'.repeat(64);
    if (kind === 'traversal') f.report.payloads[0].legalFiles.LICENSE = '../LICENSE';
    if (kind === 'binary') put(f.report.payloads[0].root, 'binary.fixture', 'tampered');
    assert.throws(() => verifyDistributionEvidence(f.args), /mismatch|Invalid/);
  }
});
test('font declaration cannot override the retained actual producer build configuration', t => {
  const f = fixture(t); const p = f.report.payloads[0];
  const material = JSON.parse(fs.readFileSync(p.buildMaterialEvidence, 'utf8'));
  put(f.root, path.basename(material.cmakeCaches[0].path), 'KIRIN_HYPHA_KIMERA_FONT_FILE:FILEPATH=external-font.otf\n');
  material.cmakeCaches[0] = fileFact(material.cmakeCaches[0].path);
  put(f.root, path.basename(p.buildMaterialEvidence), JSON.stringify(material));
  p.buildMaterialEvidenceSha256 = fileFact(p.buildMaterialEvidence).sha256;
  assert.throws(() => verifyDistributionEvidence(f.args), /no-font build/);
});
