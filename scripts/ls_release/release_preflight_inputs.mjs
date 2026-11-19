// Preparation only: reuse authoritative validators without initializing or publishing a candidate.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { ownerOverride } from './hypha_release_contract.mjs';
import { publicationBinding } from './publication_input.mjs';
import { stagePlan } from './release_stages.mjs';

export function inspectManufacturingInputs(config, { verifyJuce = root => {
  const env = { ...process.env }; delete env.KIRIN_JUCE_DIR;
  return spawnSync('bash', ['scripts/verify_juce_patch_state.sh'], {
    cwd: root, env, encoding: 'utf8', timeout: 30000, stdio: ['ignore', 'pipe', 'pipe'],
  }).status === 0;
} } = {}) {
  const checks = [], input = config.manufacturing;
  const check = (id, operation) => {
    try { checks.push({ id, result: operation() ? 'PASS' : 'CHECKPOINT' }); }
    catch { checks.push({ id, result: 'CHECKPOINT' }); }
  };
  const state = () => {
    if (!input || !path.isAbsolute(input.root) || !/^[0-9]+\.[0-9]+\.[0-9]+$/.test(input.version)
        || typeof input.candidateId !== 'string' || !input.candidateId.trim()) throw new Error('Missing preparation identity');
    return { root: fs.realpathSync(input.root), candidate: { version: input.version, id: input.candidateId },
      inputs: { date: input.date, notes: input.notes } };
  };
  check('init-state-not-overwritten', () => {
    const s = state();
    if (typeof input.state !== 'string' || !input.state) return false;
    const file = path.resolve(s.root, input.state), relative = path.relative(s.root, file);
    if (relative.startsWith('..') || path.isAbsolute(relative) || !relative.startsWith(`release_state${path.sep}`)) return false;
    try { fs.lstatSync(file); return false; } catch (e) { return e.code === 'ENOENT'; }
  });
  check('init-owner-overrides', () => {
    const s = state();
    if (!Array.isArray(input.ownerOverrides)) return false; // Explicit [] means no requested exception.
    const ids = input.ownerOverrides.map(file => ownerOverride(path.resolve(s.root, file), s.candidate).testId);
    return ids.length === new Set(ids).size;
  });
  check('juce-tracked-patches', () => verifyJuce(state().root));
  check('hp-publication-input', () => {
    const s = state();
    if (typeof input.publicationInput !== 'string' || !input.publicationInput) return false;
    publicationBinding(s, input.publicationInput); return true;
  });
  check('ls-stage-order', () => {
    state();
    const expected = stagePlan('complete');
    return Array.isArray(input.stages) && JSON.stringify(input.stages) === JSON.stringify(expected);
  });
  return checks;
}
