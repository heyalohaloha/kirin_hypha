import assert from 'node:assert/strict';
import test from 'node:test';
import { CONDITIONAL_HOST_TESTS, requiredHostTestsFor, timingScope } from './release_scope.mjs';

const commit = 'a'.repeat(40);
const git = changes => args => {
  if (args[0] === 'describe') return 'v1.2.2\n';
  const scope = args.slice(args.indexOf('--') + 1);
  const pick = args.some(arg => arg.startsWith('-G')) ? changes.edited || [] : changes.paths || [];
  return pick.filter(path => scope.some(prefix => path === prefix || path.startsWith(`${prefix}/`))).join('\n');
};

test('timing rows join when timing code, the wrapper patches or the JUCE pin changed', () => {
  for (const paths of [['juce_shell/src/PluginProcessorContentTiming.cpp'], ['crates/kirin_hypha_ffi/src/pairing.rs'],
    ['juce_shell/patches/0011-aax-engine-clock.patch'], ['juce_shell/JUCE'], ['juce_shell/CMakeLists.txt'],
    ['scripts/apply_juce_patches.sh'], ['scripts/build_aax_universal.sh'], ['scripts/build_aax_windows.ps1']]) {
    const scope = timingScope('/repo', commit, git({ paths }));
    assert.equal(scope.timingChanged, true, paths[0]); assert.equal(scope.previous, 'v1.2.2');
  }
  // A changed line naming the clock counts whatever the file is called.
  assert.equal(timingScope('/repo', commit, git({ paths: ['juce_shell/src/PluginEditor.cpp'], edited: ['juce_shell/src/PluginEditor.cpp'] })).timingChanged, true);
});

test('documentation, tooling and unrelated code leave the timing rows out', () => {
  const scope = timingScope('/repo', commit, git({ paths: ['README.md', 'scripts/ci_scope.mjs', 'juce_shell/src/PluginEditorColours.cpp'] }));
  assert.deepEqual(scope, { previous: 'v1.2.2', timingChanged: false, paths: [] });
  assert.deepEqual(requiredHostTestsFor(['a'], scope), ['a']);
  assert.deepEqual(requiredHostTestsFor(['a'], { timingChanged: true }), ['a', ...CONDITIONAL_HOST_TESTS]);
});

test('any doubt runs the timing rows', () => {
  for (const broken of [() => { throw new Error('not a git repository'); }, () => 'main\n']) {
    assert.equal(timingScope('/repo', commit, broken).timingChanged, true);
  }
  assert.equal(timingScope('/repo', 'HEAD', git({})).timingChanged, true);
});
