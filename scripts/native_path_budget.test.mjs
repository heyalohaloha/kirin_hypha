import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { auditBuildPrefix, headroom, inspect, pathLimit, projectedLength, run, scan, temporaryAllowance, windowsBuildPrefix, windowsRelative } from './native_path_budget.mjs';

const oldBuildPrefix = 'D:\\a\\kirin_hypha\\kirin_hypha\\juce_shell\\build-windows';

function fixture(t) {
  const base = fs.mkdtempSync(path.join(os.tmpdir(), 'path-budget-'));
  t.after(() => fs.rmSync(base, { recursive: true, force: true }));
  return { base, root: path.join(base, 't'), ledger: path.join(base, 'path-ledger') };
}

test('budget includes staging and keeps forty characters at the exact boundary', () => {
  const room = pathLimit - headroom - projectedLength('x') + 1;
  assert.equal(temporaryAllowance, 13);
  const boundary = 'x'.repeat(room);
  assert.equal(inspect([boundary]).maximum, 219);
  assert.equal(inspect([boundary]).violations.length, 0);
  assert.equal(inspect([`${boundary}x`]).violations.length, 1);
});

test('UTF-16 units include both halves of non-BMP characters', () => {
  assert.equal(projectedLength('音/😀.json') - projectedLength('x/x.json'), 1);
});

test('only OS-specific test-home components map to their real Windows counterparts', () => {
  const mac = 's-123/home/Library/Application Support/Kirin OS';
  const win = 's-123/home/AppData';
  assert.equal(windowsRelative(`${mac}/plugin_data/file.json`), `${win}/Local/Kirin OS/plugin_data/file.json`);
  assert.equal(windowsRelative(`${mac}/identity.json`), `${win}/Roaming/Kirin OS/identity.json`);
  assert.equal(projectedLength(`${mac}/plugin_data/file.json`), projectedLength(`${win}/Local/Kirin OS/plugin_data/file.json`));
  const unrelated = 's-123/Library/Application Support/Kirin OS/file.json';
  assert.equal(windowsRelative(unrelated), unrelated);
  assert.equal(windowsRelative(`${win}/Local/Kirin OS/file.json`), `${win}/Local/Kirin OS/file.json`);
});

test('deepening a fixture or the configured root cannot evade the budget', () => {
  const relative = 'x'.repeat(219 - projectedLength('x') + 1);
  assert.equal(inspect([relative], 't').violations.length, 0);
  assert.equal(inspect([relative], 'test-kirin-os').violations.length, 1);
  assert.equal(inspect([`deeper/${relative}`]).violations.length, 1);
});

test('old Drop, hash preset and Rust claim paths fail with their old fixture prefix', () => {
  const prefix = 'kirin-hypha-audio-transparency-12345/home/AppData/Local/Kirin OS/plugin_data/';
  const uuid = '11111111-1111-4111-8111-111111111111';
  const paths = [
    `update-fixture-project/record_expected/drop_commits/by_session/${uuid}.json`,
    `reference/v2/library/presets/${'a'.repeat(64)}.json`,
    `update-fixture-project/record_expected/claims/update-fixture-bounce-1234567890123/${uuid}.json`,
  ];
  assert.equal(inspect(paths.map(p => prefix + p), 'test-kirin-os', oldBuildPrefix).violations.length, 3);
  const shortened = paths.map(p => `s-12345/home/AppData/Local/Kirin OS/plugin_data/${p}`
    .replace('update-fixture-project', 'up').replace('update-fixture-bounce', 'bounce'));
  assert.equal(inspect(shortened, 't', oldBuildPrefix).violations.length, 0);
});

test('pair-owner leases need compact fixture instance directories, not a weaker limit', () => {
  const uuid = '11111111-1111-4111-8111-111111111111';
  const lease = `.pair_owners/${uuid}-${'a'.repeat(32)}.lease`;
  const prefix = `s-12345/tmp/kirin/${uuid}/`;
  assert.equal(inspect([`${prefix}${uuid}/${lease}`], 't', oldBuildPrefix).violations.length, 1);
  assert.equal(inspect([`${prefix}iffff/${lease}`], 't', oldBuildPrefix).violations.length, 0);
});

