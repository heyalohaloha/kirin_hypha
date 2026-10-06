#!/usr/bin/env node

// Source-only UI boundary gate. Runtime signing, persistence, budget and notification tickets
// are verified by the native update-worker tests; this gate prevents GUI polling from gaining
// I/O or release URLs. Run: node --test scripts/check_hypha_update_ui.mjs
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { catalogEnglish, findUntranslated } from './check_screen_text.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = name => fs.readFileSync(path.join(root, name), 'utf8');
const updateFile = 'juce_shell/src/PluginEditorUpdate.cpp';
const ui = read(updateFile);
const method = name => {
  const begin = ui.indexOf(`KirinHyphaEditor::${name}`);
  assert.notEqual(begin, -1, `${name} exists`);
  const end = ui.indexOf('\nvoid KirinHyphaEditor::', begin + 1);
  const otherEnd = ui.indexOf('\nbool KirinHyphaEditor::', begin + 1);
  return ui.slice(begin, Math.min(...[end, otherEnd, ui.length].filter(value => value >= 0)));
};
const code = source => source.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g, '');

test('only hosted editors configure the worker; startup never opts in', () => {
  const configure = method('configureUpdateChecking');
  assert.match(configure, /wrapperType_Undefined\) return;/);
  assert.ok(configure.indexOf('wrapperType_Undefined') < configure.indexOf('updateServicesForEditor'));
  assert.match(configure, /processorRef\.updateServicesForEditor\(\)\.get\s*\(\s*hypha::plugin_format::name\s*\(processorRef\.wrapperType\)\)/);
  assert.match(configure, /request\s*\(false\)/);
  assert.doesNotMatch(code(configure), /setEnabled|URL|launchInDefaultBrowser|File\s*\(/);
  assert.equal((code(ui).match(/request\s*\(/g) ?? []).length, 2); // startup + manual
});

test('timer reads cached state at most once a second without storage, HTTP or retry', () => {
  const refresh = code(method('refreshUpdateChecking'));
  assert.match(refresh, /now < nextUpdateSnapshotAt/);
  assert.match(refresh, /nextUpdateSnapshotAt = now \+ 1\.0/);
  assert.match(refresh, /updateChecker->snapshot\s*\(\)/);
  assert.doesNotMatch(refresh, /->request\s*\(|URL|File\b|InputStream|setEnabled|refreshLicense|Record|plugin_data/);
  assert.match(read('juce_shell/src/PluginEditorLifecycle.cpp'), /refreshUpdateChecking\s*\(\)/);
});

test('notification is claimed only when visible, outside Blind and action feedback', () => {
  const refresh = method('refreshUpdateChecking');
  assert.match(refresh, /isShowing\(\) && ! informationBlockedByBlind\(\) && now >= toastUntil/);
  assert.match(refresh, /informationAnchor\(\)\.isShowing\(\)/);
  assert.match(refresh, /mayPresent && observatoryView\.feedback\(\)\.isEmpty\(\)\s*&& state\.status == hypha::update::Status::available\s*&& updateChecker->claimNotification\(\)/);
  assert.ok(refresh.indexOf('claimNotification()') < refresh.lastIndexOf('showToast ('));
  assert.doesNotMatch(code(refresh), /AlertWindow|PopupMenu|launchInDefaultBrowser|dismiss\s*\(/);
});

test('manual checks use the daily cap and recheck after the asynchronous menu', () => {
  const menu = method('addUpdateCheckMenu');
  assert.match(menu, /nextAttemptAt > updateNow\(\)/);
  assert.match(menu, /manualUpdateAction, "Check for updates now", ! state\.busy && ! dailyLimit/);
  const action = method('handleUpdateCheckMenu');
  assert.match(action, /informationBlockedByBlind\(\)/);
  assert.match(action, /state\.nextAttemptAt > updateNow\(\)/);
  assert.match(action, /requestedUpdatePreference = ! state\.enabled/);
  assert.match(action, /setEnabled\s*\(requestedUpdatePreference\)/);
  assert.match(action, /request\s*\(true\)/);
  assert.match(action, /manualUpdateToken = updateChecker->request\s*\(true\)/);
  assert.match(method('refreshUpdateChecking'), /state\.manualCompletion >= manualUpdateToken/);
});

test('preference acknowledgement waits for persisted worker result and manual available always answers', () => {
  const refresh = method('refreshUpdateChecking');
  assert.match(refresh, /updatePreferenceToken != 0 && mayPresent\s*&& state\.preferenceCompletion >= updatePreferenceToken/);
  assert.match(refresh, /state\.preferenceCompletion > updatePreferenceToken/);
  assert.match(refresh, /showToast \("Update check setting changed in another Hypha window"\)/);
  assert.match(refresh, /! state\.preferenceSaved \|\| state\.preferenceValue != requestedUpdatePreference/);
  assert.doesNotMatch(refresh, /state\.enabled != requestedUpdatePreference/);
  assert.match(refresh, /showToast \("Update check preference could not be saved"\)/);
  assert.match(refresh, /if \(state\.status == hypha::update::Status::available\)\s*updateChecker->claimNotification\(\);\s*showToast \(updateResultToast \(state\)\)/);
  const action = method('handleUpdateCheckMenu');
  assert.match(action, /updatePreferenceToken = updateChecker->setEnabled \(requestedUpdatePreference\)/);
  assert.match(action, /showToast \("Changing update check preference\.\.\."\)/);
  assert.doesNotMatch(action, /showToast \(state\.enabled \? "Automatic update checks/);
  assert.doesNotMatch(refresh, /state\.revision|manualUpdateBusySeen|manualUpdateCheckedAt|manualUpdateNextAttemptAt/);
});

test('the existing fixed official download and copy routes remain explicit user actions', () => {
  const information = read('juce_shell/src/PluginEditorInformation.cpp');
  assert.match(information, /addUpdateCheckMenu\s*\(menu\)/);
  assert.match(information, /handleUpdateCheckMenu\s*\(result\)/);
  for (const action of ['downloadsEnglish', 'downloadsJapanese', 'copyEnglish', 'copyJapanese'])
    assert.ok(information.includes(`update::Action::${action}`));
  assert.match(information, /update::dispatch/);
  assert.doesNotMatch(ui, /https?:\/\//);
  assert.doesNotMatch(code(ui), /launchInDefaultBrowser|SystemClipboard/);
});

test('every new update sentence has Japanese and the catalog section is registered', () => {
  assert.equal(findUntranslated(root).filter(finding => finding.path === updateFile).length, 0);
  assert.match(read('juce_shell/src/HyphaJapaneseCatalog.h'), /noticeSection\(\),[^}]*\bupdateSection\(\) \};/);
  const catalog = catalogEnglish(root);
  for (const sample of ['New official version available: v1.1.51',
    'Development build; official version: v1.1.51',
    'Hypha v1.1.51 available; open PRE / POST for downloads',
    'Last update check: 2026-10-05 18:00', 'Next permitted check: 2026-10-06 18:00'])
    assert.ok(catalog.exact.has(sample) || catalog.patterns.some(pattern => pattern.test(sample)), sample);
  const seen = new Set();
  for (const english of catalog.english) {
    assert.ok(!seen.has(english), `one Japanese entry: ${english}`);
    seen.add(english);
  }
});
