import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { inspectBundle } from './aax_swap_preflight.mjs';
import { parseArgs, performSwap } from './aax_swap.mjs';

const posixTest = process.platform === 'win32' ? test.skip : test;

test('CLI requires an explicit plan or run and a fresh receipt destination', () => {
  assert.equal(parseArgs(['--help']).help, true);
  assert.throws(() => parseArgs(['--run', 'config']), /receipt/);
  assert.throws(() => parseArgs(['--run', 'config', '--plan', 'config', '--receipt', 'out']), /one mode/);
  assert.throws(() => parseArgs(['--run', 'config', '--receipt', 'out', '--target', '/other']), /unknown/);
});

posixTest('CLI plan never authorizes; run records hashes and uses one worker', async t => {
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'aax-cli-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const manifest = ['PRE', 'POST'].map(role => ({ role,
    install_relative: `Library/Application Support/Avid/Audio/Plug-Ins/Kirin Hypha ${role}.aaxplugin` }));
  const sources = manifest.map(({ role }) => {
    const source = path.join(root, role); fs.mkdirSync(source, { mode: 0o700 });
    fs.writeFileSync(path.join(source, 'payload'), role, { mode: 0o600 });
    return { role, source, sha256: inspectBundle(source).sha256 };
  });
  const config = path.join(root, 'config.json');
  fs.writeFileSync(config, JSON.stringify({ schema: 'hypha-aax-swap-v1', sources }), { mode: 0o600 });
  let prompts = 0, installed = 0, restored = 0;
  const dependencies = { manifest,
    inspectInstalled: async ({ role }) => inspectBundle(path.join(root, role)),
    authorize: async () => { prompts += 1; return {
      install: async () => { installed += 1; }, restore: async () => { restored += 1; },
    }; }, interact: async () => {},
  };
  const plan = await performSwap({ mode: 'plan', config, receipt: path.join(root, 'plan.jsonl') }, dependencies);
  assert.equal(plan.status, 'planned'); assert.equal(prompts, 0);
  const result = await performSwap({ mode: 'run', config, receipt: path.join(root, 'run.jsonl') }, dependencies);
  assert.equal(result.status, 'restored'); assert.equal(prompts, 1); assert.equal(installed, 1); assert.equal(restored, 1);
  const events = fs.readFileSync(path.join(root, 'run.jsonl'), 'utf8').trim().split('\n').map(JSON.parse);
  assert.equal(events[0].phase, 'prepared');
  assert.match(events[0].scriptSha256, /^[a-f0-9]{64}$/);
  assert.match(events[0].commandSha256, /^[a-f0-9]{64}$/);
  assert.equal(events.at(-1).phase, 'restored');
  assert.equal(fs.statSync(path.join(root, 'run.jsonl')).mode & 0o777, 0o600);
});

test('example source paths meet the preflight whitespace rule', () => {
  const example = JSON.parse(fs.readFileSync(new URL('../../docs/release/aax_swap_config.example.json', import.meta.url), 'utf8'));
  assert.equal(example.sources.length, 2);
  for (const item of example.sources) assert.doesNotMatch(item.source, /\s/);
});
