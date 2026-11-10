import childProcess from 'node:child_process';

// Host rows that re-run only when the release changes timing, sample-rate or pairing code since
// the previous public release. Every other host row runs for every release.
export const CONDITIONAL_HOST_TESTS = ['pair/combinations', 'high-rate-192k'];

const CODE = ['juce_shell/src', 'crates/kirin_hypha_ffi/src', 'crates/kirin_measure/src'];
// Host wrappers come from the pinned JUCE tree and its patch stack (clock, delay compensation);
// any change there, or to how the shell is built, re-runs the timing rows.
const ALWAYS = ['juce_shell/JUCE', 'juce_shell/patches', 'juce_shell/CMakeLists.txt', 'juce_shell/cmake',
  'scripts/apply_juce_patches.sh', 'scripts/verify_juce_patch_state.sh', 'scripts/build_hypha.mjs',
  'scripts/build_juce_universal.sh', 'scripts/build_aax_universal.sh', 'scripts/build_aax_windows.ps1'];
const TIMING_PATH = /(clock|sample_?rate|pair|timing|pdc|latency|transport|loop|resampl|align|correspond)/i;
// A changed line naming the clock, rate, delay or pairing also counts, whatever the file is called.
const TIMING_TEXT = '(sample_?rate|sampleRate|SampleRate|[Cc]lock|[Ll]atency|pdc|PDC|[Pp]airing|[Tt]ransport)';

// Decided by the tool at --init and frozen with the acceptance criteria. Any doubt runs the rows.
export function timingScope(root, commit, git = args => childProcess.execFileSync('git', args, { cwd: root, encoding: 'utf8' })) {
  try {
    if (!/^[0-9a-f]{40}$/.test(commit || '')) throw new Error('candidate commit required');
    const previous = git(['describe', '--tags', '--abbrev=0', '--match', 'v[0-9]*.[0-9]*.[0-9]*', `${commit}^`]).trim();
    if (!/^v\d+\.\d+\.\d+$/.test(previous)) throw new Error('no previous public release tag');
    const range = `${previous}..${commit}`;
    const named = git(['diff', '--name-only', range, '--', ...CODE]).split('\n').filter(path => TIMING_PATH.test(path));
    const edited = git(['diff', '--name-only', `-G${TIMING_TEXT}`, range, '--', ...CODE]).split('\n');
    const wrappers = git(['diff', '--name-only', range, '--', ...ALWAYS]).split('\n');
    const paths = [...new Set([...named, ...edited, ...wrappers].filter(Boolean))].sort();
    return { previous, timingChanged: paths.length > 0, paths: paths.slice(0, 40) };
  } catch (error) {
    return { previous: null, timingChanged: true, reason: String(error.message).split('\n')[0] };
  }
}

export const requiredHostTestsFor = (fixed, scope) => [...fixed, ...(scope.timingChanged ? CONDITIONAL_HOST_TESTS : [])];
