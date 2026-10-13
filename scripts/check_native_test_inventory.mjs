import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

function testMap(tests, label) {
  if (!Array.isArray(tests) || tests.length === 0) throw new Error(`${label}: empty inventory`);
  const result = new Map();
  for (const test of tests) {
    if (typeof test.name !== 'string' || !test.name) throw new Error(`${label}: missing test name`);
    if (result.has(test.name)) throw new Error(`${label}: duplicate ${test.name}`);
    result.set(test.name, test);
  }
  return result;
}

// Compare CTest's actual selections, including automatically added fixtures, with ALL
// registered tests. A selected-count check alone cannot discover an omitted new test.
export function checkInventory(registered, groups, exclusions) {
  const all = testMap(registered, 'registered');
  if (!Array.isArray(groups) || groups.length === 0) throw new Error('missing selection groups');
  const selected = new Set();
  for (const { tests, expected } of groups) {
    const group = testMap(tests, 'selected');
    if (!Number.isSafeInteger(expected) || expected < 1 || group.size !== expected) {
      throw new Error(`selected count ${group.size} does not match expected ${expected}`);
    }
    for (const name of group.keys()) {
      if (!all.has(name)) throw new Error(`selected but not registered: ${name}`);
      if (all.get(name).properties?.some(p => p.name === 'DISABLED' && p.value === true)) {
        throw new Error(`selected test is disabled: ${name}`);
      }
      selected.add(name);
    }
  }
  if (!Array.isArray(exclusions)) throw new Error('exclusions must be an array');
  const excluded = new Set();
  for (const { name, reason } of exclusions) {
    if (typeof name !== 'string' || !name || typeof reason !== 'string' || !reason.trim()) {
      throw new Error('each exclusion needs an exact name and a reason');
    }
    if (excluded.has(name)) throw new Error(`duplicate exclusion: ${name}`);
    if (!all.has(name)) throw new Error(`stale exclusion: ${name}`);
    if (selected.has(name)) throw new Error(`selected and excluded: ${name}`);
    excluded.add(name);
  }
  const missing = [...all.keys()].filter(name => !selected.has(name) && !excluded.has(name));
  if (missing.length) throw new Error(`registered tests missing from release source inventory:\n${missing.join('\n')}`);
  return { registered: all.size, selected: selected.size, excluded: excluded.size };
}

function ctestInventory(buildDir, config, regex) {
  // Official machine-readable inventory; do not parse CMake source or human -N output.
  // https://cmake.org/cmake/help/latest/manual/ctest.1.html#show-as-json-object-model
  const args = ['--test-dir', buildDir, '--build-config', config, '--show-only=json-v1'];
  if (regex !== undefined) args.push('-R', regex);
  const result = spawnSync('ctest', args, { encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`CTest inventory failed: ${result.stderr || result.stdout}`);
  const json = JSON.parse(result.stdout);
  if (json.kind !== 'ctestInfo' || json.version?.major !== 1) throw new Error('unsupported CTest inventory');
  return json.tests;
}

export function main(args) {
  let buildDir, config, exclusionPath;
  const selections = [];
  while (args.length) {
    const option = args.shift();
    if (option === '--build-dir') buildDir = args.shift();
    else if (option === '--config') config = args.shift();
    else if (option === '--exclusions') exclusionPath = args.shift();
    else if (option === '--group') selections.push({ expected: Number(args.shift()), regex: args.shift() });
    else throw new Error(`unknown option: ${option}`);
  }
  if (!buildDir || !config || !exclusionPath || !selections.length
      || selections.some(s => !s.regex || !Number.isSafeInteger(s.expected) || s.expected < 1)) {
    throw new Error('required: --build-dir DIR --config CONFIG --exclusions JSON --group COUNT REGEX [--group COUNT REGEX]');
  }
  const registered = ctestInventory(buildDir, config);
  const groups = selections.map(s => ({ expected: s.expected, tests: ctestInventory(buildDir, config, s.regex) }));
  const summary = checkInventory(registered, groups, JSON.parse(readFileSync(exclusionPath, 'utf8')));
  console.log(`native inventory: PASS (${summary.registered} registered, ${summary.selected} selected, ${summary.excluded} reasoned exclusions)`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try { main(process.argv.slice(2)); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
