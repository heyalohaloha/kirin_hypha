import assert from 'node:assert/strict';
import test from 'node:test';
import { checkSleepBudget, countShortSleeps, isTestSource, parseBaseline } from './check_test_sleep_budget.mjs';

test('short sleeps are counted in every spelling and long sleeps are not', () => {
  assert.equal(countShortSleeps('std::this_thread::sleep_for (std::chrono::milliseconds (11));'), 1);
  assert.equal(countShortSleeps('std::this_thread::sleep_for(std::chrono::microseconds(500));'), 1);
  assert.equal(countShortSleeps('std::this_thread::sleep_for (5ms);'), 1);
  assert.equal(countShortSleeps('thread::sleep(Duration::from_millis(2));'), 1);
  assert.equal(countShortSleeps('std::thread::sleep(std::time::Duration::from_micros(10));'), 1);
  assert.equal(countShortSleeps('sleep_for (std::chrono::milliseconds (600)); thread::sleep(Duration::from_millis(250));'), 0);
  assert.equal(countShortSleeps('std::this_thread::sleep_until (deadline);'), 0);
});

test('only test sources are scanned', () => {
  for (const file of ['juce_shell/tests/a_test.cpp', 'juce_shell/tests/live_compare/Rig.h',
    'crates/kirin_measure/src/spectrum_runtime_tests.rs', 'crates/kirin_hypha_ffi/tests/parity.rs',
    'crates/kirin_measure/src/attack_runtime_test_support.rs'])
    assert.ok(isTestSource(file), file);
  for (const file of ['juce_shell/src/PluginProcessor.cpp', 'crates/kirin_measure/src/spectrum_runtime_worker.rs',
    'scripts/x.mjs'])
    assert.ok(!isTestSource(file), file);
});

test('the ratchet refuses new, increased, lowered-but-unrecorded and stale entries', () => {
  const baseline = parseBaseline('# file\tcount\na.rs\t2\nb.rs\t1\n');
  assert.deepEqual(checkSleepBudget(new Map([['a.rs', 2], ['b.rs', 1]]), baseline), []);
  assert.match(checkSleepBudget(new Map([['a.rs', 3], ['b.rs', 1]]), baseline)[0], /^REGRESSION a\.rs/);
  assert.match(checkSleepBudget(new Map([['a.rs', 2], ['b.rs', 1], ['c.rs', 1]]), baseline)[0], /^NEW c\.rs/);
  assert.match(checkSleepBudget(new Map([['a.rs', 1], ['b.rs', 1]]), baseline)[0], /^LOWER a\.rs/);
  assert.match(checkSleepBudget(new Map([['a.rs', 2]]), baseline)[0], /^STALE b\.rs/);
  assert.throws(() => parseBaseline('a.rs\tx\n'), /malformed/);
  assert.throws(() => parseBaseline('a.rs\t1\na.rs\t1\n'), /duplicate/);
});
