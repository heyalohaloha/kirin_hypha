import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { catalogEnglish, findStale, findUntranslated, literals, looksLikeProse }
  from './check_screen_text.mjs';

// A small tree shaped like the repository: one catalog section, one screen source, one Rust
// source and the kept list.
function fixture({ catalog, screen, rust = '', kept = '' }) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-screen-text-'));
  const source = path.join(root, 'juce_shell', 'src');
  fs.mkdirSync(source, { recursive: true });
  fs.mkdirSync(path.join(root, 'scripts'));
  fs.mkdirSync(path.join(root, 'crates', 'kirin_measure', 'src'), { recursive: true });
  fs.writeFileSync(path.join(source, 'HyphaJapaneseFixture.cpp'),
    `const Entry entries[] = {\n${catalog}\n};\n`);
  fs.writeFileSync(path.join(source, 'Screen.cpp'), screen);
  fs.writeFileSync(path.join(root, 'crates', 'kirin_measure', 'src', 'lib.rs'), rust);
  fs.writeFileSync(path.join(root, 'scripts', 'screen_text_kept.tsv'), `# kept\n${kept}`);
  return root;
}

function withFixture(parts, check) {
  const root = fixture(parts);
  try { check(root); } finally { fs.rmSync(root, { recursive: true, force: true }); }
}

test('literals join adjacent pieces and read byte escapes as UTF-8', () => {
  const found = literals('auto a = "Choose a " "range"; // "comment"\nauto b = "\\xCE\\x94 up";\n');
  assert.deepEqual(found.map(literal => literal.text), ['Choose a range', 'Δ up']);
});

test('sentences and long capital statuses are prose; labels are not', () => {
  assert.equal(looksLikeProse('Choose history time range'), true);
  assert.equal(looksLikeProse('PRE REQUIRED FOR DELTA'), true);
  assert.equal(looksLikeProse('MAX TP'), false);
  assert.equal(looksLikeProse('LUFS'), false);
  assert.equal(looksLikeProse('ヘルプ help text'), false);
});

test('catalog entries, patterns, composed pieces and joined facts count as translated', () => {
  withFixture({
    catalog: [
      '{ "Choose history time range", u8"履歴" },',
      '{ "WARMING %1 S", u8"準備中 %1 S" },',
      '{ "Open Kirin OS.\\nSaved presets arrive.", u8"開く" },',
      '{ "BLIND STOPPED", u8"中止" },',
      '{ "RETURN A EXPLICITLY", u8"戻す" },',
    ].join('\n'),
    screen: [
      'a.setTooltip ("Choose history time range");',
      'b = "WARMING " + n + " S";',
      'c = "Open Kirin OS.\\n" "Saved presets arrive.";',
      'd = "BLIND STOPPED / RETURN A EXPLICITLY";',
    ].join('\n'),
  }, root => assert.deepEqual(findUntranslated(root), []));
});

test('new prose without Japanese is found, and a kept reason clears it', () => {
  const parts = {
    catalog: '{ "Choose history time range", u8"履歴" },',
    screen: 'a.setTooltip ("Choose a new range");\nb = "NEW STATUS IS HERE";\n',
  };
  withFixture(parts, root => assert.deepEqual(
    findUntranslated(root).map(finding => finding.text), ['Choose a new range', 'NEW STATUS IS HERE']));
  withFixture({ ...parts, kept: 'Choose a new range\tlegend\nNEW STATUS IS HERE\tlabel\n' },
    root => assert.deepEqual(findUntranslated(root), []));
});

test('a kept line needs its reason', () => {
  withFixture({ catalog: '', screen: '', kept: 'Choose a new range\n' },
    root => assert.throws(() => findUntranslated(root), /no reason/));
});

test('entries whose English left the source are stale; composed and engine English is present', () => {
  withFixture({
    catalog: [
      '{ "PAIR SELECT PRE", u8"選択" },',
      '{ "Failed to start record", u8"失敗" },',
      '{ "All Keep: %1 ready POSTs", u8"準備 %1" },',
      '{ "Gone from the source", u8"消えた" },',
    ].join('\n'),
    screen: 'a = "PAIR " + name;\nb = "SELECT PRE";\nc = "All Keep: " + n + " ready POST";\n',
    rust: 'let message = "Failed to start record";\n',
    kept: 'Kept but removed\tlegend\n',
  }, root => {
    assert.equal(catalogEnglish(root).english.length, 4);
    assert.deepEqual(findStale(root).map(entry => `${entry.kind}:${entry.english}`),
      ['catalog:Gone from the source', 'kept:Kept but removed']);
  });
});
