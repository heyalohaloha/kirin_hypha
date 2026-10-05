import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { makeState, STAGES } from '../release_hypha.mjs';
import { artifactPaths } from '../ls_release/hypha_release_local.mjs';
import { fileFact, digest, hash, DISTRIBUTION_TESTS, POST_TESTS, reportTemplate } from '../ls_release/hypha_release_contract.mjs';
import { MAX_BYTES, MAX_LIFETIME, jucePublicKey, signManifest, verifyWire, validSemVer,
  validatePayload, payloadFromCompletedRelease, runCli, assertReleaseSigner } from './update_manifest.mjs';
import { updateBinding } from './update_key_binding.mjs';

const now = 1791158400;
const pair = crypto.generateKeyPairSync('rsa', { modulusLength: 2048, publicExponent: 65537 });
const base = () => ({ schema: 1, product: 'kirin-hypha', channel: 'stable', version: '1.1.51',
  source_commit: 'a'.repeat(40), published_at: now, expires_at: now + 86400,
  publication_sequence: 1, withdrawn: false, platforms: ['macos', 'windows'], formats: ['AU', 'VST3'] });

test('fresh disposable RSA key signs one bounded envelope, verified independently by Node', () => {
  const wire = signManifest(base(), pair.privateKey, now);
  assert.deepEqual(verifyWire(wire, pair.publicKey, now), base());
  assert.match(jucePublicKey(pair.publicKey), /^10001,[89a-f][0-9a-f]{511}$/);
  const e = JSON.parse(wire);
  assert.equal(Buffer.from(e.signature, 'base64').length, 256);
  assert.ok(Buffer.byteLength(wire) < MAX_BYTES);
  assert.equal(crypto.verify('sha256', Buffer.from(e.payload, 'base64'),
    { key: pair.publicKey, padding: crypto.constants.RSA_PKCS1_PADDING }, Buffer.from(e.signature, 'base64')), true);
});

test('tampering, wrong key, truncated signature, malformed/noncanonical base64 and oversized wire fail', () => {
  const wire = signManifest(base(), pair.privateKey, now), e = JSON.parse(wire);
  const other = crypto.generateKeyPairSync('rsa', { modulusLength: 2048, publicExponent: 65537 });
  assert.throws(() => verifyWire(wire, other.publicKey, now), /signature/);
  assert.throws(() => verifyWire('x'.repeat(MAX_BYTES + 1), pair.publicKey, now), /Oversized/);
  for (const changed of [{ ...e, payload: Buffer.from('changed').toString('base64') },
    { ...e, signature: Buffer.alloc(255).toString('base64') }, { ...e, signature: `${e.signature}\n` },
    { ...e, payload: '*===' }, { ...e, unknown: true }]) {
    assert.throws(() => verifyWire(JSON.stringify(changed), pair.publicKey, now));
  }
});

test('strict schema, stable semver, safe integer bounds, time and platform limits', () => {
  for (const value of ['1.2.3', '0.0.0', '2147483647.1.0']) assert.equal(validSemVer(value), true);
  for (const value of ['01.2.3', '1.2', '1.2.3-beta', '1.2.3+build', '2147483648.0.0', '1..3', ' 1.2.3'])
    assert.equal(validSemVer(value), false);
  const changes = [{ schema: '1' }, { product: 'other' }, { channel: 'preview' }, { version: '01.2.3' },
    { source_commit: 'A'.repeat(40) }, { published_at: now + 1 }, { expires_at: now },
    { expires_at: now + MAX_LIFETIME + 1 }, { publication_sequence: 0 },
    { publication_sequence: Number.MAX_SAFE_INTEGER + 1 }, { withdrawn: 0 },
    { platforms: [] }, { platforms: ['macos', 'macos'] }, { formats: ['unknown'] },
    { platforms: ['windows'], formats: ['AU'] }, { extra: true }];
  for (const change of changes) assert.throws(() => validatePayload({ ...base(), ...change }, now));
  validatePayload({ ...base(), expires_at: now + MAX_LIFETIME, publication_sequence: Number.MAX_SAFE_INTEGER }, now);
});

