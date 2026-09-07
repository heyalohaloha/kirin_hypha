#!/usr/bin/env node

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repositoryRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const modulePath = path.join(repositoryRoot, 'juce_shell', 'cmake', 'KirinHyphaAax.cmake');
const fixtureRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-aax-cmake-'));
const sdkRoot = path.join(fixtureRoot, 'External AAX SDK');
const repositorySdkRoot = fs.mkdtempSync(path.join(repositoryRoot, '.aax-sdk-gate-fixture-'));
const repositorySdkSymlink = path.join(repositoryRoot, `.aax-sdk-link-fixture-${process.pid}`);
fs.mkdirSync(path.join(sdkRoot, 'Interfaces', 'ACF'), { recursive: true });
fs.mkdirSync(path.join(repositorySdkRoot, 'Interfaces', 'ACF'), { recursive: true });

const harness = path.join(fixtureRoot, 'gate.cmake');
fs.writeFileSync(harness, `include("${modulePath}")\nkirin_hypha_configure_aax(ENABLED)\nmessage(STATUS "gate-enabled=\${ENABLED}")\n`);

function run(definitions) {
  const args = Object.entries(definitions).map(([key, value]) => `-D${key}=${value}`);
  try {
    const output = execFileSync('cmake', [...args, '-P', harness], { encoding: 'utf8', stdio: 'pipe' });
    return { status: 0, output };
  } catch (error) {
    return { status: error.status, output: `${error.stdout ?? ''}${error.stderr ?? ''}` };
  }
}

try {
  let result = run({ CMAKE_SYSTEM_NAME: 'Darwin' });
  assert.equal(result.status, 0);
  assert.match(result.output, /gate-enabled=OFF/);

  result = run({ CMAKE_SYSTEM_NAME: 'Darwin', KIRIN_HYPHA_REQUIRE_AAX: 'ON' });
  assert.notEqual(result.status, 0);
  assert.match(result.output, /requires KIRIN_HYPHA_AAX_SDK_PATH/);

  result = run({ CMAKE_SYSTEM_NAME: 'Darwin', KIRIN_HYPHA_AAX_SDK_PATH: sdkRoot });
  assert.notEqual(result.status, 0);
  assert.match(result.output, /LICENSE_CONFIRMED=ON/);

  result = run({
    CMAKE_SYSTEM_NAME: 'Darwin',
    KIRIN_HYPHA_AAX_SDK_PATH: path.join(fixtureRoot, 'missing'),
    KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED: 'ON',
  });
  assert.notEqual(result.status, 0);
  assert.match(result.output, /AAX SDK path is invalid/);

  result = run({
    CMAKE_SYSTEM_NAME: 'Linux',
    KIRIN_HYPHA_AAX_SDK_PATH: sdkRoot,
    KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED: 'ON',
  });
  assert.notEqual(result.status, 0);
  assert.match(result.output, /supported only on macOS and Windows/);

  result = run({
    CMAKE_SYSTEM_NAME: 'Darwin',
    KIRIN_HYPHA_AAX_SDK_PATH: repositorySdkRoot,
    KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED: 'ON',
  });
  assert.notEqual(result.status, 0);
  assert.match(result.output, /must remain outside/);

  const externalSdkSymlink = path.join(fixtureRoot, 'repository-sdk-link');
  fs.symlinkSync(repositorySdkRoot, externalSdkSymlink, 'dir');
  result = run({
    CMAKE_SYSTEM_NAME: 'Darwin',
    KIRIN_HYPHA_AAX_SDK_PATH: externalSdkSymlink,
    KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED: 'ON',
  });
  assert.notEqual(result.status, 0);
  assert.match(result.output, /must remain outside/);

  fs.symlinkSync(sdkRoot, repositorySdkSymlink, 'dir');
  result = run({
    CMAKE_SYSTEM_NAME: 'Darwin',
    KIRIN_HYPHA_AAX_SDK_PATH: repositorySdkSymlink,
    KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED: 'ON',
  });
  assert.notEqual(result.status, 0);
  assert.match(result.output, /must remain outside/);

  for (const system of ['Darwin', 'Windows']) {
    result = run({
      CMAKE_SYSTEM_NAME: system,
      KIRIN_HYPHA_AAX_SDK_PATH: sdkRoot,
      KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED: 'ON',
    });
    assert.equal(result.status, 0);
    assert.match(result.output, /gate-enabled=ON/);
  }

  console.log('AAX CMake gate tests passed (10 cases)');
} finally {
  fs.rmSync(fixtureRoot, { recursive: true, force: true });
  fs.rmSync(repositorySdkSymlink, { force: true });
  fs.rmSync(repositorySdkRoot, { recursive: true, force: true });
}