test('real collision UUID lease paths fit the short CI root without changing identities', () => {
  const uuid = '11111111-1111-4111-8111-111111111111';
  const relative = `s-1234/tmp/kirin/${uuid}/${uuid}/.pair_owners/${uuid}-${'a'.repeat(32)}.lease`;
  assert.equal(projectedLength(relative, 't', oldBuildPrefix), 248);
  assert.equal(projectedLength(relative), 200);
  assert.equal(inspect([relative]).violations.length, 0);
  assert.equal(inspect([relative], 't', oldBuildPrefix).violations.length, 1);
  const physical = auditBuildPrefix('D:/kh/t', 'win32', () => `${oldBuildPrefix}\\t`);
  assert.equal(physical, oldBuildPrefix);
  assert.equal(inspect([relative], 't', physical).violations.length, 1);
  assert.throws(() => projectedLength(relative, 't', 'relative/build'));
});

test('all Windows CI consumers share the measured root and refuse existing output', () => {
  const workflow = fs.readFileSync(new URL('../.github/workflows/ci.yml', import.meta.url), 'utf8');
  const job = workflow.slice(workflow.indexOf('  windows-vst3-preflight:\n'));
  const configured = job.match(/^      HYPHA_WINDOWS_BUILD_DIR: (.+)$/m)?.[1];
  assert.equal(configured?.replaceAll('/', '\\'), windowsBuildPrefix);
  assert.doesNotMatch(job, /juce_shell\/build-windows/);
  const variable = '$env:HYPHA_WINDOWS_BUILD_DIR';
  for (const prefix of ['cmake -S juce_shell -B ', '--build-dir ', 'cmake --build ', '--artifact-dir '])
    assert.ok(job.includes(prefix + variable), prefix);
  assert.equal((job.match(/ctest --test-dir /g) || []).length, job.split('ctest --test-dir ' + variable).length - 1);
  assert.match(job, /if \(Test-Path -LiteralPath \$buildRoot\) \{ throw/);
  assert.match(job, /steps\.native-root\.outcome == 'success'/);
  assert.doesNotMatch(job, /Remove-Item[^\n]*\$buildRoot/);
});

test('scanner includes files, empty folders, and hidden temporaries', t => {
  const f = fixture(t); run('setup', f.root, f.ledger);
  fs.mkdirSync(path.join(f.root, 'empty'));
  fs.writeFileSync(path.join(f.root, '.pending'), '{}');
  assert.deepEqual(scan(f.root), ['.pending', 'empty']);
});

test('deleted Rust-shaped output remains checked through the teardown ledger', t => {
  const f = fixture(t); run('setup', f.root, f.ledger);
  const relative = `record_expected/claims/${'c'.repeat(200)}/receipt.json`;
  fs.mkdirSync(path.dirname(path.join(f.root, relative)), { recursive: true });
  fs.writeFileSync(path.join(f.root, relative), '{}');
  fs.writeFileSync(path.join(f.ledger, 'removed.json'), JSON.stringify(scan(f.root)));
  fs.rmSync(path.join(f.root, 'record_expected'), { recursive: true });
  assert.ok(run('cleanup', f.root, f.ledger).violations.some(entry => entry.relative === relative));
  run('setup', f.root, f.ledger);
  assert.ok(run('cleanup', f.root, f.ledger).violations.length > 0, 'setup cannot erase failed evidence');
});

test('missing setup, malformed ledger and missing root fail closed', t => {
  const f = fixture(t);
  assert.throws(() => run('cleanup', f.root, f.ledger));
  run('setup', f.root, f.ledger);
  fs.writeFileSync(path.join(f.ledger, 'bad.json'), '{}');
  assert.throws(() => run('cleanup', f.root, f.ledger), /invalid path ledger/);
  fs.unlinkSync(path.join(f.ledger, 'bad.json'));
  fs.rmSync(f.root, { recursive: true });
  assert.throws(() => run('cleanup', f.root, f.ledger));
});

test('ledger cannot escape the root or hide absolute paths', () => {
  for (const entry of ['../x', '/absolute', 'a/../x', 'a\\x', 'C:/file', '', 'a//b', 'a\0b'])
    assert.throws(() => inspect([entry]), /invalid relative/);
  assert.throws(() => inspect(['file'], '../root'), /invalid fixture root/);
});

test('links are measured without traversal and linked audit roots are refused', t => {
  const f = fixture(t); run('setup', f.root, f.ledger);
  const outside = path.join(f.base, 'outside'); fs.mkdirSync(outside);
  fs.writeFileSync(path.join(outside, 'untouched'), 'keep');
  fs.symlinkSync(outside, path.join(f.root, 'link'), 'junction');
  assert.deepEqual(scan(f.root), ['link']);
  assert.throws(() => run('setup', path.join(f.root, 'link'), path.join(f.root, 'ledger')), /linked/);
  assert.equal(fs.readFileSync(path.join(outside, 'untouched'), 'utf8'), 'keep');
});

test('CTest auto-selects the budget, fails overlong native output, and runs cleanup after failure', t => {
  const f = fixture(t), source = path.join(f.base, 'source'), build = path.join(f.base, 'build');
  fs.mkdirSync(source);
  const script = fileURLToPath(new URL('./native_path_budget.mjs', import.meta.url));
  const cmake = fileURLToPath(new URL('../juce_shell/cmake/NativePathBudget.cmake', import.meta.url));
  const write = path.join(source, 'write.mjs');
  fs.writeFileSync(write, `import fs from 'node:fs'; import path from 'node:path';
    fs.mkdirSync(path.join(process.env.KIRIN_HYPHA_PATH_BUDGET_ROOT, 'x'.repeat(220)));
    process.exit(Number(process.argv[2]));`);
  // Use real fixture wiring, substituting only the repository-relative script location.
  const cmakeText = fs.readFileSync(cmake, 'utf8').replace('${CMAKE_CURRENT_SOURCE_DIR}/../scripts/native_path_budget.mjs', script.replaceAll('\\', '/'));
  fs.writeFileSync(path.join(source, 'Budget.cmake'), cmakeText);
  fs.writeFileSync(path.join(source, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.22)
project(PathBudgetFixture NONE)
enable_testing()
set(KIRIN_HYPHA_NODE_EXECUTABLE "${process.execPath.replaceAll('\\', '/')}")
add_test(NAME native_ok COMMAND "${process.execPath.replaceAll('\\', '/')}" -e "process.exit(0)")
add_test(NAME native_long COMMAND "${process.execPath.replaceAll('\\', '/')}" "${write.replaceAll('\\', '/')}" 0)
add_test(NAME native_failed COMMAND "${process.execPath.replaceAll('\\', '/')}" "${write.replaceAll('\\', '/')}" 7)
set(kirin_hypha_native_tests native_ok native_long native_failed)
include(Budget.cmake)
`);
  const invoke = (cmd, args) => spawnSync(cmd, args, { encoding: 'utf8' });
  const config = invoke('cmake', ['-S', source, '-B', build]);
  assert.equal(config.status, 0, config.stderr);
  const ctest = name => invoke('ctest', ['--test-dir', build, '-C', 'Release', '--output-on-failure', '-R', `^${name}$`]);
  const ok = ctest('native_ok'); assert.equal(ok.status, 0, ok.stdout + ok.stderr);
  const long = ctest('native_long'); assert.notEqual(long.status, 0);
  assert.match(long.stdout, /kirin_native_path_budget_cleanup[^\n]*Failed/);
  fs.rmSync(path.join(build, 't'), { recursive: true });
  const fail = ctest('native_failed'); assert.notEqual(fail.status, 0);
  assert.match(fail.stdout, /native_failed[^\n]*Failed/);
  assert.match(fail.stdout, /kirin_native_path_budget_cleanup[^\n]*Failed/);
});

test('owned writers cannot reintroduce target-derived JUCE staging or unaudited teardown', () => {
  const repository = fileURLToPath(new URL('../', import.meta.url));
  for (const area of ['src', 'tests']) {
    const base = path.join(repository, 'juce_shell', area);
    for (const entry of fs.readdirSync(base, { recursive: true }).filter(name => /\.(h|cpp)$/.test(name))) {
      const text = fs.readFileSync(path.join(base, entry), 'utf8');
      assert.doesNotMatch(text, /\.replaceWith(?:Text|Data)\s*\(/, entry);
      assert.doesNotMatch(text, /getSiblingFile\s*\(\s*"\."\s*\+\s*juce::Uuid/, entry);
      assert.doesNotMatch(text, /juce::TemporaryFile\s+\w+\s*\([^;]*useHiddenFile|juce::TemporaryFile\s+\w+\s*\(\s*\w+\s*\);/, entry);
      if (area === 'tests' && entry !== 'TestPathBudget.h')
        assert.doesNotMatch(text, /\.deleteRecursively\s*\(/, entry);
    }
  }
});
