import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import { readFloatWav } from './wav.mjs';
import { inlineJson, selection } from './build_review_pack.mjs';
import { spaceFollowupSelection } from './build_space_followup_pack.mjs';

const context = vm.createContext({});
vm.runInContext(await readFile(new URL('./model.js', import.meta.url), 'utf8'), context);
const model = context.HyphaReviewModel;
const pack = { pack_id: 'test', manifest_sha256: 'a'.repeat(64), protocol: 'test', items: [
  { id: 's', mode: 'space', rate: 44100, frames: 1323000, preview: [441000, 882000], audio_sha256: 'b'.repeat(64), source_start_sample: 1323000 },
  { id: 'a', mode: 'attack', rate: 192000, frames: 5760000, preview: [1920000, 3840000], audio_sha256: 'c'.repeat(64), source_start_sample: 5760000 },
] };
function wav(samples = [.25, -.5, .125, -.125], options = {}) {
  const b = Buffer.alloc(44 + samples.length * 4); b.write('RIFF'); b.writeUInt32LE(b.length - 8, 4); b.write('WAVEfmt ', 8);
  b.writeUInt32LE(16, 16); b.writeUInt16LE(options.tag ?? 3, 20); b.writeUInt16LE(2, 22);
  b.writeUInt32LE(44100, 24); b.writeUInt32LE(44100 * 8, 28); b.writeUInt16LE(8, 32); b.writeUInt16LE(32, 34);
  b.write('data', 36); b.writeUInt32LE(samples.length * 4, 40); samples.forEach((s, i) => b.writeFloatLE(s, 44 + i * 4)); return b;
}
test('pilot selects exactly 3 distinct development items per feature', () => {
  assert.equal(selection.length, 6); assert.equal(new Set(selection.map(String)).size, 6);
  assert.equal(selection.filter(([mode]) => mode === 'space').length, 3);
});
test('SPACE follow-up uses six unused development items without reserved audio', () => {
  const ids = spaceFollowupSelection.map(([mode, number]) => `${mode}_development-${number}`);
  assert.equal(ids.length, 6);
  assert.equal(new Set(ids).size, 6);
  assert(ids.every(id => id.startsWith('space_development-')));
  assert(ids.every(id => !['space_development-01', 'space_development-03',
    'space_development-12'].includes(id)));
});
test('waveform includes both channels without mono cancellation', () => {
  const f = readFloatWav(wav()); assert.equal(f.frames, 2); assert.equal(f.rate, 44100);
  assert.equal(f.peak, .5); assert.equal(f.wave[0], Math.round(-.5 * 32767)); assert.equal(f.wave[1], Math.round(.25 * 32767));
  assert(Math.abs(f.rms - Math.sqrt((.25 ** 2 + .5 ** 2 + 2 * .125 ** 2) / 4)) < 1e-12);
});
test('truncated, unknown format, nonfinite and empty PCM are rejected', () => {
  assert.throws(() => readFloatWav(wav().subarray(0, 50)));
  assert.throws(() => readFloatWav(wav(undefined, { tag: 1 })));
  assert.throws(() => readFloatWav(wav([NaN, 0]))); assert.throws(() => readFloatWav(wav([])));
});
test('embedded metadata cannot terminate the script', () => {
  const input = { title: '</script><script>alert(1)</script>&\u2028' };
  const encoded = inlineJson(input); assert(!encoded.includes('<')); assert.deepEqual(JSON.parse(encoded), input);
});
test('blank and partial answers roundtrip without inventing completion', () => {
  const state = model.fresh(pack); state.answers.s.note = '未完了\n途中';
  const clean = model.validate(JSON.parse(JSON.stringify(state)), pack);
  assert.equal(clean.answers.s.complete, false); assert.equal(clean.answers.s.note, state.answers.s.note);
});
test('finish requires listening, explicit choice, confidence and relevant marks', () => {
  const a = model.fresh(pack).answers.s;
  assert(model.finishError(a, pack.items[0])); a.decision = 'present'; a.confidence = '4';
  assert(model.finishError(a, pack.items[0])); a.played_seconds = 2;
  assert(model.finishError(a, pack.items[0])); a.marks = [{ start: 441000, end: 450000 }];
  assert.equal(model.finishError(a, pack.items[0]), '');
  a.decision = 'none'; assert(model.finishError(a, pack.items[0]));
});
test('uncertain and unavailable never count as judged evidence', () => {
  const state = model.fresh(pack);
  Object.assign(state.answers.s, { decision: 'unavailable', complete: true });
  Object.assign(state.answers.a, { decision: 'uncertain', played_seconds: 2, complete: true });
  const counts = model.counts(model.validate(state, pack));
  assert.equal(counts.complete, 2); assert.equal(counts.judged, 0); assert.equal(counts.uncertain, 1); assert.equal(counts.unavailable, 1);
});
test('wrong pack, definition or audio identity cannot overwrite answers', () => {
  for (const [field, value] of [['pack_id', 'wrong'], ['manifest_sha256', 'wrong'], ['protocol', 'old'], ['candidate_exposure', true]]) {
    const state = model.fresh(pack); state[field] = value; assert.throws(() => model.validate(state, pack));
  }
});
test('native positions reject fractions, reversed intervals and review-window overflow', () => {
  for (const mark of [{ start: 440999, end: 450000 }, { start: 441000, end: 882000 },
    { start: 441000.5, end: 450000 }, { start: 450000, end: 441000 }, { start: 450000, end: 450000 }]) {
    const state = model.fresh(pack); state.answers.s.marks = [mark]; assert.throws(() => model.validate(state, pack));
  }
  const state = model.fresh(pack); state.answers.a.marks = [{ start: 1920000, end: 1920001 }]; assert.throws(() => model.validate(state, pack));
});
test('malformed imports, false completion and missing answers are rejected', () => {
  for (const mutate of [s => s.answers.s.complete = true, s => s.answers.s.played_seconds = Infinity,
    s => delete s.answers.a, s => s.answers.s.note = [], s => s.volume = 5,
    s => s.answers.s.marks = Array(301).fill({ start: 441000, end: 450000 })]) {
    const state = model.fresh(pack); mutate(state); assert.throws(() => model.validate(state, pack));
  }
});
test('TSV contains actual tabs, BOM and escaped multiline text without formula execution', () => {
  const state = model.fresh(pack); state.reviewer = '=SUM(1,2)'; state.answers.s.note = 'quoted "value"\nnext\tcolumn';
  const out = model.tsv(state, pack); assert(out.startsWith('\uFEFF')); assert(out.includes('\t'));
  assert(out.includes('"\'=SUM(1,2)"')); assert(out.includes('"quoted ""value""\nnext\tcolumn"')); assert(!out.includes('\\t'));
});
