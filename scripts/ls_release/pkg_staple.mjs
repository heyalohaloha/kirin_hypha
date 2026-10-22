// Resumable notarization ticket attachment. Receipts/backups stay private; sidecars use final bytes.
import fs from 'node:fs';
import path from 'node:path';
import { atomicJson, checkFacts, digest, fileFact, readJson } from './hypha_release_contract.mjs';
import { parseNotarytoolAccepted } from './aax_notarization_receipt.mjs';
import { parseNotarytoolLog } from './aax_submission_archive.mjs';
import { inspectStapleBytes } from './pkg_staple_bytes.mjs';
import crypto from 'node:crypto';

export class PkgNetworkPending extends Error { constructor(message) { super(message); this.exitCode = 2; } }
export const PKG_RETRY_LIMIT = 3;
const retryWait = milliseconds => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, milliseconds);
const pending = new Set(['CONFIRM_PENDING', 'STAPLE_PENDING', 'VALIDATE_PENDING']);
const identity = context => ({ packagePath: path.resolve(context.packagePath), binding: context.binding, identity: context.identity });
function validateContext(receipt, context) {
  if (receipt.schema !== 'hypha-pkg-staple-v1' || receipt.contextSha256 !== digest(identity(context))) throw new Error('PKG recovery candidate/context differs');
  for (const file of [context.packagePath, receipt.original?.path, context.statePath, receipt.notaryLog?.path].filter(Boolean)) {
    if (!fs.lstatSync(file).isFile() || fs.lstatSync(file).isSymbolicLink()) throw new Error('PKG recovery requires regular retained files');
  }
  checkFacts([receipt.original]);
  if (receipt.metadata?.signed !== true || receipt.metadata?.fileName !== path.basename(context.packagePath)) throw new Error('PKG metadata is not the signed package');
  if (context.identity.source && (digest(receipt.metadata.source) !== digest(context.identity.source)
      || receipt.metadata.version !== context.identity.version || receipt.metadata.aaxIncluded !== context.identity.aaxIncluded)) {
    throw new Error('PKG product/source metadata differs from the frozen context');
  }
  if (receipt.metadataSha256 !== digest(receipt.metadata)) throw new Error('PKG metadata changed');
  const bytes = inspectStapleBytes(receipt.original.path, context.packagePath);
  return bytes;
}
export function verifyPkgResume(context, evidence) {
  if (evidence) checkFacts([evidence]);
  const receipt = readJson(context.statePath), bytes = validateContext(receipt, context);
  if (!pending.has(receipt.status) || !receipt.submission?.id) throw new Error('PKG has no resumable accepted-submission checkpoint');
  if (receipt.current) checkFacts([receipt.current]);
  if (receipt.notaryLog) verifyLog(receipt, context);
  return { receipt, bytes };
}
function verifyLog(receipt, context) {
  checkFacts([receipt.notaryLog]);
  const log = parseNotarytoolLog(fs.readFileSync(receipt.notaryLog.path, 'utf8'), 'PKG notary log');
  if (log.job_id !== receipt.submission.id || log.archive_sha256 !== receipt.original.sha256
      || log.archive_filename !== path.basename(context.packagePath)) throw new Error('PKG notary log does not bind the original signed bytes/submission');
}
export function isPkgNetworkFailure(result) {
  const text = `${result.stdout || ''}\n${result.stderr || ''}`;
  // Foundation NSURLError.h transport codes; CloudKit CKError.h networkUnavailable/networkFailure.
  return result.status === 68 || result.error?.code === 'ETIMEDOUT'
    || /(?:CloudKit[^\n]*-1005|Error\s+68\b)/i.test(text)
    || /NSURLErrorDomain(?:\s+Code\s*=\s*|\s*[:(]\s*|\s+)-100[134569]\b/i.test(text)
    || /CKErrorDomain(?:\s+Code\s*=\s*|\s*[:(]\s*|\s+)[34]\b/i.test(text);
}
export function boundedAppleRead(run, args, onAttempt = () => {}, wait = retryWait) {
  for (let attempt = 1; attempt <= PKG_RETRY_LIMIT; attempt++) {
    const result = run('xcrun', args, { capture: true });
    onAttempt({ operation: args[1], status: result.status, networkUnavailable: isPkgNetworkFailure(result) });
    if (result.status === 0) return result.stdout || '';
    if (!isPkgNetworkFailure(result)) throw new Error(`PKG ${args[1]} verification failed (exit ${result.status})`);
    if (attempt < PKG_RETRY_LIMIT) wait(500 * attempt);
  }
  throw new PkgNetworkPending(`PKG ${args[1]} remains unconfirmed after ${PKG_RETRY_LIMIT} network attempts; resume the retained submission`);
}
function network(receipt, context, run, args, wait) {
  return boundedAppleRead(run, args, attempt => {
    receipt.attempts.push(attempt); atomicJson(context.statePath, receipt);
  }, wait);
}
function publicSidecars(receipt, context) {
  const data = fs.readFileSync(context.packagePath), size = data.length;
  const publicFields = ['schema', 'product', 'version', 'updateCheck', 'legalDelivery', 'fileName', 'path', 'signed',
    'aaxIncluded', 'aaxNotarization', 'source', 'generatedAt'];
  const metadata = Object.fromEntries(publicFields.filter(key => key in receipt.metadata).map(key => [key, receipt.metadata[key]]));
  const sidecar = { ...metadata, size, sha256: crypto.createHash('sha256').update(data).digest('hex'),
    sha512: crypto.createHash('sha512').update(data).digest('base64'), lsDisplaySize: `${(size / 1024 / 1024).toFixed(2)} MB`,
    notarized: true, notarization: { submissionId: receipt.submission.id, status: 'Accepted' } };
  for (const [file, text] of [[`${context.packagePath}.json`, `${JSON.stringify(sidecar, null, 2)}\n`],
    [`${context.packagePath}.sha256`, `${sidecar.sha256}  ${path.basename(context.packagePath)}\n`]]) {
    if (fs.existsSync(file)) { if (fs.readFileSync(file, 'utf8') !== text) throw new Error('Existing PKG sidecar differs'); }
    else fs.writeFileSync(file, text, { flag: 'wx' });
  }
  return sidecar;
}
export function finishPkgStaple(context, { run, verifyLocal, metadata, resume = false, wait = retryWait }) {
  fs.mkdirSync(path.dirname(context.statePath), { recursive: true, mode: 0o700 });
  const lock = `${context.statePath}.lock`, fd = fs.openSync(lock, 'wx');
  let receipt;
  let incomingDirectory; let reading = false;
  try {
    incomingDirectory = fs.mkdtempSync(path.join(path.dirname(context.statePath), '.notary-read-'));
    const incoming = path.join(incomingDirectory, 'notary-log.json');
    if (resume) ({ receipt } = verifyPkgResume(context));
    else {
      if (fs.existsSync(context.statePath)) throw new Error('Existing PKG notarization state requires reconciliation; never resubmit');
      const original = path.join(path.dirname(context.statePath), 'original-before-staple.pkg');
      fs.copyFileSync(context.packagePath, original, fs.constants.COPYFILE_EXCL); fs.chmodSync(original, 0o400);
      inspectStapleBytes(original, context.packagePath);
      receipt = { schema: 'hypha-pkg-staple-v1', contextSha256: digest(identity(context)), original: fileFact(original),
        metadata, metadataSha256: digest(metadata), status: 'SUBMITTING', attempts: [] };
      atomicJson(context.statePath, receipt);
      validateContext(receipt, context);
      const submitted = run('xcrun', ['notarytool', 'submit', context.packagePath, '--keychain-profile', context.keychainProfile,
        '--wait', '--output-format', 'json'], { capture: true });
      if (submitted.status !== 0) throw new Error('PKG submit result needs reconciliation; do not resubmit');
      receipt.submission = parseNotarytoolAccepted(submitted.stdout, 'PKG submit');
      receipt.status = 'CONFIRM_PENDING'; atomicJson(context.statePath, receipt);
    }
    validateContext(receipt, context);
    if (!receipt.notaryLog) {
      reading = true;
      const confirmed = parseNotarytoolAccepted(network(receipt, context, run, ['notarytool', 'info', receipt.submission.id,
        '--keychain-profile', context.keychainProfile, '--output-format', 'json'], wait), 'PKG confirmation');
      if (confirmed.id !== receipt.submission.id) throw new Error('PKG submission id changed');
      network(receipt, context, run, ['notarytool', 'log', receipt.submission.id, incoming, '--keychain-profile', context.keychainProfile], wait);
      const parsed = parseNotarytoolLog(fs.readFileSync(incoming, 'utf8'), 'PKG notary log');
      if (parsed.job_id !== receipt.submission.id || parsed.archive_sha256 !== receipt.original.sha256
          || parsed.archive_filename !== path.basename(context.packagePath)) throw new Error('PKG notary log hash/submission mismatch');
      const logPath = path.join(path.dirname(context.statePath), 'notary-log.json');
      fs.copyFileSync(incoming, logPath, fs.constants.COPYFILE_EXCL);
      receipt.notaryLog = fileFact(logPath); receipt.status = 'STAPLE_PENDING'; reading = false; atomicJson(context.statePath, receipt);
    }
    verifyLog(receipt, context);
    let bytes = validateContext(receipt, context);
    if (!bytes.stapled) {
      for (let attempt = 1; attempt <= PKG_RETRY_LIMIT; attempt++) {
        const result = run('xcrun', ['stapler', 'staple', context.packagePath], { capture: true });
        receipt.attempts.push({ operation: 'staple', status: result.status, networkUnavailable: isPkgNetworkFailure(result) });
        bytes = validateContext(receipt, context); receipt.current = bytes.current; atomicJson(context.statePath, receipt);
        if (result.status !== 0 && !isPkgNetworkFailure(result)) throw new Error(`PKG staple failed (exit ${result.status})`);
        if (bytes.stapled) break;
        if (result.status === 0) throw new Error('Stapler succeeded without appending one ticket');
        if (attempt < PKG_RETRY_LIMIT) wait(500 * attempt);
      }
      if (!bytes.stapled) throw new PkgNetworkPending('PKG staple remains unconfirmed; explicitly resume the same signed PKG/submission');
    }
    receipt.ticket = { bytes: bytes.ticketBytes, sha256: bytes.ticketSha256 }; receipt.status = 'VALIDATE_PENDING';
    receipt.current = bytes.current; atomicJson(context.statePath, receipt);
    // Local signature/payload/Gatekeeper and prefix checks precede network-dependent validation.
    const local = verifyLocal();
    if (digest(local) !== digest(receipt.metadata.updateCheck)) throw new Error('PKG verified payload metadata differs');
    reading = true;
    network(receipt, context, run, ['stapler', 'validate', context.packagePath], wait);
    reading = false;
    validateContext(receipt, context); checkFacts([receipt.current]);
    const sidecar = publicSidecars(receipt, context);
    receipt.status = 'COMPLETE'; receipt.final = fileFact(context.packagePath); atomicJson(context.statePath, receipt);
    return sidecar;
  } catch (error) {
    if (receipt) {
      if (!(error instanceof PkgNetworkPending) && !reading) receipt.status = 'FAILED';
      receipt.current = fileFact(context.packagePath); atomicJson(context.statePath, receipt);
    }
    throw error;
  } finally { if (incomingDirectory) fs.rmSync(incomingDirectory, { recursive: true, force: true }); fs.closeSync(fd); fs.unlinkSync(lock); }
}
