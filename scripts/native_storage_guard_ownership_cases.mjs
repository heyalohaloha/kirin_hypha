import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { assertExclusionOwnership, exclusionPaths, readOwnedSources } from './native_storage_exclusions.mjs';
import { fixture } from './native_storage_guard_test_helpers.mjs';

export async function testOwnership(suite) {
await suite.test('owned native source cannot reference any excluded path even through another writer', t => {
  const f = fixture(t);
  assert.ok(assertExclusionOwnership() > 0);
  for (const relative of exclusionPaths) {
    const literal = JSON.stringify(relative);
    for (const code of [
      `std::fs::write(root.join(${literal}), [1]);`,
      `auto file = choice ? root.getChildFile("safe") : root.getChildFile(${literal}); file.replaceWithText("x");`,
      `auto file = root.getChildFile("safe"); file = root.getChildFile(${literal}); file.deleteFile();`,
      `const NAME: &str = ${literal}; helper(root.join(NAME));`,
    ]) assert.throws(() => assertExclusionOwnership(new Map([['new/directory/writer.cpp', code]])),
      { code: 'STORAGE_EXCLUSION_OWNERSHIP' }, relative);
  }
  for (const code of ['root.getChildFile("GPU" "Cache");', 'root.join("GPU" + "Cache");',
    String.raw`root.join("\x47PUCache");`, 'readOnly(root.join("Preferences"));',
    '// an unmatched " in a comment\nroot.join("GPUCache");'])
    assert.throws(() => assertExclusionOwnership(new Map([['new.cpp', code]])),
      { code: 'STORAGE_EXCLUSION_OWNERSHIP' });
  // Verify discovery too, rather than testing just an injected map of known files.
  const source = path.join(f.root, 'new', 'target', 'writer.rs');
  fs.mkdirSync(path.dirname(source), { recursive: true });
  fs.writeFileSync(source, 'fn write() { std::fs::write(root.join("folder-scan"), [1]); }');
  assert.throws(() => assertExclusionOwnership(readOwnedSources(f.root)),
    { code: 'STORAGE_EXCLUSION_OWNERSHIP' });
  fs.writeFileSync(source, 'fn write() { std::fs::write(root.join("new-guarded-place"), [1]); }');
  assert.equal(assertExclusionOwnership(readOwnedSources(f.root)), 1);
  for (const code of [String.raw`root.join("GPU" /* comment */ "Cache");`,
    String.raw`root.join("\107PUCache");`, String.raw`root.join(L"\x0047PUCache");`])
    assert.throws(() => assertExclusionOwnership(new Map([['new.cpp', code]])),
      { code: 'STORAGE_EXCLUSION_SYNTAX' });
  if (process.platform !== 'win32') {
    const alias = path.join(f.root, 'source-alias');
    const target = path.join(f.root, 'target'); fs.mkdirSync(target);
    fs.writeFileSync(path.join(target, 'writer.cpp'), 'root.getChildFile("GPUCache").replaceWithText("x");');
    fs.symlinkSync(target, alias, 'dir');
    assert.throws(() => readOwnedSources(f.root), { code: 'STORAGE_EXCLUSION_SOURCE_LINK' });
    fs.unlinkSync(alias);
    fs.symlinkSync(f.root, alias, 'dir');
    assert.throws(() => readOwnedSources(alias + path.sep), { code: 'STORAGE_EXCLUSION_SOURCE_LINK' });
    fs.unlinkSync(alias);
    fs.symlinkSync(path.join(f.root, 'missing-source'), alias);
    assert.throws(() => readOwnedSources(f.root), { code: 'STORAGE_EXCLUSION_SOURCE_LINK' });
    fs.unlinkSync(alias);
  }
  // Run the actual entry point against a disposable source checkout too.
  const scripts = path.join(f.root, 'scripts'); fs.mkdirSync(scripts);
  for (const name of ['native_storage_guard.mjs', 'native_storage_exclusions.mjs'])
    fs.copyFileSync(new URL(name, import.meta.url), path.join(scripts, name));
  const expression = `import {run} from ${JSON.stringify(pathToFileURL(path.join(scripts, 'native_storage_guard.mjs')).href)};
    run(process.argv[1], ${JSON.stringify(f.state)}, ${JSON.stringify(f.roots)});`;
  const invoke = mode => spawnSync(process.execPath, ['--input-type=module', '-e', expression, mode],
    { encoding: 'utf8' });
  assert.equal(invoke('setup').status, 0);
  fs.writeFileSync(source, 'fn write() { std::fs::write(root.join("GPUCache"), [1]); }');
  assert.match(invoke('cleanup').stderr, /STORAGE_EXCLUSION_OWNERSHIP/);
  fs.unlinkSync(f.state);
  assert.match(invoke('setup').stderr, /STORAGE_EXCLUSION_OWNERSHIP/);
  assert.equal(fs.existsSync(f.state), false);

});

}
