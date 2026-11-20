import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
const root = path.resolve(import.meta.dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8');

test('labels add all tests, preserve fixtures and never select or disable tests', () => {
  const text = read('juce_shell/cmake/NativeTestLabels.cmake');
  assert.match(text, /get_property\(kirin_label_tests DIRECTORY PROPERTY TESTS\)/);
  assert.match(text, /APPEND PROPERTY LABELS "hypha-all" "hypha-\$\{domain\}"/);
  assert.match(text, /set\(domain general\)/);
  assert.doesNotMatch(text, /DISABLED|FIXTURES_REQUIRED|TIMEOUT|remove_test|add_test/);
  const cmake = read('juce_shell/CMakeLists.txt');
  assert.ok(cmake.indexOf('include(cmake/NativeTestLabels.cmake)') > cmake.indexOf('include(cmake/NativeStorageGuard.cmake)'));
});

test('compiler cache is opt-in, refuses existing launchers, retains strict compiler identity', () => {
  const text = read('juce_shell/cmake/CompilerCache.cmake');
  assert.match(text, /option\(KIRIN_HYPHA_USE_CCACHE [^\n]+ OFF\)/);
  assert.match(text, /CMAKE_C_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER/);
  assert.match(text, /find_program\(KIRIN_HYPHA_CCACHE NAMES ccache REQUIRED\)/);
  assert.match(text, /CCACHE_COMPILERCHECK=content/);
  assert.doesNotMatch(text, /CCACHE_SLOPPINESS|DOWNLOAD|execute_process/);
});

test('cmake metadata fixture classifies unknown and safety tests without compiling or running them', t => {
  const tmp = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-labels-'));
  t.after(() => fs.rmSync(tmp, { recursive: true, force: true }));
  const names = ['kirin_native_storage_guard_setup', 'kirin_reference_page_switch', 'kirin_capture_work_attachment', 'future_unknown'];
  fs.writeFileSync(path.join(tmp, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.22)\nproject(Metadata NONE)\nenable_testing()\n${names.map(n => `add_test(NAME ${n} COMMAND never-run)`).join('\n')}\ninclude("${root}/juce_shell/cmake/NativeTestLabels.cmake")\n`);
  const r = spawnSync('cmake', ['-S', tmp, '-B', path.join(tmp, 'out')], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stderr);
  const generated = fs.readFileSync(path.join(tmp, 'out/CTestTestfile.cmake'), 'utf8');
  for (const [i, label] of ['safety', 'reference', 'record', 'general'].entries())
    assert.match(generated, new RegExp(`set_tests_properties\\([^\\n]*${names[i]}[^\\n]* PROPERTIES[^\\n]*LABELS "hypha-all;hypha-${label}"`));
});

test('compiler cache configuration fails closed and OFF preserves a caller launcher', t => {
  const tmp = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'hypha-cache-config-'));
  t.after(() => fs.rmSync(tmp, { recursive: true, force: true }));
  const helper = `${root}/juce_shell/cmake/CompilerCache.cmake`;
  const cases = [
    ['off', 'set(CMAKE_C_COMPILER_LAUNCHER caller)\n', 0],
    ['existing', 'set(KIRIN_HYPHA_USE_CCACHE ON)\nset(CMAKE_C_COMPILER_LAUNCHER caller)\n', 1],
    ['unsupported', 'set(KIRIN_HYPHA_USE_CCACHE ON)\nset(CMAKE_GENERATOR Xcode)\n', 1],
    ['enabled', 'set(KIRIN_HYPHA_USE_CCACHE ON)\nset(KIRIN_HYPHA_CCACHE /synthetic/not-executed/ccache)\n', 0],
  ];
  for (const [name, before, status] of cases) {
    const script = path.join(tmp, `${name}.cmake`);
    fs.writeFileSync(script, `cmake_minimum_required(VERSION 3.22)
${before}include("${helper}")\nfile(WRITE "${tmp}/${name}.txt" "\${CMAKE_C_COMPILER_LAUNCHER}")\n`);
    const r = spawnSync('cmake', ['-P', script], { encoding: 'utf8' });
    assert.equal(r.status === 0 ? 0 : 1, status, `${name}: ${r.stderr}`);
  }
  assert.equal(fs.readFileSync(path.join(tmp, 'off.txt'), 'utf8'), 'caller');
  assert.match(fs.readFileSync(path.join(tmp, 'enabled.txt'), 'utf8'), /CCACHE_COMPILERCHECK=content;\/synthetic\/not-executed\/ccache$/);
});
