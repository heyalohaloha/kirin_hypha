// Retain public LS evidence while access problems remain resumable checkpoints.
import fs from 'node:fs';
import path from 'node:path';
import { Checkpoint, resolveInput, safeStatePath, fileFact } from './hypha_release_contract.mjs';
import { loadApiKey } from './ls_api_readback.mjs';

export async function confirmLsReadback(state, run, save, key = loadApiKey) {
  try { if (!key()?.trim()) throw new Error(); }
  catch { throw new Checkpoint('LS API credential is unreadable; confirm runtime access before publication'); }
  const directory = path.dirname(safeStatePath(state.root, path.join(path.dirname(resolveInput(state, state.inputs.lsReport)), 'ls-api-checks/checks.json')));
  fs.mkdirSync(directory, { recursive: true, mode: 0o700 });
  const output = path.join(fs.mkdtempSync(path.join(directory, 'attempt-')), 'checks.json');
  const retain = () => {
    if (!fs.statSync(output, { throwIfNoEntry: false })?.size) return;
    const fact = fileFact(output);
    state.stages.ls.evidence = { ...state.stages.ls.evidence, apiChecks: fact }; save();
    return fact;
  };
  state.publicationStarted = true; save();
  try {
    await run('node', ['scripts/ls_release/kirin_hypha_ls_dry_run.mjs', '--state',
      resolveInput(state, state.inputs.lsState), '--with-ls-api', '--json'], { stdoutFile: output });
    return retain();
  } catch (error) {
    retain();
    if (error.exitCode === 2) throw new Checkpoint('LS API readback is unconfirmed; retry after access recovers');
    throw error;
  }
}
