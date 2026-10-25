import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve, join } from 'node:path';
import { spawnSync } from 'node:child_process';

// Mask comments/literals only to locate the whole method. Compile the original
// bytes, including all control flow: a commented/unreachable call stays inactive.
function method(source, signature) {
  let mask = '', i = 0;
  const blank = value => value.replace(/[^\n\r]/g, ' ');
  while (i < source.length) {
    const start = i;
    if (source.startsWith('//', i)) {
      i = source.indexOf('\n', i); if (i < 0) i = source.length;
    } else if (source.startsWith('/*', i)) {
      const end = source.indexOf('*/', i + 2); assert.ok(end >= 0, 'closed C++ comment'); i = end + 2;
    } else if (source.startsWith('R"', i)) {
      const open = source.indexOf('(', i + 2); assert.ok(open >= 0, 'raw literal delimiter');
      const endMarker = ')' + source.slice(i + 2, open) + '"';
      const end = source.indexOf(endMarker, open + 1); assert.ok(end >= 0, 'closed raw literal');
      i = end + endMarker.length;
    } else if (source[i] === '"' || (source[i] === "'" && !/\d/.test(source[i - 1] || ''))) {
      const quote = source[i++];
      while (i < source.length) {
        if (source[i] === '\\') { i += 2; continue; }
        if (source[i++] === quote) break;
      }
    } else { mask += source[i++]; continue; }
    mask += blank(source.slice(start, i));
  }
  const start = mask.indexOf(signature);
  assert.ok(start >= 0, signature);
  assert.equal(mask.indexOf(signature, start + signature.length), -1, 'one active method: ' + signature);
  const open = mask.indexOf('{', start); assert.ok(open >= 0, signature);
  let depth = 1, end = open + 1;
  while (depth && end < mask.length) {
    if (mask[end] === '{') depth++;
    if (mask[end] === '}') depth--;
    end++;
  }
  assert.equal(depth, 0, signature);
  return source.slice(start, end);
}

test('shipping Watch snapshots reach the display model and respect publication readiness', () => {
  const meter = readFileSync('juce_shell/src/PluginEditorMeter.cpp', 'utf8');
  const view = readFileSync('juce_shell/src/HyphaObservatoryView.cpp', 'utf8');
  const editor = readFileSync('juce_shell/src/PluginEditorObservatory.cpp', 'utf8');
  const directory = mkdtempSync(join(tmpdir(), 'hypha-watch-runtime-'));
  try {
    const source = join(directory, 'watch.cpp'), binary = join(directory, process.platform === 'win32' ? 'watch.exe' : 'watch');
    writeFileSync(source, `
#include "watch_snapshot_runtime.h"
${method(view, 'void View::setWatchDisplay')}
${method(meter, 'void KirinHyphaEditor::refreshWatchSnapshot()')}
${method(editor, 'void KirinHyphaEditor::refreshObservatory()')}
int main() {
  KirinHyphaEditor editor;
  editor.refreshWatchSnapshot(); editor.refreshObservatory();
  assert(!editor.observatoryView.watchDisplayAvailable);
  for (double level : {-12.0, -31.0}) {
    editor.processorRef.available = true;
    editor.processorRef.next.current.lufs_m = level;
    editor.processorRef.next.maximum.true_peak = level + 7;
    editor.refreshWatchSnapshot();
    auto previous = editor.observatoryView.watchDisplay;
    editor.processorRef.preferredTarget = Target::absolute;
    editor.processorRef.levelReady = false;
    editor.refreshObservatory();
    assert(observation_equality::same(previous, editor.observatoryView.watchDisplay));
    editor.processorRef.levelReady = true;
    editor.refreshObservatory();
    assert(editor.observatoryView.watchDisplayAvailable);
    assert(editor.observatoryView.watchDisplay.current.lufs_m == level);
    assert(editor.observatoryView.watchDisplay.maximum.true_peak == level + 7);
    editor.processorRef.available = false;
    editor.processorRef.next = {};
    editor.refreshWatchSnapshot();
    editor.processorRef.preferredTarget = KIRIN_HYPHA_PRE_DISPLAY ? Target::absolute : Target::delta;
    editor.processorRef.levelReady = KIRIN_HYPHA_PRE_DISPLAY;
    editor.refreshObservatory();
    assert(editor.observatoryView.watchDisplay.current.lufs_m == level);
    assert(editor.observatoryView.watchDisplayAvailable);
  }
  assert(editor.observatoryView.paints == 2);
#if ! KIRIN_HYPHA_PRE_DISPLAY
  editor.processorRef.available = true;
  editor.processorRef.next.current.lufs_m = -45.0;
  editor.refreshWatchSnapshot();
  for (bool live : {true, false}) {
    editor.liveBlindOpen = live; editor.localBlindOpen = !live;
    editor.refreshObservatory();
    assert(editor.observatoryView.watchDisplay.current.lufs_m == -31.0);
    assert(editor.observatoryView.paints == 2);
  }
  editor.liveBlindOpen = editor.localBlindOpen = false;
  editor.refreshObservatory();
  assert(editor.observatoryView.watchDisplay.current.lufs_m == -45.0);
  assert(editor.observatoryView.paints == 3);
#endif
}
`);
    const compiler = process.env.CXX || (process.platform === 'win32' ? 'clang++' : 'c++');
    for (const preDisplay of [0, 1]) {
      const build = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
        `-DKIRIN_HYPHA_PRE_DISPLAY=${preDisplay}`, '-DKIRIN_HYPHA_GUIDE_TRANSPORT=0',
        '-I', resolve('scripts/fixtures'), '-I', resolve('juce_shell/src'),
        '-I', resolve('crates/kirin_hypha_ffi/include'), source, '-o', binary], { encoding: 'utf8' });
      assert.equal(build.status, 0, `PRE_DISPLAY=${preDisplay}: ` + (build.error?.message || build.stderr));
      const run = spawnSync(binary, [], { encoding: 'utf8' });
      assert.equal(run.status, 0, `PRE_DISPLAY=${preDisplay}: ` + (run.error?.message || run.stderr));
    }
  } finally { rmSync(directory, { recursive: true, force: true }); }
});