test('expired prior signature may be used only to prevent sequence rollback during offline refresh', () => {
  const wire = signManifest(base(), pair.privateKey, now);
  assert.throws(() => verifyWire(wire, pair.publicKey, now + 86400), /payload/);
  assert.deepEqual(verifyWire(wire, pair.publicKey, now + 86400, { allowExpired: true }), base());
});

test('ambiguous signed JSON and RSA-PSS do not pass producer verification', () => {
  const payload = Buffer.from(JSON.stringify(base()).replace('"schema":1', '"schema":1,"schema":1'));
  const sign = padding => JSON.stringify({ payload: payload.toString('base64'),
    signature: crypto.sign('sha256', payload, { key: pair.privateKey, padding }).toString('base64') });
  assert.throws(() => verifyWire(sign(crypto.constants.RSA_PKCS1_PADDING), pair.publicKey, now), /Noncanonical/);
  assert.throws(() => verifyWire(sign(crypto.constants.RSA_PKCS1_PSS_PADDING), pair.publicKey, now), /signature/);
});

test('wrong exponent/size or nonprivate signer fails; CLI cannot silently choose inputs or overwrite', () => {
  const weak = crypto.generateKeyPairSync('rsa', { modulusLength: 1024, publicExponent: 65537 });
  const exponent = crypto.generateKeyPairSync('rsa', { modulusLength: 2048, publicExponent: 3 });
  assert.throws(() => jucePublicKey(weak.publicKey), /RSA-2048/);
  assert.throws(() => jucePublicKey(exponent.publicKey), /RSA-2048/);
  assert.throws(() => signManifest(base(), pair.publicKey, now), /private/);
  assert.throws(() => runCli([]), /Explicit/);
  assert.throws(() => runCli(['--output', 'x', '--output', 'y']), /duplicate/);
  assert.throws(() => payloadFromCompletedRelease({}), /Invalid candidate/);
});

