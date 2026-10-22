import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import test from 'node:test';
import { finishPkgStaple, verifyPkgResume, PkgNetworkPending, PKG_RETRY_LIMIT, isPkgNetworkFailure, boundedAppleRead } from './pkg_staple.mjs';
import { fileFact, readJson } from './hypha_release_contract.mjs';
import { inspectStapleBytes } from './pkg_staple_bytes.mjs';

const submission = '12345678-1234-1234-1234-123456789abc';
function ticket() {
  // Independent little-endian fixture: terminator, one ticket, ticket trailer.
  const terminator = Buffer.from('74386c72010001000000000000000000', 'hex');
  const payload = Buffer.from('s8chfixture-ticket');
  const trailer = Buffer.from('74386c72010002000000000000000000', 'hex');
  trailer.writeUInt32LE(payload.length, 8);
  return Buffer.concat([terminator, payload, trailer]);
}
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-pkg-staple-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const packagePath = path.join(root, 'signed.pkg'), statePath = path.join(root, 'private/receipt.json');
  fs.writeFileSync(packagePath, 'xar!independent signed payload bytes');
  const originalHash = fileFact(packagePath).sha256, calls = [];
  const context = { packagePath, statePath, binding: 'candidate-freeze', identity: { commit: 'a'.repeat(40) }, keychainProfile: 'fixture-profile' };
  const metadata = { schema: 'kirin-hypha-pkg-artifact-v1', fileName: 'signed.pkg', signed: true, updateCheck: { fixturePayload: true } };
  const mode = { staple: 'network', validate: 'ok', wrongLog: false, damaged: false };
  const run = (tool, args) => {
    calls.push([tool, ...args]); assert.equal(tool, 'xcrun');
    if (args[0] === 'notarytool') {
      if (args[1] === 'log') fs.writeFileSync(args[3], JSON.stringify({ jobId: submission, status: 'Accepted',
        archiveFilename: 'signed.pkg', sha256: mode.wrongLog ? 'f'.repeat(64) : originalHash, logFormatVersion: 1, issues: null }));
      return { status: 0, stdout: JSON.stringify({ id: submission, status: 'Accepted' }) };
    }
    assert.equal(args[0], 'stapler');
    if (args[1] === 'staple') {
      if (mode.staple === 'network') return { status: 68, stderr: 'CloudKit connection lost -1005' };
      if (mode.staple === 'invalid') return { status: 65, stderr: 'invalid package' };
      fs.appendFileSync(packagePath, ticket());
      if (mode.damaged) { const b = fs.readFileSync(packagePath); b[5] ^= 1; fs.writeFileSync(packagePath, b); }
      return { status: 0 };
    }
    if (mode.validate === 'network') return { status: 68, stderr: 'Error 68' };
    if (mode.validate === 'invalid') return { status: 65 };
    return { status: 0 };
  };
  const verifyLocal = () => { calls.push(['local-signatures-payload-spctl']); return metadata.updateCheck; };
  return { root, context, metadata, mode, calls, run, verifyLocal, wait: () => {} };
}

