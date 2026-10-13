import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { checkBuildTargets } from './check_native_build_targets.mjs';
import { checkCompileSharing } from './check_native_compile_sharing.mjs';

const read = file => readFileSync(new URL(`../${file}`, import.meta.url), 'utf8');
const ci = read('.github/workflows/ci.yml');

function phaseInventory(workflow) {
  const body = workflow.split('  release-source:\n')[1]?.split('  test:\n')[0];
  assert.ok(body, 'parallel release source job missing');
  assert.match(body, /fail-fast: false/);
  assert.doesNotMatch(body, /^    needs:/m, 'phases must run independently');
  assert.match(body, /RELEASE_SOURCE_PHASE: \$\{\{ matrix\.phase }}/);
  assert.match(body, /bash scripts\/test_release_source\.sh --phase "\$RELEASE_SOURCE_PHASE"/);
  const phases = [...body.matchAll(/^          - phase: (\w+)$/gm)].map(m => m[1]);
  assert.equal(new Set(phases).size, phases.length, 'duplicate phase');
  const aggregate = workflow.split('  test:\n')[1]?.split('  auval-arm64:\n')[0];
  assert.match(aggregate, /name: release source contract \(macos\)/);
  assert.match(aggregate, /needs: \[release-source\]/);
  assert.match(aggregate, /always\(\)/);
  assert.match(aggregate, /RELEASE_SOURCE_RESULT: \$\{\{ needs.release-source.result }}/);
  assert.match(aggregate, /run: test "\$RELEASE_SOURCE_RESULT" = success/);
  return phases;
}

function plan(...args) {
  const result = spawnSync('/bin/bash', ['scripts/test_release_source.sh', ...args], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  return result.stdout.replace(/(kirin-hypha-(?:ui|observatory)-contract-)\d+/g, '$1PID');
}

test('CI phases exactly partition the local release gate command inventory', () => {
  const localPhases = plan('--list-phases').trim().split('\n');
  const phases = phaseInventory(ci);
  assert.deepEqual(phases, localPhases);
  assert.equal(plan('--plan'), phases.map(phase => plan('--phase', phase, '--plan')).join(''));
  const all = plan('--plan');
  for (const needle of ['assert_ignored_count parity 20', 'assert_ignored_count pairing_candidates 6',
    'assert_ignored_count record_auxiliary_cuts 1', 'check_ffi_symbols', 'check_native_test_inventory.mjs',
    'check_native_build_targets.mjs', '--repeat until-pass:2']) assert.ok(all.includes(needle), needle);
  assert.doesNotMatch(all, /: PASS/);
});

test('missing, duplicate and ungated phases fail the partition contract', () => {
  const local = plan('--list-phases').trim().split('\n');
  for (const bad of [ci.replace('          - phase: ffi\n            minutes: 30\n', ''),
    ci.replace('- phase: ffi', '- phase: rust'), ci.replace('needs: [release-source]', 'needs: []'),
    ci.replace('run: test "$RELEASE_SOURCE_RESULT" = success', 'run: true')]) {
    assert.throws(() => assert.deepEqual(phaseInventory(bad), local));
  }
  const unknown = spawnSync('/bin/bash', ['scripts/test_release_source.sh', '--phase', 'missing', '--plan'], { encoding: 'utf8' });
  assert.equal(unknown.status, 2);
  assert.match(unknown.stderr, /unknown release-source phase/);
});

test('Windows builds one aggregate graph before every existing CTest selection', () => {
  const windows = ci.split('  windows-vst3-preflight:\n')[1];
  assert.equal((windows.match(/cmake --build/g) ?? []).length, 1);
  assert.match(windows, /--target KirinWindowsPreflight\s+--parallel 2/);
  assert.ok(windows.indexOf('cmake --build') < windows.indexOf('ctest --test-dir'));
  const aggregate = read('juce_shell/cmake/NativeTestTargets.cmake');
  assert.match(aggregate, /DEPENDS KirinNativeTests KirinHyphaPRE_VST3 KirinHyphaPOST_VST3/);
  assert.match(aggregate, /get_property\(kirin_native_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS\)/);
  assert.match(aggregate, /type STREQUAL "EXECUTABLE" OR type STREQUAL "OBJECT_LIBRARY"/);
  assert.match(aggregate, /add_dependencies\(KirinNativeTests \$\{target}\)/);
});

test('the aggregate target inventory rejects omissions, duplicates and empty inputs', () => {
  assert.deepEqual(checkBuildTargets('First\nSecond\n', ['First']), { built: 2, required: 1 });
  assert.throws(() => checkBuildTargets('First\n', ['Second']), /misses/);
  assert.throws(() => checkBuildTargets('First\nFirst', ['First']), /duplicate/);
  assert.throws(() => checkBuildTargets('First', ['First', 'First']), /duplicate/);
  assert.throws(() => checkBuildTargets('', ['First']), /empty/);
});

const tu = (target, file, flags = '') => ({ file, command: `c++ ${flags} -o CMakeFiles/${target}.dir/unit.o -c ${file}` });
const shared = [tu('KirinNativeJuce_common', '/JUCE/modules/juce_core/juce_core.cpp'),
  tu('KirinNativeSources_common', '/juce_shell/src/Component.cpp')];
test('actual compile inventory catches repeated modules/product sources and test flag leakage', () => {
  assert.deepEqual(checkCompileSharing(shared), { sharedModules: 1, sharedSources: 1 });
  for (const file of ['/JUCE/modules/juce_core/juce_core.cpp', '/juce_shell/src/Component.cpp'])
    assert.throws(() => checkCompileSharing([...shared, tu('KirinExampleTests', file)]), /unshared/);
  assert.throws(() => checkCompileSharing([...shared, shared[0]]), /duplicate/);
  assert.throws(() => checkCompileSharing([...shared,
    tu('KirinHyphaPOST', '/juce_shell/src/PluginProcessor.cpp', '-DKIRIN_HYPHA_TEST_WALL_CLOCK=1')]), /shipping/);
  assert.throws(() => checkCompileSharing([...shared,
    tu('KirinNativeTestProcessor', '/juce_shell/src/PluginProcessor.cpp')]), /lost/);
  checkCompileSharing([...shared,
    tu('KirinNativeTestProcessor', '/juce_shell/src/PluginProcessor.cpp', '-DKIRIN_HYPHA_TEST_WALL_CLOCK=1'),
    tu('KirinNativeSources_hook', '/juce_shell/src/Component.cpp', '-DTEST_HOOK=1')]);
});

test('shared native libraries retain definitions and keep JUCE sources private', () => {
  const source = read('juce_shell/cmake/NativeTestLibraries.cmake');
  assert.match(source, /target_link_libraries\(\$\{library} PRIVATE \$\{modules}\)/);
  assert.match(source, /set_property\(TARGET \$\{library} PROPERTY INTERFACE_\$\{property}/);
  assert.match(source, /COMPILE_DEFINITIONS COMPILE_OPTIONS COMPILE_FEATURES INCLUDE_DIRECTORIES LINK_LIBRARIES/);
  assert.match(source, /string\(SHA256 profile "\$\{signature}"\)/);
  assert.doesNotMatch(read('juce_shell/cmake/ProductTestClock.cmake'), /--locked -j\d/);
});
