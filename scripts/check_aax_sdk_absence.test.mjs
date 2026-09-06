import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { findAaxSdkEntries } from './check_aax_sdk_absence.mjs';

function withFixture(run) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-aax-absence-'));
  try {
    run(root);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

test('ordinary AAX planning and JUCE wrapper names are allowed', () => withFixture((root) => {
  fs.mkdirSync(path.join(root, 'docs'), { recursive: true });
  fs.mkdirSync(path.join(root, 'juce_shell', 'JUCE'), { recursive: true });
  fs.writeFileSync(path.join(root, 'docs', 'aax_phase_a.md'), 'AAX planning only\n');
  fs.writeFileSync(path.join(root, 'juce_shell', 'JUCE', 'juce_AAX_Wrapper.cpp'), '// wrapper\n');
  assert.deepEqual(findAaxSdkEntries(root), []);
}));

test('SDK directory is found even when a gitignore file would hide it', () => withFixture((root) => {
  fs.writeFileSync(path.join(root, '.gitignore'), 'vendor/AAX_SDK/\n');
  const directory = path.join(root, 'vendor', 'AAX_SDK', 'Interfaces', 'ACF');
  fs.mkdirSync(directory, { recursive: true });
  fs.writeFileSync(path.join(directory, 'placeholder.txt'), 'licensed material\n');
  assert.ok(findAaxSdkEntries(root).some((finding) => finding.path === 'vendor/AAX_SDK'));
}));

test('SDK-style header is found without relying on its parent directory', () => withFixture((root) => {
  const directory = path.join(root, 'third_party');
  fs.mkdirSync(directory, { recursive: true });
  fs.writeFileSync(path.join(directory, 'AAX_CEffectParameters.h'), '// licensed header\n');
  assert.deepEqual(findAaxSdkEntries(root), [{
    path: 'third_party/AAX_CEffectParameters.h',
    reason: 'AAX SDK-style source/header name',
  }]);
}));

test('tracked SDK-style header is found inside an ignored build directory', () => withFixture((root) => {
  execFileSync('git', ['init', '--quiet', root]);
  fs.writeFileSync(path.join(root, '.gitignore'), 'build/\n');
  const directory = path.join(root, 'build', 'generated');
  fs.mkdirSync(directory, { recursive: true });
  const header = path.join(directory, 'AAX_CEffectParameters.h');
  fs.writeFileSync(header, '// licensed header\n');
  execFileSync('git', ['-C', root, 'add', '--force', 'build/generated/AAX_CEffectParameters.h']);

  assert.deepEqual(findAaxSdkEntries(root), [{
    path: 'build/generated/AAX_CEffectParameters.h',
    reason: 'AAX SDK-style source/header name',
  }]);
}));

test('all tracked generated-directory forms bypass walk exclusions', () => withFixture((root) => {
  execFileSync('git', ['init', '--quiet', root]);
  fs.writeFileSync(path.join(root, '.gitignore'), 'build-*\ndist/\n');
  const paths = [
    'build-generated/AAX_CHostProcessor.h',
    'dist/Avid-AAX-SDK.tgz',
  ];
  for (const relative of paths) {
    fs.mkdirSync(path.dirname(path.join(root, relative)), { recursive: true });
    fs.writeFileSync(path.join(root, relative), 'licensed material\n');
    execFileSync('git', ['-C', root, 'add', '--force', relative]);
  }
  assert.deepEqual(findAaxSdkEntries(root).map(finding => finding.path), paths);
}));

test('broken Git metadata fails instead of becoming an empty tracked set', () => withFixture((root) => {
  fs.mkdirSync(path.join(root, '.git'));
  assert.throws(() => findAaxSdkEntries(root), /failed to identify repository root/);
}));
