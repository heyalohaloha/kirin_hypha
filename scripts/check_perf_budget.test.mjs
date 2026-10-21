import assert from 'node:assert/strict';
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { PROBES, parseBudget, checkBudget } from './check_perf_budget.mjs';
const table = value => PROBES.map(name => `${name}\t${value}`).join('\n');

test('performance budgets require every probe, finite positive values and a downward ratchet', () => {
  checkBudget(table(5), table(6)); checkBudget(table(5), table(5));
  assert.throws(() => checkBudget(table(6), table(5)));
  for (const invalid of ['NaN', 'Infinity', '0', '-1', '']) assert.throws(() => parseBudget(table(invalid)));
  assert.throws(() => parseBudget(`${table(5)}\n${PROBES[0]}\t5`));
  assert.throws(() => parseBudget(`${PROBES[0]}\t5`));
  assert.throws(() => parseBudget(`${table(5)}\nunknown\t5`));
  assert.throws(() => parseBudget(table(5).replace('\t5', '\t5\textra')));
  checkBudget(fs.readFileSync(new URL('./perf_budget.tsv', import.meta.url), 'utf8'));
});

test('release source probes keep the absolute ceiling locally and use calibration on CI', () => {
  const plan = ci => {
    const env = { ...process.env, GITHUB_ACTIONS: ci };
    const result = spawnSync('/bin/bash', ['scripts/test_release_source.sh', '--phase', 'rust', '--plan'], { env, encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
    return result.stdout.split('\n').filter(line => line.includes('KIRIN_HYPHA_PERF_ABSOLUTE='));
  };
  for (const [ci, expected] of [['', '1'], ['true', '0']]) {
    const commands = plan(ci);
    for (const probe of PROBES) {
      const command = commands.find(line => line.includes(probe));
      assert.ok(command, `probe ${probe} is scheduled`);
      assert.ok(command.includes(`KIRIN_HYPHA_PERF_ABSOLUTE=${expected}`));
    }
  }
});
