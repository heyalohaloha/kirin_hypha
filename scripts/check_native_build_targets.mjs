import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

export function checkBuildTargets(inventory, expected) {
  const actual = inventory.trim().split(/\s+/);
  if (!inventory.trim() || !expected.length) throw new Error('empty native build inventory');
  if (new Set(actual).size !== actual.length) throw new Error('duplicate native build target');
  if (new Set(expected).size !== expected.length) throw new Error('duplicate required build target');
  const missing = expected.filter(target => !actual.includes(target));
  if (missing.length) throw new Error(`native aggregate misses: ${missing.join(', ')}`);
  return { built: actual.length, required: expected.length };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    const [buildFlag, buildDir, configFlag, config, targetsFlag, ...targets] = process.argv.slice(2);
    if (buildFlag !== '--build-dir' || !buildDir || configFlag !== '--config' || !config
        || targetsFlag !== '--targets') throw new Error('expected --build-dir DIR --config CONFIG --targets TARGET...');
    const result = checkBuildTargets(readFileSync(join(buildDir, `native-build-targets-${config}.txt`), 'utf8'), targets);
    console.log(`native build inventory: PASS (${result.built} aggregated, ${result.required} required)`);
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
