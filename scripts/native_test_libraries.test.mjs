import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
test('CMake pools identical contexts, separates hooks, and preserves shipping sources', t => {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), 'native-libraries-'));
  t.after(() => fs.rmSync(fixture, { recursive: true, force: true }));
  for (const file of ['src/Body.cpp', 'tests/Main.cpp', 'JUCE/modules/juce_core/juce_core.cpp']) {
    fs.mkdirSync(path.dirname(path.join(fixture, file)), { recursive: true });
    fs.writeFileSync(path.join(fixture, file), '// Configure-only fixture; never compiled.\n');
  }
  const include = name => path.join(root, 'juce_shell/cmake', name).replaceAll('\\', '/');
  fs.writeFileSync(path.join(fixture, 'CMakeLists.txt'), `
cmake_minimum_required(VERSION 3.22)
project(NativeLibraryFixture LANGUAGES NONE)
# No compiler detection, try_compile, native build or JUCE bootstrap in this test.
set(CMAKE_CXX_COMPILE_OBJECT "false")
set(CMAKE_CXX_LINK_EXECUTABLE "false")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY "false")
set(CMAKE_CXX_ARCHIVE_CREATE "false")
set(CMAKE_CXX_ARCHIVE_FINISH "false")
foreach(module core recommended_config_flags recommended_warning_flags)
    add_library(juce_\${module} INTERFACE)
    add_library(juce::juce_\${module} ALIAS juce_\${module})
endforeach()
target_sources(juce_core INTERFACE "\${CMAKE_CURRENT_SOURCE_DIR}/JUCE/modules/juce_core/juce_core.cpp")
target_compile_definitions(juce_core INTERFACE JUCE_MODULE_AVAILABLE_juce_core=1)
foreach(target KirinFirstTests KirinSecondTests KirinHookTests)
    add_executable(\${target} tests/Main.cpp src/Body.cpp)
    target_compile_definitions(\${target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    set_property(TARGET \${target} PROPERTY JUCE_GENERATED_SOURCES_DIRECTORY "\${CMAKE_CURRENT_BINARY_DIR}/\${target}/generated")
    target_include_directories(\${target} PRIVATE "$<TARGET_PROPERTY:\${target},JUCE_GENERATED_SOURCES_DIRECTORY>")
    target_link_libraries(\${target} PRIVATE juce::juce_core)
endforeach()
target_compile_definitions(KirinHookTests PRIVATE TEST_HOOK=1)
# Absolute paths from portable helpers must be pooled too.
add_executable(KirinPortableTests tests/Main.cpp "\${CMAKE_CURRENT_SOURCE_DIR}/src/Body.cpp")
add_library(KirinHyphaPOST STATIC src/Body.cpp)
add_library(KirinNativeTestProcessor STATIC src/Body.cpp)
target_compile_definitions(KirinNativeTestProcessor PRIVATE KIRIN_HYPHA_TEST_WALL_CLOCK=1)
include("${include('NativeTestLibraries.cmake')}")
foreach(target KirinFirstTests KirinSecondTests KirinHookTests KirinPortableTests)
    get_target_property(sources \${target} SOURCES)
    if(NOT sources STREQUAL "tests/Main.cpp")
        message(FATAL_ERROR "unpooled product source in \${target}: \${sources}")
    endif()
    get_target_property(pool_\${target} \${target} LINK_LIBRARIES)
    list(FILTER pool_\${target} INCLUDE REGEX "^KirinNativeSources_")
endforeach()
if(NOT pool_KirinFirstTests STREQUAL pool_KirinSecondTests)
    message(FATAL_ERROR "identical sources/definitions compiled twice")
endif()
if(pool_KirinFirstTests STREQUAL pool_KirinHookTests)
    message(FATAL_ERROR "test hook profile merged with normal profile")
endif()
get_target_property(hook_defs \${pool_KirinHookTests} COMPILE_DEFINITIONS)
if(NOT "TEST_HOOK=1" IN_LIST hook_defs)
    message(FATAL_ERROR "hook lost from compiled library")
endif()
foreach(target KirinHyphaPOST KirinNativeTestProcessor)
    get_target_property(sources \${target} SOURCES)
    if(NOT sources STREQUAL "src/Body.cpp")
        message(FATAL_ERROR "processor sources changed")
    endif()
endforeach()
get_property(targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(module_count 0)
foreach(target IN LISTS targets)
    get_target_property(type \${target} TYPE)
    if(NOT type STREQUAL "INTERFACE_LIBRARY")
        set_property(TARGET \${target} PROPERTY LINKER_LANGUAGE CXX)
    endif()
    if(target MATCHES "^KirinNativeJuce_")
        math(EXPR module_count "\${module_count} + 1")
    endif()
endforeach()
if(NOT module_count EQUAL 1)
    message(FATAL_ERROR "JUCE was not compiled once for the shared profile")
endif()
include("${include('NativeTestTargets.cmake')}")
get_target_property(dependencies KirinNativeTests MANUALLY_ADDED_DEPENDENCIES)
foreach(target KirinFirstTests KirinSecondTests KirinHookTests KirinPortableTests)
    if(NOT target IN_LIST dependencies)
        message(FATAL_ERROR "aggregate omitted \${target}")
    endif()
endforeach()
`);
  const result = spawnSync('cmake', ['-S', fixture, '-B', path.join(fixture, 'build')], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stdout + result.stderr);
});
