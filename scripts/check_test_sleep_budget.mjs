#!/usr/bin/env node
// Ratchet for short sleeps in tests. A test that sleeps a few milliseconds and polls depends on
// the scheduler: a virtual CI machine or background QoS stretches an 11 ms sleep to ~100 ms, so
// such tests pass locally and time out on CI. Wait for a notification or follow audio time
// instead. Existing short sleeps are recorded per file; a file may not add one, a new file may
// not start with one, and a file that removes one must lower its recorded count.
import { execFileSync } from 'node:child_process';
import { readFileSync, existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const SHORT_SLEEP_LIMIT_MS = 50;

const CPP = /sleep_for\s*\(\s*std::chrono::(milliseconds|microseconds|nanoseconds)\s*\(\s*(\d+)\s*\)\s*\)/g;
const CPP_LITERAL = /sleep_for\s*\(\s*(\d+)\s*(ms|us|ns)\s*\)/g;
const RUST = /sleep\s*\(\s*(?:std::time::)?Duration::from_(millis|micros|nanos)\s*\(\s*(\d+)\s*\)\s*\)/g;
const toMs = { milliseconds: 1, ms: 1, millis: 1, microseconds: 1e-3, us: 1e-3, micros: 1e-3,
  nanoseconds: 1e-6, ns: 1e-6, nanos: 1e-6 };

export function isTestSource(file) {
  if (/^juce_shell\/tests\/.+\.(cpp|h|mm)$/.test(file)) return true;
  if (!/^crates\/.+\.rs$/.test(file)) return false;
  return /\/tests\//.test(file) || /(^|\/)[^/]*tests?\.rs$/.test(file) || /test_support[^/]*\.rs$/.test(file);
}

export function countShortSleeps(text) {
  let count = 0;
  for (const pattern of [CPP, CPP_LITERAL, RUST]) {
    for (const match of text.matchAll(pattern)) {
      const unit = match[1] in toMs ? match[1] : match[2];
      const amount = Number(match[1] in toMs ? match[2] : match[1]);
      if (amount * toMs[unit] <= SHORT_SLEEP_LIMIT_MS) count++;
    }
  }
  return count;
}

export function parseBaseline(text) {
  const baseline = new Map();
  for (const line of text.split('\n')) {
    if (!line.trim() || line.startsWith('#')) continue;
    const [file, value] = line.split('\t');
    if (!file || !/^\d+$/.test(value ?? '')) throw new Error(`malformed sleep budget line: ${line}`);
    if (baseline.has(file)) throw new Error(`duplicate sleep budget entry: ${file}`);
    baseline.set(file, Number(value));
  }
  return baseline;
}

export function checkSleepBudget(counts, baseline) {
  const failures = [];
  for (const [file, count] of counts) {
    const allowed = baseline.get(file) ?? 0;
    if (count > allowed)
      failures.push(`${baseline.has(file) ? 'REGRESSION' : 'NEW'} ${file}: ${count} short sleeps > ${allowed}`);
    else if (count < allowed)
      failures.push(`LOWER ${file}: ${count} short sleeps < recorded ${allowed}; lower scripts/test_sleep_budget.tsv`);
  }
  for (const [file, allowed] of baseline)
    if (!counts.has(file) && allowed > 0)
      failures.push(`STALE ${file}: recorded ${allowed} but the file has none or is gone; remove the entry`);
  return failures;
}

function main() {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
  const files = execFileSync('git', ['ls-files', '--cached', '--others', '--exclude-standard'],
    { cwd: root, encoding: 'utf8' }).split('\n').filter(isTestSource);
  const counts = new Map();
  for (const file of files) {
    if (!existsSync(path.join(root, file))) continue;
    const count = countShortSleeps(readFileSync(path.join(root, file), 'utf8'));
    if (count > 0) counts.set(file, count);
  }
  if (process.argv.includes('--print')) {
    for (const [file, count] of [...counts].sort()) console.log(`${file}\t${count}`);
    return;
  }
  const baseline = parseBaseline(readFileSync(path.join(root, 'scripts', 'test_sleep_budget.tsv'), 'utf8'));
  const failures = checkSleepBudget(counts, baseline);
  if (failures.length) {
    for (const failure of failures) console.error(`test sleep budget: ${failure}`);
    process.exit(1);
  }
  const total = [...counts.values()].reduce((a, b) => a + b, 0);
  console.log(`test sleep budget: PASS (${total} recorded short sleeps in ${counts.size} files; none added)`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
