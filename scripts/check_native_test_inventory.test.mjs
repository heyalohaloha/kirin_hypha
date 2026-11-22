import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, writeFileSync, rmSync, readFileSync, appendFileSync } from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { checkInventory as rawInventory, checkStorageFixtures, main } from './check_native_test_inventory.mjs';

const tests = (...names) => names.map(name => ({ name, properties: [
  { name: 'FIXTURES_REQUIRED', value: ['kirin_real_storage'] },
] }));
const group = (...names) => ({ tests: tests(...names), expected: names.length });

const fixtures = () => [
  ['kirin_native_storage_guard_setup', 'FIXTURES_SETUP', 'kirin_real_storage'],
  ['kirin_native_storage_guard_cleanup', 'FIXTURES_CLEANUP', 'kirin_real_storage'],
  ['kirin_native_path_budget_setup', 'FIXTURES_SETUP', 'kirin_path_budget'],
  ['kirin_native_path_budget_cleanup', 'FIXTURES_CLEANUP', 'kirin_path_budget'],
].map(([name, property, value]) => ({ name, properties: [{ name: property, value: [value] }] }));
// Selection unit cases add the four real fixture roles; direct registry tests below do not.
function checkInventory(registered, groups, exclusions) {
  const result = rawInventory(registered.length ? [...fixtures(), ...registered] : registered,
    groups.map(g => ({ tests: g.tests.length ? [...fixtures(), ...g.tests] : g.tests,
      expected: Number.isSafeInteger(g.expected) && g.expected > 0 ? g.expected + 4 : g.expected })), exclusions);
  return { registered: result.registered - 4, selected: result.selected - 4, excluded: result.excluded };
}

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
add_test(NAME kirin_native_path_budget_setup COMMAND "/not-executed")
add_test(NAME kirin_native_path_budget_cleanup COMMAND "/not-executed")
set_tests_properties(kirin_native_path_budget_setup PROPERTIES FIXTURES_SETUP kirin_path_budget)
set_tests_properties(kirin_native_path_budget_cleanup PROPERTIES FIXTURES_CLEANUP kirin_path_budget)
add_test(NAME existing COMMAND "/not-executed")
add_test(NAME new_product COMMAND "/not-executed")
set_tests_properties(kirin_native_storage_guard_setup PROPERTIES FIXTURES_SETUP kirin_real_storage)
set_tests_properties(kirin_native_storage_guard_cleanup PROPERTIES FIXTURES_CLEANUP kirin_real_storage)
set_tests_properties(existing new_product PROPERTIES FIXTURES_REQUIRED "kirin_real_storage;kirin_path_budget")
`);
    const build = path.join(root, 'build');
    const configured = spawnSync('cmake', ['-S', root, '-B', build], { encoding: 'utf8' });
    assert.equal(configured.status, 0, configured.stderr);
    const exclusions = path.join(root, 'exclusions.json');
    writeFileSync(exclusions, '[]');
    const run = (count, regex) => spawnSync(process.execPath, [checker, '--build-dir', build,
      '--config', 'Release', '--exclusions', exclusions, '--group', String(count), regex], { encoding: 'utf8' });
    const fixtureOnly = () => spawnSync(process.execPath, [checker, '--fixtures-only', '--build-dir', build, '--config', 'Release'], { encoding: 'utf8' });
    assert.equal(fixtureOnly().status, 0);
    const missed = run(5, '^existing$');
    assert.equal(missed.status, 1);
    assert.match(missed.stderr, /missing from release source inventory:\nnew_product/);
    const fixed = run(6, '^(existing|new_product)$');
    assert.equal(fixed.status, 0, fixed.stderr);
    assert.match(fixed.stdout, /6 registered, 6 selected, 0 reasoned exclusions/);
    const wrongCount = run(2, '^(existing|new_product)$');
    assert.equal(wrongCount.status, 1);
    assert.match(wrongCount.stderr, /count 6 does not match expected 2/);
    writeFileSync(exclusions, '[{"name":"new_product","reason":"separate measurement"}]');
    const excluded = run(5, '^existing$');
    assert.equal(excluded.status, 0, excluded.stderr);
    assert.match(excluded.stdout, /6 registered, 5 selected, 1 reasoned exclusions/);
    // CMake commands are case-insensitive: late ADD_TEST must not escape the final registry check.
    appendFileSync(path.join(root, 'CMakeLists.txt'), '\nADD_TEST(NAME late_added COMMAND "/not-executed")\n');
    assert.equal(spawnSync('cmake', ['-S', root, '-B', build]).status, 0);
    writeFileSync(exclusions, '[]');
    const unguarded = run(7, '^(existing|new_product|late_added)$');
    assert.equal(unguarded.status, 1);
    assert.equal(fixtureOnly().status, 1);
    assert.match(unguarded.stderr, /missing kirin_real_storage fixture: late_added/);
    appendFileSync(path.join(root, 'CMakeLists.txt'),
      'set_tests_properties(late_added PROPERTIES FIXTURES_REQUIRED kirin_real_storage)\n');
    assert.equal(spawnSync('cmake', ['-S', root, '-B', build]).status, 0);
    const guarded = run(7, '^(existing|new_product|late_added)$');
    assert.equal(guarded.status, 0, guarded.stderr);
    assert.equal(fixtureOnly().status, 0);
    appendFileSync(path.join(root, 'CMakeLists.txt'), '\nset_tests_properties(kirin_native_storage_guard_setup PROPERTIES FIXTURES_SETUP \"\" FIXTURES_REQUIRED kirin_real_storage)\n');
    assert.equal(spawnSync('cmake', ['-S', root, '-B', build]).status, 0);
    assert.equal(fixtureOnly().status, 1);

  } finally { rmSync(root, { recursive: true, force: true }); }
});

test('release selection includes the added native contracts, including renamed readability diagnostics', () => {
  const source = readFileSync(new URL('./test_release_source.sh', import.meta.url), 'utf8');
  const selection = new RegExp(source.match(/JUCE_TEST_REGEX='([^']+)'/)[1]);
  const added = ["kirin_blind_preference_trial", "kirin_reference_balance_alignment", "kirin_reference_balance_overlay", "kirin_reference_balance_approval", "kirin_reference_balance_target_sizes", "kirin_reference_balance_compact_fit", "kirin_reference_balance_follow", "kirin_reference_balance_range_label", "kirin_reference_change_notes_ui", "kirin_reference_metric_series", "kirin_reference_metric_scan", "kirin_reference_metric_job", "kirin_reference_metric_page_reader", "kirin_reference_metric_source_scan", "kirin_metric_needle", "kirin_metric_shared_zero", "kirin_reference_metric_set_runtime", "kirin_metric_face_contract", "kirin_reference_metric_set_source", "kirin_reference_change_notes", "kirin_readability_fixes_visual", "kirin_session_coverage_diagnostic", "kirin_storage_write_guard_startup", "kirin_pre_display_write_guard_startup"];
  for (const name of added) assert.ok(selection.test(name), `release selection omitted ${name}`);
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
  checkStorageFixtures([...fixtures(), ...tests('guarded')]);
});


test('initial source phase wait has release selection, build target and updated count', () => {
  const source = readFileSync(new URL('./test_release_source.sh', import.meta.url), 'utf8');
  const selection = new RegExp(source.match(/JUCE_TEST_REGEX='([^']+)'/)[1]);
  assert.ok(selection.test('kirin_reference_initial_source_phase_wait'));
  const targets = source.match(/JUCE_TEST_TARGETS=\(([\s\S]*?)\n\)/)[1].trim().split(/\s+/);
  assert.equal(targets.filter(name => name === 'KirinInitialSourcePhaseWaitTests').length, 1);
  assert.match(source, /^JUCE_TEST_COUNT=378$/m);
});


test('all LISTEN layout shards use fixed CTest count, arguments, timeout and release selectors', () => {
  const source = readFileSync(new URL('./test_release_source.sh', import.meta.url), 'utf8');
  const ci = readFileSync(new URL('../.github/workflows/ci.yml', import.meta.url), 'utf8');
  const sourceRegex = source.match(/JUCE_TEST_REGEX='([^']+)'/)[1];
  const windowsStep = ci.split('kirin_reaper_clock|kirin_reaper_correspondence')[1].split("'")[0];
  const windowsRegex = `^(kirin_reaper_clock|kirin_reaper_correspondence${windowsStep}`;
  const stem = 'kirin_listen_text_layout';
  const expected = new Map([
    [stem, ['--recovery-only']],
    [`${stem}_keep_tail`, ['--keep-tail-notice-only']],
    [`${stem}_footer`, ['--footer-only']],
    [`${stem}_blind_ui`, ['--blind-ui-only']],
    [`${stem}_blind_image`, ['--blind-image-only']],
    [`${stem}_footer_restore`, ['--footer-restoration-only']],
    ...Array.from({ length: 17 }, (_, n) => [`${stem}_blind_trial_${n}`, ['--blind-trial-layout-only', String(n), '17']]),
    ...Array.from({ length: 12 }, (_, n) => [`${stem}_paint_${n}`, ['--paint-only', String(n), '12']]),
  ]);
  const releaseArgs = source.match(/JUCE_TEST_TARGETS=\(([\s\S]*?)\n\)/)[1].trim().split(/\s+/);
  assert.equal(releaseArgs.filter(target => target === 'KirinListenTextLayoutTests').length, 1);
  assert.match(source, new RegExp(`^JUCE_TEST_COUNT=${344 + expected.size - 1}$`, 'm'));

  const parent = path.join(process.cwd(), 'target', 'native-inventory-fixtures');
  mkdirSync(parent, { recursive: true });
  const root = mkdtempSync(path.join(parent, 'listen-shards-'));
  const cmake = fileURLToPath(new URL('../juce_shell/cmake/ListenTextLayoutTests.cmake', import.meta.url));
  try {
    writeFileSync(path.join(root, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.22)
project(ListenLayoutInventoryFixture NONE)
enable_testing()
add_executable(KirinListenTextLayoutTests IMPORTED)
set_target_properties(KirinListenTextLayoutTests PROPERTIES IMPORTED_LOCATION "\${CMAKE_COMMAND}")
include("${cmake}")
`);
    const build = path.join(root, 'build');
    const configured = spawnSync('cmake', ['-S', root, '-B', build], { encoding: 'utf8' });
    assert.equal(configured.status, 0, configured.stderr);
    const inventory = (regex) => {
      const additional = regex ? ['-R', regex] : [];
      const result = spawnSync('ctest', ['--test-dir', build, '--show-only=json-v1', ...additional], { encoding: 'utf8' });
      assert.equal(result.status, 0, result.stderr);
      return JSON.parse(result.stdout).tests;
    };
    const sortedNames = (registered) => registered.map(({ name }) => name).sort();
    for (const selected of [undefined, sourceRegex, windowsRegex])
      assert.deepEqual(sortedNames(inventory(selected)), [...expected.keys()].sort());
    for (const { name, command, properties } of inventory()) {
      assert.deepEqual(command.slice(1), expected.get(name), name);
      assert.equal(properties.find(property => property.name === 'TIMEOUT')?.value, 300, name);
    }
    for (const selector of [sourceRegex, windowsRegex])
      for (const invalid of ['_blind_trial_17', '_paint_12', '_made_up'])
        assert.equal(new RegExp(selector).test(stem + invalid), false, stem + invalid);
  } finally { rmSync(root, { recursive: true, force: true }); }
});

test('all four known roles must exist with their own property, even if required is present', () => {
  for (const role of fixtures()) {
    assert.throws(() => checkStorageFixtures(fixtures().filter(t => t.name !== role.name)), /missing required fixture registration/);
    for (const properties of [[], tests('body')[0].properties,
      [{ name: role.properties[0].name, value: ['wrong'] }]]) {
      assert.throws(() => checkStorageFixtures(fixtures().map(t => t.name === role.name ? { ...t, properties } : t)), /known fixture missing/);
    }
  }
  assert.throws(() => checkStorageFixtures([]), /empty/);
  assert.throws(() => main(['--fixtures-only']), /required/);
  assert.throws(() => main(['--fixtures-only', '--build-dir', 'x', '--config', 'Release', '--group', '1', '^a$']), /no selection/);
});
