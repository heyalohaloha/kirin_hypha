import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { execFileSync, spawnSync } from 'node:child_process';

const root = path.resolve(import.meta.dirname, '..');
const hook = path.join(root, 'juce_shell/cmake/UpdateChecking.cmake');
const signer = path.join(root, 'scripts/macos/resign_plugin_bundle.sh');

test('the final plist mutation precedes an AU/VST3-only ad-hoc signature and verification', () => {
  const cmake = fs.readFileSync(hook, 'utf8');
  const mutation = cmake.indexOf('Add :KirinHyphaUpdateKeySha256');
  const signing = cmake.indexOf('resign_plugin_bundle.sh');
  assert.ok(mutation >= 0 && signing > mutation, 'bundle signing must follow the final update plist edit');
  assert.match(cmake.slice(mutation, signing), /if\(format STREQUAL "AU" OR format STREQUAL "VST3"\)/);
  assert.match(cmake.slice(signing), /TARGET_BUNDLE_DIR:\$\{target\}_\$\{format\}/);
  const script = fs.readFileSync(signer, 'utf8');
  assert.match(script, /codesign --force --sign - /);
  assert.match(script, /codesign --verify --strict /);
});

test('actual AU and VST3 fixture bundles verify after their plist changes', { skip: process.platform !== 'darwin' }, t => {
  const parent = path.join(root, 'target/test-fixtures');
  fs.mkdirSync(parent, { recursive: true });
  const tmp = fs.mkdtempSync(path.join(parent, 'adhoc-'));
  t.after(() => fs.rmSync(tmp, { recursive: true, force: true }));
  assert.notEqual(spawnSync('/bin/sh', [signer], { stdio: 'pipe' }).status, 0, 'missing bundle is rejected');
  assert.notEqual(spawnSync('/bin/sh', [signer, path.join(tmp, 'fixture.aaxplugin')], { stdio: 'pipe' }).status, 0,
    'AAX is outside the ad-hoc signing path');
  for (const format of ['component', 'vst3']) for (const role of ['PRE', 'POST']) {
    const bundle = path.join(tmp, `Fixture ${role}.${format}`);
    const contents = path.join(bundle, 'Contents'), executable = path.join(contents, 'MacOS', 'Fixture');
    fs.mkdirSync(path.dirname(executable), { recursive: true });
    fs.copyFileSync('/usr/bin/true', executable);
    fs.chmodSync(executable, 0o755);
    const plist = path.join(contents, 'Info.plist');
    fs.writeFileSync(plist, `<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.example.fixture.${role}.${format}</string><key>CFBundleExecutable</key><string>Fixture</string><key>CFBundlePackageType</key><string>BNDL</string><key>CFBundleName</key><string>Fixture</string></dict></plist>\n`);
    execFileSync('/usr/bin/codesign', ['--force', '--sign', '-', bundle], { stdio: 'pipe' });
    execFileSync('/usr/bin/codesign', ['--verify', '--strict', bundle], { stdio: 'pipe' });
    execFileSync('/usr/libexec/PlistBuddy', ['-c', 'Add :FixtureRevision integer 2', plist], { stdio: 'pipe' });
    assert.notEqual(spawnSync('/usr/bin/codesign', ['--verify', '--strict', bundle], { stdio: 'pipe' }).status, 0,
      'the plist change must invalidate the old signature');
    execFileSync('/bin/sh', [signer, bundle], { stdio: 'pipe' });
    execFileSync('/usr/bin/codesign', ['--verify', '--strict', bundle], { stdio: 'pipe' });
    execFileSync('/usr/libexec/PlistBuddy', ['-c', 'Set :FixtureRevision 3', plist], { stdio: 'pipe' });
    assert.notEqual(spawnSync('/usr/bin/codesign', ['--verify', '--strict', bundle], { stdio: 'pipe' }).status, 0,
      'verification must still detect a later mutation');
  }
});

test('CMake builds and verifies both final bundle formats after plist edits', { skip: process.platform !== 'darwin' }, t => {
  const parent = path.join(root, 'target/test-fixtures');
  fs.mkdirSync(parent, { recursive: true });
  const tmp = fs.mkdtempSync(path.join(parent, 'adhoc-cmake-'));
  t.after(() => fs.rmSync(tmp, { recursive: true, force: true }));
  const source = path.join(tmp, 'juce_shell'), scripts = path.join(tmp, 'scripts/macos');
  fs.mkdirSync(source, { recursive: true }); fs.mkdirSync(scripts, { recursive: true });
  fs.copyFileSync(signer, path.join(scripts, path.basename(signer)));
  fs.writeFileSync(path.join(source, 'fixture.c'), 'int fixture(void) { return 1; }\n');
  fs.writeFileSync(path.join(source, 'Info.plist'), `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.example.fixture</string>
<key>CFBundleExecutable</key><string>\u0040MACOSX_BUNDLE_EXECUTABLE_NAME\u0040</string>
<key>CFBundlePackageType</key><string>BNDL</string><key>AudioComponents</key><array><dict/></array></dict></plist>\n`);
  const include = hook.replaceAll('\\', '/').replaceAll('"', '\\"');
  fs.writeFileSync(path.join(source, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.20)
project(SignatureFixture C)
include("${include}")
set(KIRIN_HYPHA_UPDATE_PUBLIC_KEY "")
set(KIRIN_HYPHA_UPDATE_KEY_SHA256 "fixture")
foreach(format AU VST3)
  add_library(Mock_\u0024{format} MODULE fixture.c)
  if(format STREQUAL "AU")
    set(extension component)
  else()
    set(extension vst3)
  endif()
  set_target_properties(Mock_\u0024{format} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION \u0024{extension}
    MACOSX_BUNDLE_INFO_PLIST "\u0024{CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
  add_custom_command(TARGET Mock_\u0024{format} POST_BUILD
    COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:Mock_\u0024{format}>")
endforeach()
hypha_au_resource_usage(Mock)
`);
  const build = path.join(tmp, 'build');
  execFileSync('cmake', ['-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release'], { stdio: 'pipe' });
  execFileSync('/usr/bin/nice', ['-n', '10', 'cmake', '--build', build, '--parallel', '6', '--target',
    'Mock_AU', 'Mock_VST3'], { stdio: 'pipe', env: { ...process.env, CMAKE_BUILD_PARALLEL_LEVEL: '6' } });
  for (const extension of ['component', 'vst3']) {
    const bundles = fs.readdirSync(build).filter(name => name.endsWith(`.${extension}`));
    assert.equal(bundles.length, 1, `one ${extension} fixture bundle`);
    const bundle = path.join(build, bundles[0]);
    execFileSync('/usr/bin/codesign', ['--verify', '--strict', bundle], { stdio: 'pipe' });
    const value = execFileSync('/usr/libexec/PlistBuddy', ['-c', 'Print :KirinHyphaUpdateKeySha256',
      path.join(bundle, 'Contents/Info.plist')], { encoding: 'utf8' }).trim();
    assert.equal(value, 'fixture');
  }
});
