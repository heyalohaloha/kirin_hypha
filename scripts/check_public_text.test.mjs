import assert from 'node:assert/strict';
import test from 'node:test';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';

import { isRetainedUpstreamNotice, PATTERNS, addedLines, findRecords, namePatterns, newLines } from './check_public_text.mjs';

const kinds = (text, patterns = PATTERNS) => findRecords(text, patterns).map((record) => record.kind);

test('attributions to a person are found, a bare owner in code is not', () => {
  assert.deepEqual(kinds('stay in English (owner, 2026-10-05).'), ['attribution']);
  assert.deepEqual(kinds('chosen by the owner on 2026-10-04 after a review'), ['attribution']);
  assert.deepEqual(kinds('持ち主の実素材16曲47版で'), ['attribution']);
  assert.deepEqual(kinds('owner->getProperties().set ("waiting", next);'), []);
  assert.deepEqual(kinds('出力の持ち主の表（OutputOwnership.h）'), []);
});

test('internal branches, work items, plan numbers and paths are found', () => {
  assert.deepEqual(kinds('INV-S45 is used by `claude/hypha-light-stage2`'), ['internal branch']);
  assert.deepEqual(kinds('see .claude/worktrees for checkouts'), []);
  assert.deepEqual(kinds('fixed in W-1234'), ['work item']);
  assert.deepEqual(kinds('left for X4'), ['review item']);
  assert.deepEqual(kinds('closes HY-6 and F-H05, TH-13, KO-2'), ['plan number']);
  assert.deepEqual(kinds('INV-S47 and B-1240 are public'), []);
  assert.deepEqual(kinds('read /Users/someone/Music/song.wav'), ['local path']);
  assert.deepEqual(kinds('Kirin OS (native/src/reference_analysis.rs)'), ['Kirin OS internal']);
  assert.deepEqual(kinds('crates/kirin_measure/src/reference_visual.rs'), []);
});

test('the private names match whole Latin words and CJK as written', () => {
  const names = namePatterns(['# comment', 'Alexa', '山田', '']);
  assert.equal(names.length, 2);
  assert.deepEqual(kinds('Alexa said so', names), ['private name']);
  assert.deepEqual(kinds('Alexander', names), []);
  assert.deepEqual(kinds('山田さんの指示', names), ['private name']);
});

test('only added lines are read, with their file and new line number', () => {
  const diff = [
    'diff --git a/README.md b/README.md',
    '--- a/README.md',
    '+++ b/README.md',
    '@@ -10,2 +10,3 @@',
    ' kept line',
    '-removed (owner, 2026-10-01)',
    '+added (owner, 2026-10-05)',
    '+another line',
    'diff --git a/gone.txt b/gone.txt',
    '--- a/gone.txt',
    '+++ /dev/null',
    '@@ -1 +0,0 @@',
    '-by the owner on 2026-10-04',
  ].join('\n');
  const lines = addedLines(diff);
  assert.deepEqual(lines.map((line) => `${line.file}:${line.line}`), ['README.md:11', 'README.md:12']);
  assert.deepEqual(lines.flatMap((line) => kinds(line.text)), ['attribution']);
});

test('a line moved unchanged to another file is not new text; an edited or extra copy is', () => {
  const diff = [
    'diff --git a/old.test.mjs b/old.test.mjs',
    '--- a/old.test.mjs',
    '+++ b/old.test.mjs',
    '@@ -5,2 +4,0 @@',
    "-  url: 'https://example.com/W-1234',",
    '-  plain: true,',
    'diff --git a/new.test.mjs b/new.test.mjs',
    '--- /dev/null',
    '+++ b/new.test.mjs',
    '@@ -0,0 +1,4 @@',
    "+  url: 'https://example.com/W-1234',",
    "+  url: 'https://example.com/W-1234',",
    "+  edited: 'https://example.com/W-1235',",
    '+  plain: true,',
  ].join('\n');
  const lines = newLines(diff);
  assert.deepEqual(lines.map((line) => `${line.file}:${line.line}`), ['new.test.mjs:2', 'new.test.mjs:3']);
  assert.deepEqual(lines.flatMap((line) => kinds(line.text)), ['work item', 'work item']);
});

test('a planted record in a commit message fails, a clean message passes', () => {
  assert.deepEqual(kinds('[B-9999] fix\n\n- closes F-H05 (owner, 2026-10-06)'), ['attribution', 'plan number']);
  assert.deepEqual(kinds('[B-9999] Reference：差の向きを一か所で決める\n\nCo-Authored-By: Claude <noreply@anthropic.com>'), []);
});


test('retained upstream notices require exact reviewed bytes; edited or unregistered files are scanned', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'public-upstream-fixture-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const relative = 'THIRD_PARTY_LICENSES/cargo/fixture/README.md';
  fs.mkdirSync(path.dirname(path.join(root, relative)), { recursive: true });
  const bytes = Buffer.from('X1 is an upstream mathematical variable.');
  fs.writeFileSync(path.join(root, relative), bytes);
  fs.writeFileSync(path.join(root, 'THIRD_PARTY_LICENSES/cargo/provenance.json'), JSON.stringify({
    schema: 'hypha-retained-dependency-license-text-v1', files: [{ path: relative, bytes: bytes.length,
      sha256: crypto.createHash('sha256').update(bytes).digest('hex') }],
  }));
  assert.equal(isRetainedUpstreamNotice(root, relative), true);
  fs.appendFileSync(path.join(root, relative), ' internal record');
  assert.equal(isRetainedUpstreamNotice(root, relative), false);
  assert.equal(isRetainedUpstreamNotice(root, 'THIRD_PARTY_LICENSES/cargo/unregistered.md'), false);
  assert.equal(isRetainedUpstreamNotice(root, '../secret'), false);
});
