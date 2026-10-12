import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = (path) => readFileSync(new URL(`../${path}`, import.meta.url), 'utf8');
const cases = read('juce_shell/tests/ListenTextCases.h');
const recovery = read('juce_shell/src/HyphaLiveCompareRecoveryText.h');
const named = read('juce_shell/src/PluginEditorLiveCompare.cpp');

test('LISTEN action messages remain in the full-text native inventory', () => {
  // The named status and Blind screens come from their production projections. Direct-action
  // sentences must also stay in the inventory, including entry failures before a session exists.
  const directFunctions = named.slice(named.indexOf('juce::String pinFailure'), named.indexOf('void KirinHyphaEditor::configureLiveCompare'));
  const directToasts = ['PluginEditorLiveCompare.cpp', 'PluginEditorLiveCompareAuto.cpp', 'PluginEditorLiveBlind.cpp']
    .map((file) => read(`juce_shell/src/${file}`));
  const lifecycle = read('juce_shell/src/PluginEditorLifecycle.cpp');
  directToasts.push(lifecycle.slice(lifecycle.indexOf('    if (isPost && liveCompareAuto.hiddenStopNoticePending)'),
    lifecycle.indexOf('void KirinHyphaEditor::commitEditorSizeStateIfSettled')));
  const messages = [...directFunctions.matchAll(/return "([^"]+)"/g),
    ...directToasts.flatMap((source) => [...source.matchAll(/(?:showToast|stopLiveCompareAuto) \("([^"]+)"\)/g)])]
    .map((match) => match[1]);
  for (const message of messages)
    assert.ok(cases.includes(`"${message}"`) || recovery.includes(`"${message}"`), `Uncovered LISTEN notice: ${message}`);
  for (const projection of ['namedPresentation', 'currentNamedPresentation', 'screenFor', 'autoReadinessNotice', 'admissionText', 'refusalText'])
    assert.ok(cases.includes(projection), `${projection} must feed the native text inventory`);
});


test('release-source runs the full LISTEN text layout gate', () => {
  const gate = read('scripts/test_release_source.sh');
  assert.match(gate, /\n  KirinListenTextLayoutTests\n/);
  const selected = gate.match(/JUCE_TEST_REGEX='([^']+)'/)[1];
  assert.ok(new RegExp(selected).test('kirin_listen_text_layout'));
});
