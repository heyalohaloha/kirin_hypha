#!/usr/bin/env node

// SDK-free gate for scripts/build_aax_universal.sh. The dry-run path validates argument and path
// handling plus command composition without running Cargo, CMake, lipo, wraptool, or macOS-only
// tools. Fixtures contain only the SDK directory marker required by the shared path gate.

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repositoryRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const scriptPath = path.join(repositoryRoot, 'scripts', 'build_aax_universal.sh');
const fixtureRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-aax-build-'));
const sdkRoot = path.join(fixtureRoot, 'external-sdk');
const fontPath = path.join(fixtureRoot, 'diagnostic-font.otf');
const invalidRoot = path.join(fixtureRoot, 'not-an-sdk');
const repositorySdkRoot = fs.mkdtempSync(path.join(repositoryRoot, '.aax-sdk-build-fixture-'));
fs.mkdirSync(path.join(sdkRoot, 'Interfaces', 'ACF'), { recursive: true });
fs.writeFileSync(fontPath, 'fixture');
fs.mkdirSync(path.join(invalidRoot), { recursive: true });
fs.mkdirSync(path.join(repositorySdkRoot, 'Interfaces', 'ACF'), { recursive: true });

const signingEnv = {
  KIRIN_AAX_PACE_ACCOUNT: 'fixture-account',
  KIRIN_AAX_PACE_CUSTOMER_NUMBER: 'fixture-customer-number',
  KIRIN_AAX_PACE_CUSTOMER_NAME: 'Fixture Signer',
  KIRIN_AAX_APPLE_SIGN_IDENTITY: 'Developer ID Application: Fixture (TEAM)',
  KIRIN_AAX_WRAPTOOL: '/fixture/wraptool',
};

function run(args, env = {}) {
  try {
    const output = execFileSync('bash', [scriptPath, ...args], {
      cwd: repositoryRoot,
      encoding: 'utf8',
      stdio: 'pipe',
      env: {
        ...process.env,
        KIRIN_AAX_PACE_ACCOUNT: '',
        KIRIN_AAX_PACE_CUSTOMER_NUMBER: '',
        KIRIN_AAX_PACE_CUSTOMER_NAME: '',
        KIRIN_AAX_APPLE_SIGN_IDENTITY: '',
        KIRIN_AAX_WRAPTOOL: '',
        ...env,
      },
    });
    return { status: 0, output };
  } catch (error) {
    return { status: error.status, output: `${error.stdout ?? ''}${error.stderr ?? ''}` };
  }
}

try {
  let result = run([]);
  assert.notEqual(result.status, 0);
  assert.match(result.output, /--sdk is required/);

  result = run(['--sdk', invalidRoot, '--license-confirmed']);
  assert.notEqual(result.status, 0);
  assert.match(result.output, /AAX SDK path is invalid/);

  result = run(['--sdk', repositorySdkRoot, '--license-confirmed']);
  assert.notEqual(result.status, 0);
  assert.match(result.output, /must remain outside the repository/);

  result = run(['--sdk', sdkRoot, '--dry-run']);
  assert.notEqual(result.status, 0);
  assert.match(result.output, /--license-confirmed is required/);

  result = run(['--sdk', sdkRoot, '--license-confirmed', '--dry-run']);
  assert.equal(result.status, 0);
  assert.match(result.output, /CMAKE_OSX_ARCHITECTURES=x86_64\\;arm64/);
  assert.match(result.output, /KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED=ON/);
  assert.match(result.output, /KIRIN_HYPHA_REQUIRE_AAX=ON/);
  assert.match(result.output, /--target aarch64-apple-darwin/);
  assert.match(result.output, /--target x86_64-apple-darwin/);
  assert.doesNotMatch(result.output, /wraptool/);

  result = run([
    '--sdk', sdkRoot,
    '--license-confirmed',
    '--sign',
    '--kimera-font', fontPath,
    '--kimera-license-confirmed',
    '--dry-run',
  ], signingEnv);
  assert.equal(result.status, 0);
  assert.match(result.output, /\/fixture\/wraptool sign/);
  assert.match(result.output, /--account \\<redacted\\>/);
  assert.match(result.output, /--customernumber \\<redacted\\>/);
  assert.match(result.output, /--customername \\<redacted\\>/);
  assert.match(result.output, /--signid \\<redacted\\>/);
  assert.doesNotMatch(result.output, /fixture-account|fixture-customer-number|Fixture Signer/);

  result = run([
    '--sdk', sdkRoot,
    '--license-confirmed',
    '--diagnostic-sign',
    '--dry-run',
  ], signingEnv);
  assert.equal(result.status, 0);
  assert.match(result.output, /--account \\<redacted\\>/);
  assert.match(result.output, /signed for diagnostics/);
  assert.doesNotMatch(result.output, /--require-kimera/);

  result = run([
    '--sdk', sdkRoot,
    '--license-confirmed',
    '--diagnostic',
    '--diagnostic-sign',
    '--dry-run',
  ]);
  assert.notEqual(result.status, 0);
  assert.match(result.output, /mutually exclusive/);

  console.log('build_aax_universal.sh gate: ok');
} finally {
  fs.rmSync(fixtureRoot, { recursive: true, force: true });
  fs.rmSync(repositorySdkRoot, { recursive: true, force: true });
}
