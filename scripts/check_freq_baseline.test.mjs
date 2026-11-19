import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import assert from 'node:assert/strict';
import { baseline, check } from './check_freq_baseline.mjs';
const source = 'a'.repeat(40);
function fixture(fn) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'freq-baseline-'));
  try {
    const actual = path.join(tmp, 'render'), binary = path.join(tmp, 'binary');
    fs.cpSync(baseline, actual, { recursive: true }); fs.writeFileSync(binary, 'test executable');
    fn(actual, binary, tmp);
  } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
}
function edit(actual, fn) {
  const file = path.join(actual, 'render.json'), data = JSON.parse(fs.readFileSync(file));
  fn(data); fs.writeFileSync(file, JSON.stringify(data));
}
test('exact approved baseline accepts all six cases', () => fixture((a, b) => assert.equal(check(a, b, source).cases, 6)));
test('baseline directory cannot masquerade as new render', () => fixture((a, b) => assert.throws(() => check(baseline, b, source))));
for (const [name, mutation] of [
  ['missing', d => d.cases.pop()], ['duplicate', d => d.cases.push(d.cases[0])],
  ['backend', d => { d.backend = 'other'; }], ['os', d => { d.os = 'other'; }],
  ['dimensions', d => { d.cases[0].width++; }],
  ['text', d => { d.cases[0].labels[0].text += 'x'; }],
  ['bounds', d => { d.cases[0].labels[0].bounds = '0 0 1 1'; }],
  ['rgba', d => { d.cases[0].rgba_sha256 = '0'.repeat(64); }],
]) test(`${name} mutation fails`, () => fixture((a, b) => { edit(a, mutation); assert.throws(() => check(a, b, source)); }));
test('even a single changed PNG byte fails without tolerance', () => fixture((a, b) => {
  const file = path.join(a, '300-en.png'), bytes = fs.readFileSync(file); bytes[bytes.length - 1] ^= 1;
  fs.writeFileSync(file, bytes); assert.throws(() => check(a, b, source));
}));
test('missing image fails', () => fixture((a, b) => {
  fs.unlinkSync(path.join(a, '900-ja.png')); assert.throws(() => check(a, b, source));
}));
test('short source identity fails', () => fixture((a, b) => assert.throws(() => check(a, b, 'abcdef'))));
