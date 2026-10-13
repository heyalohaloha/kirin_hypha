import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';

export const isFixture = test => test.properties?.some(p =>
  (p.name === 'FIXTURES_SETUP' || p.name === 'FIXTURES_CLEANUP') && p.value.length > 0);
const bodies = tests => tests.filter(t => !isFixture(t)).map(t => t.name);

// Setup/cleanup intentionally recur: CTest adds their dependency closure in every selection.
// Every actual test body, across all groups, must belong to exactly one nonempty shard.
export function checkPartition(wholeGroups, shards) {
  if (!wholeGroups.length || !shards.length) throw new Error('empty partition');
  const all = wholeGroups.flatMap(bodies);
  if (!all.length || new Set(all).size !== all.length) throw new Error('empty or duplicate full selection');
  const seen = new Set();
  for (const groups of shards) {
    if (groups.length !== wholeGroups.length) throw new Error('missing shard group');
    const selected = groups.flatMap(bodies);
    if (!selected.length) throw new Error('empty shard');
    for (let i = 0; i < groups.length; ++i) {
      const expected = new Set(bodies(wholeGroups[i]));
      for (const name of bodies(groups[i])) {
        if (!expected.has(name)) throw new Error(`unexpected selection: ${name}`);
        if (seen.has(name)) throw new Error(`duplicate shard test: ${name}`);
        seen.add(name);
      }
      // Required fixture closure must survive sharding; omitting setup is not a speedup.
      const required = new Set(groups[i].flatMap(t => t.properties ?? [])
        .filter(p => p.name === 'FIXTURES_REQUIRED').flatMap(p => p.value));
      for (const fixture of wholeGroups[i].filter(isFixture)) {
        const needed = fixture.properties.some(p =>
          (p.name === 'FIXTURES_SETUP' || p.name === 'FIXTURES_CLEANUP') && p.value.some(v => required.has(v)));
        if (needed && !groups[i].some(t => t.name === fixture.name)) throw new Error(`missing fixture: ${fixture.name}`);
      }
    }
  }
  const missing = all.filter(n => !seen.has(n));
  if (missing.length) throw new Error(`missing shard tests: ${missing.join(', ')}`);
  return { tests: all.length, shards: shards.map(g => g.flatMap(bodies).length) };
}

export function inventory(build, config, include, exclude = '', index = null, count = null) {
  const args = ['--test-dir', build, '--build-config', config, '-N', '--show-only=json-v1', '-R', include];
  if (exclude) args.push('-E', exclude);
  if (index !== null) args.push('-I', `${index},,${count}`);
  const result = spawnSync('ctest', args, { encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`ctest -N failed: ${result.stderr}`);
  const data = JSON.parse(result.stdout);
  if (data.kind !== 'ctestInfo' || data.version?.major !== 1) throw new Error('unsupported CTest inventory');
  return data.tests;
}
export function main(args) {
  let build, config, count;
  const groups = [];
  while (args.length) {
    const option = args.shift();
    if (option === '--build-dir') build = args.shift();
    else if (option === '--config') config = args.shift();
    else if (option === '--shards') count = Number(args.shift());
    else if (option === '--group') groups.push([args.shift(), args.shift()]);
    else throw new Error(`unknown option: ${option}`);
  }
  if (!build || !config || !Number.isInteger(count) || count < 2 || !groups.length
      || groups.some(g => !g[0] || g[1] === undefined)) throw new Error('required build, config, shards and groups');
  const whole = groups.map(g => inventory(build, config, ...g));
  const shards = Array.from({ length: count }, (_, i) => groups.map(g => inventory(build, config, ...g, i + 1, count)));
  const result = checkPartition(whole, shards);
  console.log(`native partition: PASS (${result.tests} bodies; shards ${result.shards.join(', ')}; fixtures retained)`);
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try { main(process.argv.slice(2)); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