test('staple network loss resumes the same signed PKG/submission with no second submit or manufacturing', t => {
  const f = fixture(t), original = fs.readFileSync(f.context.packagePath);
  f.metadata.preStapleSha256 = 'private fixture hash';
  f.metadata.originalBackup = 'private fixture location';
  assert.throws(() => finishPkgStaple(f.context, f), PkgNetworkPending);
  const failed = readJson(f.context.statePath);
  assert.equal(failed.status, 'STAPLE_PENDING'); assert.equal(failed.submission.id, submission);
  assert.deepEqual(fs.readFileSync(failed.original.path), original);
  assert.equal(f.calls.filter(c => c[2] === 'staple').length, PKG_RETRY_LIMIT);
  assert.equal(fs.existsSync(`${f.context.packagePath}.json`), false);
  verifyPkgResume(f.context, fileFact(f.context.statePath));
  f.mode.staple = 'ok';
  const result = finishPkgStaple(f.context, { ...f, resume: true });
  assert.equal(result.notarization.submissionId, submission);
  assert.equal(f.calls.filter(c => c[2] === 'submit').length, 1);
  assert.equal(f.calls.filter(c => c[2] === 'info').length, 1);
  const complete = readJson(f.context.statePath);
  assert.equal(complete.status, 'COMPLETE');
  assert.equal(inspectStapleBytes(complete.original.path, f.context.packagePath).stapled, true);
  assert.equal(result.sha256, fileFact(f.context.packagePath).sha256);
  assert.notEqual(result.sha256, complete.original.sha256);
  for (const key of ['original', 'current', 'statePath', 'ticket', 'contextSha256', 'preStapleSha256', 'originalBackup']) assert.equal(key in result, false);
  assert.ok(f.calls.findIndex(c => c[0] === 'local-signatures-payload-spctl') < f.calls.findIndex(c => c[2] === 'validate'));
  assert.throws(() => finishPkgStaple(f.context, f));
  assert.equal(fs.existsSync(`${f.context.statePath}.lock`), false);
});
test('validate network loss remains unconfirmed, bounded, and resumes validation without a second staple', t => {
  const f = fixture(t); f.mode.staple = 'ok'; f.mode.validate = 'network';
  assert.throws(() => finishPkgStaple(f.context, f), PkgNetworkPending);
  assert.equal(readJson(f.context.statePath).status, 'VALIDATE_PENDING');
  assert.equal(f.calls.filter(c => c[2] === 'validate').length, PKG_RETRY_LIMIT);
  assert.equal(fs.existsSync(`${f.context.packagePath}.json`), false);
  f.mode.validate = 'ok'; finishPkgStaple(f.context, { ...f, resume: true });
  assert.equal(f.calls.filter(c => c[2] === 'staple').length, 1);
});
test('foreign candidate, changed original/current, wrong notary hash and non-network failure refuse recovery', t => {
  const f = fixture(t); assert.throws(() => finishPkgStaple(f.context, f), PkgNetworkPending);
  assert.throws(() => verifyPkgResume({ ...f.context, binding: 'foreign' }));
  fs.appendFileSync(f.context.packagePath, 'junk'); assert.throws(() => verifyPkgResume(f.context));
  const g = fixture(t); g.mode.wrongLog = true;
  assert.throws(() => finishPkgStaple(g.context, g), /hash\/submission/);
  assert.equal(g.calls.filter(c => c[2] === 'staple').length, 0);
  const h = fixture(t); h.mode.staple = 'invalid';
  assert.throws(() => finishPkgStaple(h.context, h), /staple failed/);
  assert.equal(h.calls.filter(c => c[2] === 'staple').length, 1);
  assert.equal(readJson(h.context.statePath).status, 'FAILED');
  assert.throws(() => verifyPkgResume(h.context));
});
test('one-bit prefix change, extra or truncated ticket and duplicate trailer never qualify', t => {
  const f = fixture(t); f.mode.staple = 'ok'; f.mode.damaged = true;
  assert.throws(() => finishPkgStaple(f.context, f), /prefix/);
  const original = path.join(f.root, 'original.pkg'); fs.writeFileSync(original, 'xar!signed original payload');
  const current = path.join(f.root, 'current.pkg'), bytes = fs.readFileSync(original);
  for (const extra of [Buffer.from('junk'), ticket().subarray(0, -1), Buffer.concat([ticket(), ticket()])]) {
    fs.writeFileSync(current, Buffer.concat([bytes, extra]));
    assert.throws(() => inspectStapleBytes(original, current));
  }
});

test('public preflight treats validation network loss as unconfirmed, with bounded attempts', async () => {
  const { verifyStaplerReadback, verificationExitCode } = await import('./kirin_hypha_ls_dry_run.mjs');
  let calls = 0;
  const unavailable = verifyStaplerReadback('fixture.pkg', () => { calls++; return { status: 68 }; }, () => {});
  assert.equal(verificationExitCode([unavailable]), 2);
  assert.equal(unavailable.status, 'UNCONFIRMED'); assert.equal(unavailable.ok, false); assert.equal(calls, PKG_RETRY_LIMIT);
  calls = 0;
  const invalid = verifyStaplerReadback('fixture.pkg', () => { calls++; return { status: 65 }; });
  assert.equal(verificationExitCode([unavailable, invalid]), 1);
  assert.equal(verificationExitCode([{ ok: true }]), 0);
  assert.equal(invalid.status, 'FAIL'); assert.equal(calls, 1);
  assert.equal(verifyStaplerReadback('fixture.pkg', () => ({ status: 0 })).status, 'PASS');
});

test('documented transport codes retry with injected waits; authentication and package errors do not', () => {
  for (const stderr of [-1001, -1003, -1004, -1005, -1006, -1009].map(code => `Error Domain=NSURLErrorDomain Code=${code}`)
    .concat(['Error Domain=CKErrorDomain Code=3', 'Error Domain=CKErrorDomain Code=4'])) {
    assert.equal(isPkgNetworkFailure({ status: 1, stderr }), true);
  }
  for (const stderr of ['NSURLErrorDomain Code=-1012', 'NSURLErrorDomain Code=-1200', 'CKErrorDomain Code=9', 'invalid package'])
    assert.equal(isPkgNetworkFailure({ status: 65, stderr }), false);
  const waits = []; let calls = 0;
  assert.throws(() => boundedAppleRead(() => { calls++; return { status: 1, stderr: 'NSURLErrorDomain Code=-1001' }; },
    ['notarytool', 'info'], () => {}, ms => waits.push(ms)), PkgNetworkPending);
  assert.equal(calls, PKG_RETRY_LIMIT); assert.deepEqual(waits, [500, 1000]);
});
test('a failed read retains the accepted submission checkpoint and resumes without resubmission', t => {
  for (const operation of ['info', 'log', 'validate']) {
    const f = fixture(t); f.mode.staple = 'ok';
    const run = (tool, args) => args[1] === operation ? { status: 65 } : f.run(tool, args);
    assert.throws(() => finishPkgStaple(f.context, { ...f, run }));
    assert.ok(['CONFIRM_PENDING', 'VALIDATE_PENDING'].includes(readJson(f.context.statePath).status));
    verifyPkgResume(f.context);
    finishPkgStaple(f.context, { ...f, resume: true });
    assert.equal(f.calls.filter(call => call[2] === 'submit').length, 1);
  }
});
