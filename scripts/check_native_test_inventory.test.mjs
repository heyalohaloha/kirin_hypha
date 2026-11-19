import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, writeFileSync, rmSync, readFileSync, appendFileSync } from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { checkInventory, checkStorageFixtures, main } from './check_native_test_inventory.mjs';

const tests = (...names) => names.map(name => ({ name, properties: [
  { name: 'FIXTURES_REQUIRED', value: ['kirin_real_storage'] },
] }));
const group = (...names) => ({ tests: tests(...names), expected: names.length });

test('all registered tests are selected across groups, with shared CTest fixtures counted once', () => {
  assert.deepEqual(checkInventory(tests('setup', 'a', 'b', 'cleanup'), [
    group('setup', 'a', 'cleanup'), group('setup', 'b', 'cleanup'),
  ], []), { registered: 4, selected: 4, excluded: 0 });
});

test('a newly registered test fails even when the old selected count still matches', () => {
  assert.throws(() => checkInventory(tests('a', 'new_product'), [group('a')], []), /new_product/);
});

test('an exact exclusion with a reason accounts for a separate benchmark', () => {
  assert.deepEqual(checkInventory(tests('a', 'benchmark'), [group('a')], [
    { name: 'benchmark', reason: 'Requires a controlled performance measurement environment.' },
  ]), { registered: 2, selected: 1, excluded: 1 });
});

test('missing reasons and wildcard exclusions cannot hide tests', () => {
  for (const exclusion of [{ name: 'benchmark' }, { name: 'benchmark', reason: ' ' },
    { name: 'bench.*', reason: 'separate measurement' }]) {
    assert.throws(() => checkInventory(tests('a', 'benchmark'), [group('a')], [exclusion]));
  }
});

test('stale, duplicate and already selected exclusions fail', () => {
  const excluded = { name: 'b', reason: 'separate measurement' };
  assert.throws(() => checkInventory(tests('a'), [group('a')], [excluded]), /stale exclusion/);
  assert.throws(() => checkInventory(tests('a', 'b'), [group('a')], [excluded, excluded]), /duplicate exclusion/);
  assert.throws(() => checkInventory(tests('a', 'b'), [group('a', 'b')], [excluded]), /selected and excluded/);
});

test('zero, off-by-one and noninteger expected counts fail', () => {
  for (const expected of [0, 2, 1.5, NaN]) {
    assert.throws(() => checkInventory(tests('a'), [{ tests: tests('a'), expected }], []), /count/);
  }
});

test('duplicate and unknown selected names fail', () => {
  assert.throws(() => checkInventory(tests('a'), [group('a', 'a')], []), /duplicate/);
  assert.throws(() => checkInventory(tests('a'), [group('b')], []), /not registered/);
  assert.throws(() => checkInventory(tests('a', 'a'), [group('a')], []), /duplicate/);
});

test('disabled selected tests cannot count as coverage', () => {
  const disabled = { ...tests('a')[0], properties: [...tests('a')[0].properties, { name: 'DISABLED', value: true }] };
  assert.throws(() => checkInventory([disabled], [group('a')], []), /disabled/);
});

test('empty or malformed inventories fail closed', () => {
  assert.throws(() => checkInventory([], [group('a')], []), /empty/);
  assert.throws(() => checkInventory(tests('a'), [], []), /groups/);
  assert.throws(() => checkInventory(tests('a'), [group()], []), /empty/);
  assert.throws(() => checkInventory([{}], [group('a')], []), /name/);
  assert.throws(() => checkInventory(tests('a'), [group('a')], {}), /array/);
  assert.throws(() => main([]), /required/);
  assert.throws(() => main(['--unknown']), /unknown/);
});

