import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import assert from 'node:assert/strict';

test('the keys and LISTEN guides explain host-first shortcuts and the button alternative', () => {
  for (const path of ['docs/manual/screen_and_keys.md', 'docs/manual/live_compare.md']) {
    const text = readFileSync(path, 'utf8').replace(/\s+/g, ' ');
    assert.match(text, /Logic Pro/);
    assert.match(text, /(?:DAW|host)[^.]*\b(?:takes|take)\b[^.]*\b(?:letter|character) keys?\b/i);
    assert.match(text, /click[^.]*Hypha window/i);
    assert.match(text, /(?:buttons?|controls?)[^.]*\b(?:instead|same)\b/i);
  }
});
