import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = (path) => readFileSync(new URL(`../${path}`, import.meta.url), 'utf8');
const cases = read('juce_shell/tests/ListenTextCases.h');
const recovery = read('juce_shell/src/HyphaLiveCompareRecoveryText.h');
const actions = read('juce_shell/src/PluginEditorCompareActions.cpp');
const liveCompare = read('juce_shell/src/PluginEditorLiveCompare.cpp');

test('LISTEN action messages remain in the full-text native inventory', () => {
  // The named status and Blind screens come from their production projections. Direct-action
  // sentences must also stay in the inventory, including entry failures before a session exists.
  const start = actions.indexOf('const char* hypha::live_compare_ui::startFailure');
  const end = actions.indexOf('void KirinHyphaEditor::requestListenStart');
  assert.ok(start >= 0 && end > start, 'startFailure must be present in the action source');
  const directFunctions = actions.slice(start, end);
  assert.ok([...directFunctions.matchAll(/return "([^"]+)"/g)].length > 0, 'startFailure messages must not be empty');
  const matchStart = liveCompare.indexOf('juce::String matchFailure (');
  const matchEnd = liveCompare.indexOf('\n}\n}', matchStart);
  assert.ok(matchStart >= 0 && matchEnd > matchStart, 'matchFailure must be present in the live compare source');
  const matchMessages = [...liveCompare.slice(matchStart, matchEnd).matchAll(/return "([^"]+)"/g)];
  assert.ok(matchMessages.length > 0, 'matchFailure messages must not be empty');
  const directToasts = ['PluginEditorCompareActions.cpp', 'PluginEditorLiveCompare.cpp', 'PluginEditorLiveCompareAuto.cpp', 'PluginEditorLiveBlind.cpp']
    .map((file) => read(`juce_shell/src/${file}`));
  const lifecycle = read('juce_shell/src/PluginEditorLifecycle.cpp');
  directToasts.push(lifecycle.slice(lifecycle.indexOf('    if (isPost && liveCompareAuto.hiddenStopNoticePending)'),
    lifecycle.indexOf('void KirinHyphaEditor::commitEditorSizeStateIfSettled')));
  const messages = [...directFunctions.matchAll(/return "([^"]+)"/g), ...matchMessages,
    ...directToasts.flatMap((source) => [...source.matchAll(/(?:showToast|stopLiveCompareAuto) \("([^"]+)"\)/g)])]
    .map((match) => match[1]);
  for (const message of messages)
    assert.ok(cases.includes(`"${message}"`) || recovery.includes(`"${message}"`), `Uncovered LISTEN notice: ${message}`);
  for (const projection of ['namedPresentation', 'currentNamedPresentation', 'currentListenPresentation', 'screenFor', 'autoReadinessNotice', 'admissionText', 'refusalText'])
    assert.ok(cases.includes(projection), `${projection} must feed the native text inventory`);
});


test('MATCH span refusals keep distinct recovery actions across both languages', () => {
  const japaneseNotices = read('juce_shell/src/HyphaJapaneseNotices.cpp');
  const layout = read('juce_shell/tests/ListenTextLayoutTest.h');
  for (const [reason, name] of [
    ['tooShort', 'matchSpanTooShortNotice'],
    ['wholeSpanLost', 'matchSpanLostNotice'],
  ]) {
    const message = liveCompare.match(new RegExp(`case MatchFailure::${reason}:\\s*return "([^"]+)";`))?.[1];
    assert.ok(message, `${reason} has a concrete source notice`);
    assert.ok(recovery.includes(`${name} = "${message}";`), `${reason} recovery ledger must match the source exactly`);
    const expectedJapanese = cases.match(new RegExp(`live_compare_ui::${name}, u8"([^"]+)"`))?.[1];
    assert.ok(expectedJapanese, `${reason} has an expected Japanese notice in the native inventory`);
    assert.ok(japaneseNotices.includes(`{ "${message}", u8"${expectedJapanese}" }`),
      `${reason} matches the production Japanese notice exactly`);
    assert.equal(message.includes('END'), reason === 'wholeSpanLost');
    assert.equal(expectedJapanese.includes('END'), reason === 'wholeSpanLost');
  }
  assert.match(cases, /for \(const auto& notice : matchSpanNotices\) add \(notice.english\);/);
  assert.match(layout, /for \(const auto& notice : listen_text::matchSpanNotices\)/);
  assert.match(layout, /! listen_text::isMatchSpanNotice \(item.text\)/);
});

test('release-source runs the full LISTEN text layout gate', () => {
  const gate = read('scripts/test_release_source.sh');
  assert.match(gate, /\n  KirinListenTextLayoutTests\n/);
  const selected = gate.match(/JUCE_TEST_REGEX='([^']+)'/)[1];
  assert.ok(new RegExp(selected).test('kirin_listen_text_layout'));
});