test('real CTest selection includes fixtures and detects an omitted product without executing it', () => {
  const parent = path.join(process.cwd(), 'target', 'native-inventory-fixtures');
  mkdirSync(parent, { recursive: true });
  const root = mkdtempSync(path.join(parent, 'case-'));
  const checker = fileURLToPath(new URL('./check_native_test_inventory.mjs', import.meta.url));
  try {
    writeFileSync(path.join(root, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.20)
project(InventoryFixture NONE)
enable_testing()
add_test(NAME kirin_native_storage_guard_setup COMMAND "/not-executed")
add_test(NAME kirin_native_storage_guard_cleanup COMMAND "/not-executed")
add_test(NAME existing COMMAND "/not-executed")
add_test(NAME new_product COMMAND "/not-executed")
set_tests_properties(kirin_native_storage_guard_setup PROPERTIES FIXTURES_SETUP kirin_real_storage)
set_tests_properties(kirin_native_storage_guard_cleanup PROPERTIES FIXTURES_CLEANUP kirin_real_storage)
set_tests_properties(existing new_product PROPERTIES FIXTURES_REQUIRED kirin_real_storage)
`);
    const build = path.join(root, 'build');
    const configured = spawnSync('cmake', ['-S', root, '-B', build], { encoding: 'utf8' });
    assert.equal(configured.status, 0, configured.stderr);
    const exclusions = path.join(root, 'exclusions.json');
    writeFileSync(exclusions, '[]');
    const run = (count, regex) => spawnSync(process.execPath, [checker, '--build-dir', build,
      '--config', 'Release', '--exclusions', exclusions, '--group', String(count), regex], { encoding: 'utf8' });
    const missed = run(3, '^existing$');
    assert.equal(missed.status, 1);
    assert.match(missed.stderr, /missing from release source inventory:\nnew_product/);
    const fixed = run(4, '^(existing|new_product)$');
    assert.equal(fixed.status, 0, fixed.stderr);
    assert.match(fixed.stdout, /4 registered, 4 selected, 0 reasoned exclusions/);
    const wrongCount = run(2, '^(existing|new_product)$');
    assert.equal(wrongCount.status, 1);
    assert.match(wrongCount.stderr, /count 4 does not match expected 2/);
    writeFileSync(exclusions, '[{"name":"new_product","reason":"separate measurement"}]');
    const excluded = run(3, '^existing$');
    assert.equal(excluded.status, 0, excluded.stderr);
    assert.match(excluded.stdout, /4 registered, 3 selected, 1 reasoned exclusions/);
    // CMake commands are case-insensitive: late ADD_TEST must not escape the final registry check.
    appendFileSync(path.join(root, 'CMakeLists.txt'), '\nADD_TEST(NAME late_added COMMAND "/not-executed")\n');
    assert.equal(spawnSync('cmake', ['-S', root, '-B', build]).status, 0);
    writeFileSync(exclusions, '[]');
    const unguarded = run(5, '^(existing|new_product|late_added)$');
    assert.equal(unguarded.status, 1);
    assert.match(unguarded.stderr, /missing kirin_real_storage fixture: late_added/);
    appendFileSync(path.join(root, 'CMakeLists.txt'),
      'set_tests_properties(late_added PROPERTIES FIXTURES_REQUIRED kirin_real_storage)\n');
    assert.equal(spawnSync('cmake', ['-S', root, '-B', build]).status, 0);
    const guarded = run(5, '^(existing|new_product|late_added)$');
    assert.equal(guarded.status, 0, guarded.stderr);

  } finally { rmSync(root, { recursive: true, force: true }); }
});

test('release selection includes the added native contracts, including renamed readability diagnostics', () => {
  const source = readFileSync(new URL('./test_release_source.sh', import.meta.url), 'utf8');
  const selection = new RegExp(source.match(/JUCE_TEST_REGEX='([^']+)'/)[1]);
  const added = ["kirin_blind_preference_trial", "kirin_reference_balance_alignment", "kirin_reference_balance_overlay", "kirin_reference_balance_approval", "kirin_reference_balance_target_sizes", "kirin_reference_balance_compact_fit", "kirin_reference_balance_follow", "kirin_reference_balance_range_label", "kirin_reference_change_notes_ui", "kirin_reference_metric_series", "kirin_reference_metric_scan", "kirin_reference_metric_job", "kirin_reference_metric_page_reader", "kirin_reference_metric_source_scan", "kirin_metric_needle", "kirin_metric_shared_zero", "kirin_reference_metric_set_runtime", "kirin_metric_face_contract", "kirin_reference_metric_set_source", "kirin_reference_change_notes", "kirin_readability_fixes_visual", "kirin_session_coverage_diagnostic", "kirin_storage_write_guard_startup", "kirin_pre_display_write_guard_startup"];
  for (const name of added) assert.ok(selection.test(name), `release selection omitted ${name}`);
  assert.equal(selection.test('kirin_look239_visual'), false);
  assert.equal(selection.test('kirin_unregistered_future_contract'), false);
});

test('final registry rejects unguarded, excluded, wrongly typed and invented fixture bodies', () => {
  for (const properties of [undefined, [], [{ name: 'FIXTURES_REQUIRED', value: 'kirin_real_storage' }],
    [{ name: 'FIXTURES_REQUIRED', value: ['other'] }],
    [{ name: 'FIXTURES_SETUP', value: ['invented'] }]]) {
    assert.throws(() => checkStorageFixtures([{ name: 'late_added', properties }]), /late_added/);
  }
  assert.throws(() => checkInventory([...tests('a'), { name: 'benchmark' }], [group('a')],
    [{ name: 'benchmark', reason: 'performance' }]), /missing kirin_real_storage/);
  assert.throws(() => checkStorageFixtures([{ name: 'kirin_native_storage_guard_setup' }]), /missing/);
  checkStorageFixtures([{ name: 'kirin_native_storage_guard_setup', properties: [
    { name: 'FIXTURES_SETUP', value: ['kirin_real_storage'] },
  ] }, ...tests('guarded')]);
});


test('initial source phase wait has release selection, build target and updated count', () => {
  const source = readFileSync(new URL('./test_release_source.sh', import.meta.url), 'utf8');
  const selection = new RegExp(source.match(/JUCE_TEST_REGEX='([^']+)'/)[1]);
  assert.ok(selection.test('kirin_reference_initial_source_phase_wait'));
  const targets = source.match(/JUCE_TEST_TARGETS=\(([\s\S]*?)\n\)/)[1].trim().split(/\s+/);
  assert.equal(targets.filter(name => name === 'KirinInitialSourcePhaseWaitTests').length, 1);
  assert.match(source, /^JUCE_TEST_COUNT=344$/m);
});
