import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { checkPartition, inventory, isFixture } from './check_native_shards.mjs';

test('actual CTest intersections cover every body once and retain fixture dependencies', () => {
  const dir = mkdtempSync(join(tmpdir(), 'native-shards-'));
  try {
    writeFileSync(join(dir, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.22)
project(ShardFixture NONE)
enable_testing()
add_test(NAME setup COMMAND "\${CMAKE_COMMAND}" -E true)
add_test(NAME cleanup COMMAND "\${CMAKE_COMMAND}" -E true)
set_tests_properties(setup PROPERTIES FIXTURES_SETUP storage)
set_tests_properties(cleanup PROPERTIES FIXTURES_CLEANUP storage)
foreach(i RANGE 1 17)
 add_test(NAME body_\${i} COMMAND "\${CMAKE_COMMAND}" -E true)
 set_tests_properties(body_\${i} PROPERTIES FIXTURES_REQUIRED storage)
endforeach()
`);
    const configured = spawnSync('cmake', ['-S', dir, '-B', join(dir, 'build')], { encoding: 'utf8' });
    assert.equal(configured.status, 0, configured.stderr);
    const groups = [['^body_', '^body_([1-8])$'], ['^body_([1-8])$', '']];
    const read = (index = null) => groups.map(g => inventory(join(dir, 'build'), 'Release', ...g, index, 4));
    const whole = read();
    const shards = [1, 2, 3, 4].map(read);
    assert.equal(checkPartition(whole, shards).tests, 17);
    const mutate = fn => { const copy = structuredClone(shards); fn(copy); return copy; };
    assert.throws(() => checkPartition(whole, mutate(s => s[0][0] = s[0][0].filter(isFixture))), /missing/);
    assert.throws(() => checkPartition(whole, [...shards, shards[0]]), /duplicate/);
    assert.throws(() => checkPartition(whole, [...shards, whole.map(() => [])]), /empty/);
    assert.throws(() => checkPartition(whole, mutate(s => s[0][0] = s[0][0].filter(t => t.name !== 'setup'))), /fixture/);
    assert.throws(() => checkPartition(whole, mutate(s => s[0].pop())), /group/);
    assert.throws(() => checkPartition(whole, mutate(s => s[0][0].push({ name: 'unknown' }))), /unexpected/);
    assert.throws(() => checkPartition([whole[0], whole[0]], shards), /duplicate/);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
