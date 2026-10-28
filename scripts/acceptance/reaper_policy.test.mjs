import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { bindReaperPolicy } from './reaper_policy.mjs';
import { candidateKeyProof } from './reaper_capture_binding.mjs';

test('candidate key policy binds reviewed producer fact before freeze', t => {
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-policy-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const repo = path.join(root, 'repo'); fs.mkdirSync(repo); fs.mkdirSync(path.join(repo, 'release_state'));
  const candidate = { id: 'candidate', commit: 'a'.repeat(40) }, config = path.join(root, 'config.json'), producer = path.join(root, 'producer.json');
  fs.writeFileSync(config, JSON.stringify({ candidate, records: { hmacKey: 'fixture-key' } }), { mode: 0o600 });
  const proof = candidateKeyProof('fixture-key', candidate);
  fs.writeFileSync(producer, JSON.stringify({ schema: 'hypha-reaper-key-provenance-v1', candidate, keyProof: proof, review: 'independent fixture review' }), { mode: 0o600 });
  const profile = path.join(repo, 'release_state', 'candidate.json');
  fs.writeFileSync(profile, JSON.stringify({ root: repo, candidate, requiredHostTests: ['macOS/x86_64/VST3'] }));
  bindReaperPolicy(repo, profile, 'vst3', config, producer);
  const state = JSON.parse(fs.readFileSync(profile));
  assert.equal(state.evidenceMethods['macOS/x86_64/VST3'], 'reaper');
  assert.equal(state.reaperKeyBindings['macOS/x86_64/VST3'].proof, proof);
  assert.equal(JSON.stringify(state).includes('fixture-key'), false);
  state.freeze = {}; fs.writeFileSync(profile, JSON.stringify(state));
  assert.throws(() => bindReaperPolicy(repo, profile, 'vst3', config, producer), /frozen/i);
});
