# Frequency bitmap baseline

Six approved initial static renders: 300×200, 600×400 and 900×600, English/Japanese,
scale 1, paired full-frequency LR, live average, fixed synthetic snapshot, no hover.
This is not an actual DAW, animation, all display modes, intermediate-size or platform acceptance test.

`bundle.json` records the product source commit, executable SHA-256, fixture SHA-256,
renderer/OS/JUCE identity and all six image hashes. The executable was built normally
from `07c18f84f3feeedcfe89a5df091966bd09dbf5fc` with `PriorFreqFixture.h` and its
command dispatch. A separately recorded test-only overlay changed the placement of
a literal in `xtask/src/ffi_unwind_guard_tests.rs`; it did not change this executable.
Normal storage guard setup, rendering and cleanup all succeeded.

The original comparison used released source
`b193e43227290964d268bf0e0b037ddf845e8a8f` and the same fixture/backend/dimensions.
The reviewed change included the glow layer reduction and the previously reviewed
instrument-note opaque backing, bounded text slot and text opacity. This is a
one-time acceptance of these exact images, NOT a future pixel tolerance. Prior
images, difference images, metrics and approval remain in the external acceptance
record, not in the repository. This baseline is not evidence for a later integrated build.

## Reproduce and check

1. Build the normal `KirinUiRenderContractTests` target using the repository's native
   test instructions, with the same OS/JUCE/backend. Preserve its source and binary hashes.
2. Use the normal native storage guard before and after the render. If setup fails,
   do not run the executable or weaken the guard. Use an isolated test storage root.
3. Run `KirinUiRenderContractTests --prior-freq NEW_OUTPUT_DIRECTORY`.
   The directory must not exist. No output is written to the baseline directory.
4. Run `node scripts/check_freq_baseline.mjs NEW_OUTPUT_DIRECTORY BINARY FULL_SOURCE_COMMIT`
   from the repository root. Save its output with the native process exit code and guards.

The checker requires all six cases, matching fixture/backend/OS/JUCE, exact text and
bounds, RGBA digest and PNG bytes. It has no masks, exclusions, approval override or
maximum-difference threshold. A backend/encoding change can therefore fail even if
it looks identical; report that separately rather than increasing tolerance.
The supplied output must come from the just-run executable; copied baseline images
are only synthetic checker tests, not fresh rendering evidence.

`node --test scripts/check_freq_baseline.test.mjs` exercises positive and mutation
cases without native execution and is included in the lightweight source contract.
That test does not prove the current product still renders identically. Real native
render/check remains required for a candidate, using the sequence above. Baseline
updates require separate visual review and must preserve new provenance.
