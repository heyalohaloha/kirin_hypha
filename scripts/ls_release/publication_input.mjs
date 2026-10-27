// Public wording is approved separately from the frozen manufacturing inputs.
import fs from 'node:fs';
import path from 'node:path';
import { Checkpoint, digest, fileFact, readJson, resolveInput, authorization } from './hypha_release_contract.mjs';
import { assertReviewedNotice, installerFilename, makeHpRecord } from './hypha_release_hp_data.mjs';

export function publicationBinding(state, file) {
  const inputPath = path.resolve(state.root, file);
  const input = readJson(inputPath);
  if (Object.keys(input).sort().join(',') !== 'date,notice,schema,version'
      || input.schema !== 'hypha-publication-v1' || input.version !== state.candidate.version
      || input.date !== state.inputs.date) throw new Error('Public input version/date/schema mismatch');
  const facts = ['macos', 'windows'].map(target => ({ path: installerFilename(input.version, target), bytes: 1, sha256: 'a'.repeat(64) }));
  assertReviewedNotice(makeHpRecord(input.version, input.date, facts), input.notice);
  const inputFact = fileFact(inputPath), notesFact = fileFact(resolveInput(state, state.inputs.notes));
  const identity = { candidateId: state.candidate.id, version: input.version, date: input.date,
    inputSha256: inputFact.sha256, notesSha256: notesFact.sha256 };
  return { input: inputFact, notes: notesFact, sha256: digest(identity) };
}
export function publicationNotice(state) {
  if (!state.publication) throw new Checkpoint('Reviewed public input must be bound before publication');
  const current = publicationBinding(state, state.publication.input.path);
  if (digest(current) !== digest(state.publication)) throw new Checkpoint('Public input or notes changed; bind and approve the new digest');
  return readJson(current.input.path).notice;
}
export function authorizePublication(state, options) {
  authorization(state, options);
  publicationNotice(state);
  if (options.publicationApproved !== state.publication.sha256) throw new Checkpoint('Public input approval digest does not match');
}
export async function auditBeforeInit(options, run) {
  if (!options.hpRoot) throw new Checkpoint('HP checkout is required for the dependency audit');
  await run('npm', ['run', 'security:audit'], { cwd: options.hpRoot });
}

export async function bindPublication(statePath, file, { validate } = {}) {
  const { assertState, atomicJson } = await import('./hypha_release_contract.mjs');
  const { sourceSnapshot } = await import('../hypha_build_source.mjs');
  const lock = `${statePath}.lock`, fd = fs.openSync(lock, 'wx');
  try {
    const state = readJson(statePath);
    if (validate) validate(state);
    else {
      assertState(state);
      if (digest(sourceSnapshot(state.root)) !== digest(state.source)) throw new Error('Candidate source changed');
      if (state.freeze) {
        const { sha256, ...frozen } = state.freeze;
        if (digest(frozen) !== sha256 || state.freeze.inputs !== digest(state.inputs)) throw new Error('Frozen candidate changed');
      }
    }
    if (state.githubRelease || state.hpPlanned || state.hpCommit
        || ['github', 'hp', 'postrelease'].some(stage => ['PASS', 'RUNNING', 'FAIL'].includes(state.stages[stage]?.status))) {
      throw new Checkpoint('Publication already started; reconcile published evidence before changing copy');
    }
    if (state.freeze?.publicationNotes) throw new Checkpoint('Legacy frozen notes cannot be rebound without candidate reconciliation');
    state.publication = publicationBinding(state, file);
    atomicJson(statePath, state);
    return state;
  } finally { fs.closeSync(fd); fs.unlinkSync(lock); }
}
