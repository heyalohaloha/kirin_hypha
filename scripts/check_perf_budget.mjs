#!/usr/bin/env node
// Performance budgets only ratchet downward. Absolute local CPU criteria are independent.
import fs from 'node:fs';
import childProcess from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const PROBES = ['one_visible_pair_continuous_sharpness_worker_budget_is_quantified',
  'two_post_absolute_workers_fit_the_optional_analysis_budget'];
export function parseBudget(source) {
  const rows = new Map();
  for (const line of source.split(/\r?\n/).filter(line => line.trim() && !line.startsWith('#'))) {
    const fields = line.split('\t'), [name, value] = fields;
    if (fields.length !== 2 || !PROBES.includes(name) || rows.has(name) || !value.trim()
        || !Number.isFinite(Number(value)) || Number(value) <= 0) throw new Error('Invalid or duplicate performance budget');
    rows.set(name, Number(value));
  }
  if (PROBES.some(name => !rows.has(name))) throw new Error('Performance probe budget missing');
  return rows;
}
export function checkBudget(current, previous = '') {
  const rows = parseBudget(current), old = previous ? parseBudget(previous) : new Map();
  for (const [name, value] of rows) if (old.has(name) && value > old.get(name)) throw new Error('Performance budget increased');
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = path.resolve(import.meta.dirname, '..');
  const base = process.env.SOURCE_LINE_BUDGET_BASE_REF || childProcess.execFileSync('git', ['merge-base', 'HEAD', 'origin/main'], { cwd: root, encoding: 'utf8' }).trim();
  childProcess.execFileSync('git', ['rev-parse', '--verify', `${base}^{commit}`], { cwd: root, stdio: 'ignore' });
  const old = childProcess.spawnSync('git', ['show', `${base}:scripts/perf_budget.tsv`], { cwd: root, encoding: 'utf8' });
  checkBudget(fs.readFileSync(path.join(root, 'scripts/perf_budget.tsv'), 'utf8'), old.status === 0 ? old.stdout : '');
  console.log('performance budget: PASS');
}
