import { preparePlan, snapshotBundle, verifySnapshot } from './aax_swap_preflight.mjs';

import { shellTreeHash } from './aax_swap_shell_plan.mjs';

// Prepare all source and installed identities before starting the single authorized worker.
export async function prepareSession({ manifest, sources, inspectInstalled }) {
  if (typeof inspectInstalled !== 'function') throw new Error('installed inspector required');
  const plan = preparePlan(manifest, sources);
  const candidates = plan.map(item => {
    const snapshot = snapshotBundle(item.source, item.sha256, item.metadataSha256);
    verifySnapshot(snapshot, item.sha256);
    return { role: item.role, target: item.target, sha256: item.sha256, metadataSha256: item.metadataSha256, treeSha256: shellTreeHash(snapshot), snapshot };
  });
  for (const candidate of candidates) {
    const original = await inspectInstalled({ role: candidate.role, target: candidate.target });
    if (!/^[a-f0-9]{64}$/.test(original?.sha256 || '') || !/^[a-f0-9]{64}$/.test(original?.metadataSha256 || '')) {
      throw new Error('invalid installed bundle hash');
    }
    candidate.originalTreeSha256 = shellTreeHash(original);
    candidate.originalHash = original.sha256;
    candidate.originalMetadataSha256 = original.metadataSha256;
  }
  return { candidates };
}

export async function runPreparedSession(request, { authorize, interact }) {
  if (typeof authorize !== 'function' || typeof interact !== 'function') throw new Error('session callbacks required');
  const worker = await authorize(request);
  if (!worker || typeof worker.install !== 'function' || typeof worker.restore !== 'function') throw new Error('invalid session worker');
  // The worker must either roll back or durably report an incomplete install.
  await worker.install();
  let operationError, operationFailed = false;
  try { await interact(); } catch (error) { operationError = error; operationFailed = true; }
  try { await worker.restore(); }
  catch (error) {
    if (operationFailed) throw new AggregateError([operationError, error], 'operation failed and restore failed');
    throw error;
  }
  if (operationFailed) throw operationError;
  return { status: 'restored' };
}

export async function runSession(options) {
  const request = await prepareSession(options);
  return runPreparedSession(request, options);
}
