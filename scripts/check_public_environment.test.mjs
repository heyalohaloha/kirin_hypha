import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import test from 'node:test';
import { findEnvironmentText, scanEnvironment } from './check_public_environment.mjs';

test('private users, LAN addresses, hostnames and private names fail; public placeholders pass', () => {
  const samples = ['/' + 'Users/fixture-person/Music', 'C:' + '\\Users\\' + 'fixture-person\\file',
    ['192', '168', '10', '20'].join('.'), 'https://' + 'fixture-host' + '.local/', 'fixture-validator'];
  for (const text of samples) assert.ok(findEnvironmentText(text, ['fixture-validator']).length > 0);
  for (const text of ['/' + 'Users/<USER>/Music', '/home/' + 'runner/work', 'C:' + '\\Users\\' + 'Public\\file',
    'fixture/home/Library/Application Support', 'fixture/home/AppData/Local', '127.0.0.1', '#### ' + [10, 6, 3, 1].join('.') + ' Section', 'https://example.com', 'PRE POST AAX', 'notfixture-validator'])
    assert.deepEqual(findEnvironmentText(text, ['fixture-validator']), []);
});
test('scan covers tracked and untracked text, filenames and symlink targets, and ignores private ignored files', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-environment-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  execFileSync('git', ['init', '-q', root]);
  fs.writeFileSync(path.join(root, '.gitignore'), 'private/\n');
  fs.writeFileSync(path.join(root, 'tracked.txt'), 'safe');
  execFileSync('git', ['add', '.gitignore', 'tracked.txt'], { cwd: root });
  assert.deepEqual(scanEnvironment(root), []);
  const machine = 'fixture-machine';
  fs.writeFileSync(path.join(root, 'new.md'), machine);
  fs.mkdirSync(path.join(root, 'private')); fs.writeFileSync(path.join(root, 'private', 'secret.txt'), machine);
  fs.symlinkSync(machine, path.join(root, 'link'));
  fs.writeFileSync(path.join(root, machine), 'safe');
  const findings = scanEnvironment(root, [machine]);
  assert.ok(findings.some(f => f.path === 'new.md'));
  assert.ok(findings.some(f => f.path === 'link'));
  assert.ok(findings.some(f => f.location === 'path'));
  assert.ok(!findings.some(f => f.path.startsWith('private/')));
});

const certificateDeclarations = [
  ['scripts/ls_release/aax_bundle_verify.mjs', "export const AAX_APPLE_AUTHORITY = 'Developer ID Application: Fixture Signer (ABCDEFGHIJ)';"],
  ['scripts/windows/windows-aax-bundles.mjs', "const AUTHENTICODE_SUBJECT = 'CN=Fixture Signer';"],
  ['xtask/src/notarize.rs', 'const DEFAULT_IDENTITY: &str = "Developer ID Application: Fixture Signer (ABCDEFGHIJ)";'],
];
function certificateFixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-certificate-text-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  execFileSync('git', ['init', '-q', root]);
  for (const [relative, declaration] of certificateDeclarations) {
    const file = path.join(root, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, declaration + '\n');
  }
  return root;
}
test('public certificate declarations are allowed only in their verification source files', t => {
  const root = certificateFixture(t);
  assert.deepEqual(scanEnvironment(root, ['Fixture Signer']), []);
});
test('certificate exceptions do not allow names in comments, other declarations or other files', t => {
  const root = certificateFixture(t);
  for (const [relative, declaration] of certificateDeclarations) {
    const file = path.join(root, relative);
    for (const text of ['// ' + declaration, declaration + ' // Fixture Signer',
      declaration + '\n// Fixture Signer', declaration.replace(/const \w+/, 'const OTHER_IDENTITY')]) {
      fs.writeFileSync(file, text + '\n');
      assert.ok(scanEnvironment(root, ['Fixture Signer']).some(f => f.path === relative), relative);
    }
    fs.writeFileSync(file, declaration + '\n');
    const other = path.join(root, 'other.txt');
    fs.writeFileSync(other, declaration + '\n');
    assert.ok(scanEnvironment(root, ['Fixture Signer']).some(f => f.path === 'other.txt'));
    fs.rmSync(other);
  }
});
