import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { check, directCalls, readKept } from './check_unavailable_reasons.mjs';

function withSources(files, run) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hypha-unavailable-'));
  try {
    for (const [name, text] of Object.entries(files)) {
      const file = path.join(root, name);
      fs.mkdirSync(path.dirname(file), { recursive: true });
      fs.writeFileSync(file, text);
    }
    run(root, Object.keys(files));
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

test('finds direct calls and skips comments', () => {
  assert.deepEqual(directCalls('a.setEnabled (x);\n// b.setEnabled (y);\nc->setEnabled(z);').map((c) => c.line), [1, 3]);
  assert.equal(directCalls('unavailable::set (a, x, "why");').length, 0);
});

test('a new direct call fails until it takes a reason', () => {
  withSources({ 'juce_shell/src/View.cpp': 'button.setEnabled (ready);\n' }, (root, files) => {
    const failures = check(root, files, new Map());
    assert.equal(failures.length, 1);
    assert.match(failures[0], /without a reason; use hypha::unavailable::set/);
  });
  withSources({ 'juce_shell/src/View.cpp': 'unavailable::set (button, ready, "Play A first");\n' }, (root, files) => {
    assert.deepEqual(check(root, files, new Map()), []);
  });
});

test('kept counts only go down and stale rows are named', () => {
  const kept = readKept('# header\njuce_shell/src/View.cpp\t1\tnot a control: animation frames\n');
  withSources({ 'juce_shell/src/View.cpp': 'frames.setEnabled (a);\nframes.setEnabled (b);\n' }, (root, files) => {
    assert.match(check(root, files, kept)[0], /2 direct setEnabled calls, 1 kept/);
  });
  withSources({ 'juce_shell/src/Other.cpp': '' }, (root, files) => {
    assert.match(check(root, files, kept)[0], /kept but has no direct setEnabled call/);
  });
  assert.throws(() => readKept('juce_shell/src/View.cpp\t1\t \n'), /needs file, count and reason/);
});