function completeFixture(t) {
  // Synthetic release state and bytes ONLY under an owned temporary directory.
  // Tests exercise the evidence checker, not real signing/host/LS/HP acceptance.
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-update-release-fixture-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'crates/hypha_pre'), { recursive: true });
  fs.writeFileSync(path.join(root, 'crates/hypha_pre/Cargo.toml'), '[package]\nversion = "1.1.51"\n');
  const source = { commit: 'a'.repeat(40), bNumber: 'B-9999', fingerprint: 'b'.repeat(64) };
  const state = makeState(root, { state: 'release_state/release.json', date: '2026-10-05',
    windowsInstallerDir: 'windows', lsState: 'release_state/ls.json', updatePublicKey: jucePublicKey(pair.publicKey) }, () => source);
  const files = artifactPaths(state);
  const binding = ids => ({ ...updateBinding(state.inputs.updatePublicKey), binaries: ids.map(id => ({
    format: id.split('/')[0], role: id.split('/')[1], binarySha256: 'd'.repeat(64),
    publicKeySha256: updateBinding(state.inputs.updatePublicKey).publicKeySha256,
    architectures: ['arm64', 'x86_64'].map(architecture => ({ architecture, sha256: 'c'.repeat(64) })) })) });
  const mac = binding(['AU/PRE', 'AU/POST', 'VST3/PRE', 'VST3/POST', 'AAX/PRE', 'AAX/POST']);
  const win = binding(['VST3/PRE', 'VST3/POST', 'AAX/PRE', 'AAX/POST']);
  for (const file of files) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, 'TEST fixture bytes'); }
  fs.writeFileSync(files[2], JSON.stringify({ version: '1.1.51', source, signed: true, notarized: true,
    aaxIncluded: true, updateCheck: mac, sha256: fileFact(files[0]).sha256 }));
  fs.writeFileSync(files[5], JSON.stringify({ version: '1.1.51', commit: source.commit,
    unsigned_smoke_test: false, aax_included: true, git_dirty: '', updateCheck: mac, sha256: fileFact(files[3]).sha256 }));
  fs.writeFileSync(files[8], JSON.stringify({ updateCheck: win, installer: {
    payload: win.binaries.map(b => ({ role: b.role, format: b.format, binary_sha256: b.binarySha256 })) } }));
  for (const file of [files[0], files[3], files[6]]) fs.writeFileSync(`${file}.sha256`, `${fileFact(file).sha256}  ${path.basename(file)}\n`);
  const packages = files.map(fileFact);
  state.stages = Object.fromEntries(STAGES.map(stage => [stage, { status: 'PASS', facts: [packages[0]] }]));
  state.stages.packages.facts = packages;
  state.freeze = { source, inputs: digest(state.inputs), payloads: [packages[0]], updateCheck: updateBinding(state.inputs.updatePublicKey),
    criteriaSha256: digest({ tests: state.requiredHostTests, expected: state.expected }) };
  state.freeze.sha256 = digest(state.freeze);
  const writeReport = (input, gate, binding, ids, measured = null) => {
    const report = reportTemplate(state, gate, binding, ids);
    for (const row of report.tests) Object.assign(row, { result: 'PASS',
      measured: measured || row.expected, oracle: 'TEST fixture oracle', evidence: [packages[0]] });
    const file = path.resolve(root, state.inputs[input]); fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, JSON.stringify(report)); return file;
  };
  writeReport('hostsReport', 'A4', state.freeze.sha256, state.requiredHostTests);
  writeReport('distributionReport', 'A5', digest(packages), DISTRIBUTION_TESTS);
  writeReport('postreleaseReport', 'A7', digest(packages), POST_TESTS);
  const lsFile = path.resolve(root, state.inputs.lsState); fs.mkdirSync(path.dirname(lsFile), { recursive: true });
  fs.writeFileSync(lsFile, JSON.stringify({ lemonSqueezy: { products: [{ productId: 'TEST-product' }] } }));
  const lsReport = reportTemplate(state, 'A6-LS', digest(packages), ['LS/TEST-product']);
  Object.assign(lsReport.tests[0], { result: 'PASS', measured: { passed: true, sha256: packages[0].sha256 },
    oracle: 'TEST fixture LS oracle', evidence: [packages[0]] });
  fs.writeFileSync(path.resolve(root, state.inputs.lsReport), JSON.stringify(lsReport));
  state.stages.github.evidence = { url: 'https://github.com/heyalohaloha/kirin_hypha/releases/tag/v1.1.51', releaseId: 1 };
  state.hpCommit = 'c'.repeat(40);
  state.stages.hp.evidence = { hpCommit: state.hpCommit, downloads: packages.map(f => ({
    url: `https://github.com/heyalohaloha/kirin_hypha/releases/download/v1.1.51/${path.basename(f.path)}`,
    bytes: f.bytes, sha256: f.sha256 })) };
  state.releaseState = 'RELEASE_COMPLETE';
  return { state, files, packages };
}

