// Bind only an existing owner decision; this operation never creates a decision or changes payloads.
import fs from 'node:fs';
import path from 'node:path';
import { sourceSnapshot } from '../hypha_build_source.mjs';
import { assertState, atomicJson, checkFacts, digest, fileFact, ownerOverride, readJson } from './hypha_release_contract.mjs';

export function bindOwnerOverrides(statePath, files, snapshot = sourceSnapshot) {
  const lock = `${statePath}.lock`, fd = fs.openSync(lock, 'wx');
  try {
    const profile = fileFact(statePath);
    const state = readJson(statePath); assertState(state);
    if (state.freeze || state.stages.freeze) throw new Error('Owner override cannot bind after freeze has started');
    if (digest(snapshot(state.root)) !== digest(state.source)) throw new Error('Candidate source changed');
    if (!Array.isArray(files) || !files.length) throw new Error('Existing owner decision is required');
    for (const saved of Object.values(state.stages)) if (saved.status === 'PASS' && saved.facts) checkFacts(saved.facts);
    const overrides = [...(state.ownerOverrides || [])];
    for (const file of files) {
      const resolved = path.resolve(state.root, file), decision = readJson(resolved);
      if (decision.candidateId !== state.candidate.id) throw new Error('Owner decision must explicitly name this candidate');
      const bound = ownerOverride(resolved, state.candidate);
      if (overrides.some(old => old.testId === bound.testId)) throw new Error('Owner override names a row twice');
      overrides.push(bound);
    }
    state.ownerOverrides = overrides;
    assertState(state);
    if (digest(fileFact(statePath)) !== digest(profile)) throw new Error('Profile changed during binding');
    atomicJson(statePath, state); return state;
  } finally { fs.closeSync(fd); fs.unlinkSync(lock); }
}
