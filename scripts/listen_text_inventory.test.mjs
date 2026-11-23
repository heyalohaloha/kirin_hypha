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

test('a format change during the LISTEN press asks for the press again, not for another PRE', () => {
  const japaneseNotices = read('juce_shell/src/HyphaJapaneseNotices.cpp');
  const layout = read('juce_shell/tests/ListenTextLayoutTest.h');
  const processor = read('juce_shell/src/PluginProcessorLiveCompare.cpp');
  const candidate = read('juce_shell/src/CandidateCompareMenu.h');
  const message = actions.match(/case StartResult::formatChanged:\s*return "([^"]+)";/)?.[1];
  assert.equal(message, 'Audio format changed. Press LISTEN again.');
  assert.ok(recovery.includes(`formatChangedStartNotice = "${message}";`), 'the ledger matches the action source exactly');
  const expectedJapanese = cases.match(/live_compare_ui::formatChangedStartNotice, u8"([^"]+)"/)?.[1];
  assert.equal(expectedJapanese, '形式が変わりました。LISTENを押し直してください');
  assert.ok(japaneseNotices.includes(`{ "${message}", u8"${expectedJapanese}" }`), 'the production Japanese notice matches');
  assert.match(cases, /add \(formatChangedStart\.english\);/);
  assert.match(layout, /i18n::tr \(listen_text::formatChangedStart\.english\)/);
  // Only a missing pair or a PRE that could not be opened with the same format opens the PRE menu.
  const choice = recovery.slice(recovery.indexOf('inline bool startNeedsPreChoice'), recovery.indexOf('inline const char* preUnavailableNotice'));
  assert.match(choice, /StartResult::noPair \|\| result == live_compare::StartResult::preUnavailable; \}/);
  assert.ok(!choice.includes('formatChanged'));
  assert.match(processor, /if \(! mapping->open \(key, rate, false\)\)[^\n]*\n\s*return std::get<2> \(format\(\)\) != std::get<2> \(started\) \? StartResult::formatChanged : StartResult::preUnavailable;/);
  assert.match(candidate, /StartResult::formatChanged\s*\? "A\/B could not start"/);
});

test('release-source runs the full LISTEN text layout gate', () => {
  const gate = read('scripts/test_release_source.sh');
  assert.match(gate, /\n  KirinListenTextLayoutTests\n/);
  const selected = gate.match(/JUCE_TEST_REGEX='([^']+)'/)[1];
  assert.ok(new RegExp(selected).test('kirin_listen_text_layout'));
});
