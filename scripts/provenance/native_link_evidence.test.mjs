import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import test from 'node:test';

test('CMake appends target map options outside bundles and preserves platform flags', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-cmake-evidence-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const helper = path.join(import.meta.dirname, 'native_link_evidence.cmake');
  // Configure the target graph only; real MSVC map/PE checks belong to build-only CI.
  fs.writeFileSync(path.join(root, 'stub.cpp'), 'int fixture() { return 0; }');
  fs.writeFileSync(path.join(root, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.21)
project(KirinHypha LANGUAGES CXX)
set(MSVC TRUE)
foreach(role IN ITEMS PRE POST)
  add_library(KirinHypha\${role}_VST3 MODULE stub.cpp)
  target_link_options(KirinHypha\${role}_VST3 PRIVATE existing-target-option)
  file(GENERATE OUTPUT "\${CMAKE_BINARY_DIR}/\${role}.txt" CONTENT "$<TARGET_PROPERTY:KirinHypha\${role}_VST3,LINK_OPTIONS>")
endforeach()
file(WRITE "\${CMAKE_BINARY_DIR}/flags.txt" "\${CMAKE_MODULE_LINKER_FLAGS}")
`);
  const build = path.join(root, 'build');
  execFileSync('cmake', ['-S', root, '-B', build, `-DCMAKE_PROJECT_KirinHypha_INCLUDE=${helper}`, '-DCMAKE_MODULE_LINKER_FLAGS=existing-platform-flags'], { stdio: 'pipe' });
  assert.equal(fs.readFileSync(path.join(build, 'flags.txt'), 'utf8'), 'existing-platform-flags');
  for (const role of ['PRE','POST']) {
    assert.equal(fs.readFileSync(path.join(build, `${role}.txt`), 'utf8'), `existing-target-option;/MAP:${build}/link-evidence/KirinHypha${role}.map`);
  }
});
