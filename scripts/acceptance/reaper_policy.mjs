import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { atomicJson, fileFact, safeStatePath } from '../ls_release/hypha_release_contract.mjs';
import { loadPrivateReaperConfig } from './reaper_run.mjs';
import { candidateKeyProof } from './reaper_capture_binding.mjs';

export function bindReaperPolicy(root, profile, format, keyConfig, producerEvidence) {
  root = fs.realpathSync(root);
  const statePath = safeStatePath(root, profile);
  const state = JSON.parse(fs.readFileSync(statePath, 'utf8'));
  if (state.freeze) throw new Error('Candidate is already frozen');
  if (state.root !== root || !['vst3', 'au'].includes(format)) throw new Error('Invalid candidate or REAPER format');
  const id = `macOS/x86_64/${format.toUpperCase()}`;
  if (!state.requiredHostTests?.includes(id)) throw new Error('REAPER host row missing');
  const configPath = path.resolve(keyConfig), evidencePath = path.resolve(producerEvidence);
  const config = loadPrivateReaperConfig(configPath, root), producer = loadPrivateReaperConfig(evidencePath, root);
  const proof = candidateKeyProof(config.records?.hmacKey, state.candidate);
  if (config.candidate?.id !== state.candidate.id || config.candidate?.commit !== state.candidate.commit
      || producer.schema !== 'hypha-reaper-key-provenance-v1' || producer.candidate?.id !== state.candidate.id
      || producer.candidate?.commit !== state.candidate.commit || producer.keyProof !== proof
      || typeof producer.review !== 'string' || !producer.review.trim()) throw new Error('Candidate key producer evidence differs');
  state.evidenceMethods = { ...state.evidenceMethods, [id]: 'reaper' };
  state.reaperKeyBindings = { ...state.reaperKeyBindings, [id]: { keyConfig: configPath, proof, producerEvidence: fileFact(evidencePath) } };
  atomicJson(statePath, state);
  return { id, proof };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const args = process.argv.slice(2), root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
    if (args.length === 2 && args[0] === '--key-proof') {
      const config = loadPrivateReaperConfig(args[1], root);
      process.stdout.write(`${candidateKeyProof(config.records?.hmacKey, config.candidate)}\n`);
    } else if (args.length === 5 && args[0] === '--bind') {
      const result = bindReaperPolicy(root, args[1], args[2], args[3], args[4]);
      process.stdout.write(`Bound REAPER policy for ${result.id}\n`);
    } else throw new Error('Usage: --key-proof PRIVATE_CONFIG | --bind PROFILE FORMAT PRIVATE_CONFIG PRODUCER_EVIDENCE');
  } catch (error) { process.stderr.write(`${error.message}\n`); process.exitCode = 1; }
}