test('offline producer checks real retained fixture bytes, exact reports and every channel without network', t => {
  const f = completeFixture(t), options = { now, expiresAt: now + 86400, sequence: 6 };
  assert.equal(payloadFromCompletedRelease(f.state, options).version, '1.1.51');
  f.state.releaseState = 'RELEASED'; assert.throws(() => payloadFromCompletedRelease(f.state, options), /complete/);
  f.state.releaseState = 'RELEASE_COMPLETE';
  f.state.stages.ls.status = 'PENDING'; assert.throws(() => payloadFromCompletedRelease(f.state, options), /complete/);
  f.state.stages.ls.status = 'PASS';
  f.state.stages.hp.evidence.downloads.pop(); assert.throws(() => payloadFromCompletedRelease(f.state, options), /download bytes/);
  f.state.stages.hp.evidence.downloads.push({ url: 'wrong', bytes: 1, sha256: '0'.repeat(64) });
  assert.throws(() => payloadFromCompletedRelease(f.state, options), /download bytes/);
  f.state.stages.hp.evidence.downloads = f.packages.map(p => ({
    url: `https://github.com/heyalohaloha/kirin_hypha/releases/download/v1.1.51/${path.basename(p.path)}`, bytes: p.bytes, sha256: p.sha256 }));
  fs.appendFileSync(f.files[0], 'tampered'); assert.throws(() => payloadFromCompletedRelease(f.state, options), /artifact changed/);
});

test('manifest signer must match the approved key frozen into every exact distributed binary', t => {
  const { state } = completeFixture(t);
  assertReleaseSigner(state, pair.privateKey);
  const other = crypto.generateKeyPairSync('rsa', { modulusLength: 2048, publicExponent: 65537 });
  assert.throws(() => assertReleaseSigner(state, other.privateKey), /mismatch|differs/);
  state.inputs.updatePublicKey = '';
  assert.throws(() => assertReleaseSigner(state, pair.privateKey), /Disabled/);
});

test('incident withdrawal requires unchanged completed receipt and a previous signed same release', t => {
  const { state } = completeFixture(t), completedState = structuredClone(state);
  const previousPayload = verifyWire(signManifest({ ...base(), formats: ['AU', 'VST3', 'AAX'] }, pair.privateKey, now), pair.publicKey, now);
  state.releaseState = 'RELEASE_INCIDENT'; state.blocker = { stage: 'postrelease', reason: 'confirmed later acceptance failure' };
  state.stages.postrelease.status = 'FAIL';
  const options = { now, expiresAt: now + 86400, sequence: 2, withdrawn: true, completedState, previousPayload };
  assert.equal(payloadFromCompletedRelease(state, options).withdrawn, true);
  assert.throws(() => payloadFromCompletedRelease(state, { ...options, withdrawn: false }), /complete/);
  assert.throws(() => payloadFromCompletedRelease(state, { ...options, completedState: undefined }), /Withdrawal/);
  assert.throws(() => payloadFromCompletedRelease(state, { ...options, previousPayload: { ...previousPayload, source_commit: 'f'.repeat(40) } }), /Withdrawal/);
  completedState.stages.hosts.status = 'SKIP';
  assert.throws(() => payloadFromCompletedRelease(state, options), /complete/);
});

test('A7 SKIP, changed candidate/report and forged measured LS download hash cannot generate a manifest', t => {
  const { state } = completeFixture(t), options = { now, expiresAt: now + 86400, sequence: 6 };
  const reportFile = path.resolve(state.root, state.inputs.postreleaseReport);
  const report = JSON.parse(fs.readFileSync(reportFile)); report.tests[0].result = 'SKIP';
  fs.writeFileSync(reportFile, JSON.stringify(report));
  assert.throws(() => payloadFromCompletedRelease(state, options), /not PASS/);
  report.tests[0].result = 'PASS'; report.candidate.commit = 'd'.repeat(40);
  fs.writeFileSync(reportFile, JSON.stringify(report)); assert.throws(() => payloadFromCompletedRelease(state, options), /binding mismatch/);
  report.candidate = state.candidate; fs.writeFileSync(reportFile, JSON.stringify(report));
  const lsFile = path.resolve(state.root, state.inputs.lsReport), ls = JSON.parse(fs.readFileSync(lsFile));
  ls.tests[0].measured.sha256 = 'e'.repeat(64); fs.writeFileSync(lsFile, JSON.stringify(ls));
  assert.throws(() => payloadFromCompletedRelease(state, options), /downloaded PKG mismatch/);
});
