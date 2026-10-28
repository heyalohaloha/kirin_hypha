// Recognize only a transient CI recheck of an intact, persisted COMPLETE profile.
import fs from 'node:fs';
import path from 'node:path';
import { assertState, digest, fileFact, readJson } from './hypha_release_contract.mjs';

export function requireRequalification(state, statePath, actions) {
  if (!actions || !statePath || state.releaseState !== 'RELEASE_COMPLETE' || state.stages?.ci?.status !== 'PENDING') {
    assertState(state);
    return false;
  }
  const stored = readJson(statePath);
  assertState(stored);
  if (stored.releaseState !== 'RELEASE_COMPLETE' || stored.stages?.ci?.status !== 'PASS') {
    throw new Error('CI requalification requires the original completed profile');
  }
  const normalized = structuredClone(state);
  normalized.stages.ci.status = 'PASS';
  if (digest(normalized) !== digest(stored)) throw new Error('CI requalification profile changed');
  const original = fileFact(statePath);
  const directory = path.join(path.dirname(statePath), 'source-adapter-original-profiles');
  if (fs.realpathSync(directory) !== path.resolve(directory)) throw new Error('CI requalification receipt directory traverses symlink');
  const receipt = fileFact(path.join(directory, `${original.sha256}.json`));
  if (receipt.sha256 !== original.sha256 || receipt.bytes !== original.bytes) throw new Error('CI requalification receipt differs from original profile');
  return receipt;
}
