# Raw host clock diagnostic (non-shipping)

This standalone project tests the **patched JUCE wrapper's host-clock facts**, not the complete
Hypha product, its content-clock admission, or a release candidate. It is intentionally absent
from the normal product build/install/release targets. A successful probe does not qualify
CHAIN ACTION, TP CROSSING, another format, or another host.

Configure with an explicit, verified `HYPHA_DIAGNOSTIC_JUCE_ROOT`, then build
`HyphaClockDiagnostic_VST3`, `KirinHyphaPdcValidationDelay_VST3`, and `HyphaClockTraceTest`.
macOS also supplies `HyphaClockDiagnostic_AU` for mixed-wrapper clock observations.
Run CTest in the same configuration. Do not use Debug timings as production CPU evidence.

`node make-fixture.mjs /absolute/path/clock-fixture.wav` creates the fixed-seed, 24-second,
48 kHz stereo float fixture exclusively (an existing destination is refused).
`node analyze-trace.mjs fixture.wav pre.csv post.csv` reads immutable exports and emits JSON
with their hashes, raw validity flags, clock discontinuities, and exact first/last sample-bit
matches. Missing or ambiguous matches are counted separately, not interpreted as zeros or
used to infer an offset. A run means continuous observed `playing`; a seek may occur within it.
Boundary matching is a diagnostic oracle for this known fixture, not a product algorithm.

Use a dedicated disposable Song with a deterministic, low-level, stereo WAV, no instruments or
external inserts, and no changes to the user's device/routing. Insert two Clock Diagnostic
instances, first adjacent, then with the separately identified 4096-sample delay between them.
Record placement, instance IDs, binary/source hashes, sample rate, buffer setting, and GUI state.
Start each probe's one-shot observation. Compare playback, stop/resume, seeks and short loops.
Export both prefixes. Reinstantiate to start a new bounded observation; Start never resets or
overwrites existing observations. After testing, remove only these separately named diagnostic
bundles from the scan path, preserving the signed product and original Song.

Each instance holds at most 65,536 immutable rows in a preallocated array. Only the audio
producer writes; an acquire/release publication count makes exported prefixes immutable while
new callbacks append. Full means no further rows, not wraparound. No audio-path allocation,
lock, file I/O, or output modification is performed. Export runs on the message thread and is
an explicit diagnostic operation; no performance claim includes export time.

Files are explicitly exported to `~/KirinValidation/HyphaClockDiagnostic/<instance>-<count>.csv`,
outside the possibly cloud-redirected Documents directory.
They contain raw clocks, actual callback frame/channel counts, and the exact float bit patterns
of the first/last input samples on each channel. Use only the approved test WAV, not private
working audio. Sample boundary bits are observations, not a content-alignment algorithm.

`flags` bits:

| Bit | Meaning |
| --- | --- |
| 0 | Host playing |
| 1 | Host looping |
| 2 | Project position present |
| 3 | Auxiliary clock present |
| 4 / 5 | Input / output presentation latency present |
| 6 | Input boundary samples present |
| 7 | Non-realtime callback |
| 8 | Host callback time in nanoseconds present |
| 9 | PPQ position present |
| 10 | Tempo present |
| 11 | Loop points present |
| 12 | AAX engine TOD sample clock present (diagnostic-only extension) |
| 13 | AAX algorithm-context `AddClock` present (separate from TOD/native) |
| 14 | Full-frame stereo identity audit present (diagnostic-only) |

Auxiliary sources: 1 = VST3 continuous, 2 = AU render, 3 = AAX native. This standalone build
exercises VST3 and, on macOS, AU. Missing flags are missing evidence, not zero. A reported latency
of zero does not prove that the host implements meaningful per-node latency reporting.
Use known WAV boundary samples to independently test the clock/content mapping with and without
the 4096-sample delay. Equal project positions alone must not join different loop occurrences.

Extended exports append `host_ns,ppq,bpm,loop_start,loop_end`. The analyzer accepts both this
schema and the original schema. Callback time is a raw host observation, not permission to
align audio by wall-clock arrival; it may be missing or have different wrapper semantics.

## Unique emission signal (separate, opt-in non-shipping target)

`HyphaLoopIdentitySource_VST3` (and `_AU` on macOS) builds **Kirin Loop Identity Source - Not Product**, with a
different plugin ID/bundle ID from the read-only diagnostic. It replaces input with silence
until explicitly armed, then generates a stereo per-frame identity at at most -42.14 dBFS peak
only during realtime playback. Use it only in a disposable Song with explicit operator approval.
It is never a normal Hypha build/install target and must not be inserted into a working mix.

Unlike the WAV oracle, its 32-bit emission identity never repeats at a project loop or seek.
It does not reset after stop/prepare; exhaustion or a full trace produces silence, not wrapping
identities. Stop Signal disables it for the instance. Mono and offline callbacks stay silent.
Start the downstream read-only probe first, then arm this source. Keep all intervening plugins
limited to the known identity-preserving validation delays. Export both immutable prefixes.

`node analyze-identity.mjs source.csv destination.csv` decodes exact stereo identities and joins
them to emitted source spans without using clocks, project position, arrival order, or Consumer K
to choose a match. It reports missing/unmatched boundaries and clock offsets separately.
Old traces are explicitly boundary-only. New exports append the first/last decoded identity,
identity-frame count, initial silent-prefix length and error count. `IdentityAudit` reverses
the encoding on EVERY stereo frame and requires consecutive unique emissions. Correct boundaries
cannot hide an interior wrong-lap sample, silence, NaN or reorder. All matched source spans must
be gap-free too. This exact reversible check is not a hash or waveform-correlation heuristic.
The analyzer reports full-frame verified blocks/frames separately; it never upgrades old traces.
The audit and its fixed trace storage remain outside all shipping product targets.
`HyphaIdentitySignalTest` and `node --test analyze-identity.test.mjs` check identity reversibility,
peak bound, lifecycle, a whole-lap wrong-clock control, missing evidence, and corrupt spans.

## Optional AAX TOD observation

The product's pinned JUCE patch stack is unchanged. Copy its verified source to a separate,
new diagnostic directory outside any Git worktree, then apply `aax-tod-diagnostic.patch` there with
`git apply --verbose --unidiff-zero --ignore-whitespace`. A copy inside an ignored worktree
directory instead requires running from the repository root with `--directory=<copy-path>`;
running from that nested copy can silently skip every patch. Verify both patched files contain
`KIRIN_CLOCK_DIAGNOSTIC_TOD` before configuring. Never apply this patch to the product submodule
or pass this modified diagnostic copy to a release build. It adds a distinct raw TOD field;
it does not replace AAX native location, convert it to host nanoseconds, or supply product K.

Configure this project with that copy as `HYPHA_DIAGNOSTIC_JUCE_ROOT`, an external
`HYPHA_DIAGNOSTIC_AAX_SDK`, and `HYPHA_DIAGNOSTIC_AAX_LICENSE_CONFIRMED=ON` only after license
confirmation. The current AAX build also requires the separate AddClock patch below. Build
`_AAX` targets for the read-only observer, identity source and validation
delay. AudioSuite is disabled. These separately identified unsigned targets are for a Developer
host only; they are not PRE/POST distribution candidates, signed installer inputs, or product
acceptance. The SDK is never copied into the repository or source transfer.

Exports append `tod_samples`; a missing validity bit is not a zero clock. The identity analyzer
continues to accept earlier 20-column traces and reports TOD offsets/steps separately. A constant
TOD difference on ID-matched audio is an observation, not proof that the host's engine TOD alone
identifies compensated content across arbitrary nodes, sleep, stop, or loop occurrences.
`AAX_IController::GetTODLocation` in the external SDK is the source of these values.

The 2026-10-01 Windows Developer-host observation found varying TOD differences even on
ID-matched adjacent input, and a non-constant difference after the 4096-sample delay. Do not
reuse this probe as a product clock source. The measured conditions and exclusions are in
`docs/planning/hypha_daw_loop_match_design_20260930.md` (repository root).

## AAX algorithm-context AddClock probe

Apply `aax-addclock-diagnostic.patch` AFTER the TOD patch to the same isolated copy, using the
same `git apply --unidiff-zero --ignore-whitespace` / repository-root directory rule above.
Check both the PositionInfo getter and the actual `desc.AddClock` subscription in the wrapper;
CMake rejects a header-only extension. Both macros are enabled for these diagnostic targets only.
The callback copies the raw `AAX_CTimestamp` supplied by the host into a separate atomic field
for its active processing scope, then clears validity. No query of TOD/native fills this field.
Missing pointers remain unavailable. The patch never enters the approved shipping JUCE stack.

New exports add `add_clock_samples` plus the five full-frame audit fields (27 columns total).
The identity analyzer keeps separate AddClock/TOD/native offsets, continuity and within-block
contradictions, including partially silent initial-delay blocks. A running counter is not yet a
compensated-content proof: SDK 2.9.0 also documents PT-282946 (live/non-live timestamp discrepancy).
Test raw sample-quantum progression, initial LOOP, stop/restart, mid-play insertion and delays
smaller/equal/larger than the LOOP. Never infer a universal two-buffer correction or inject K=0.

The 2026-10-01 Windows Developer probe audited every frame at 48 kHz / stereo / 1024-frame
callbacks, with a 131072-sample LOOP. Adjacent initial LOOP + stop/restart matched 1577984 frames
at AddClock offset 0. After the 4096-sample physical delay, 1570816 frames matched at offset
4096; each run's first four non-identity startup blocks were rejected, not counted as success.
A probe inserted during playback matched another 612352 frames at offset 4096. Native locations
folded at the LOOP; input/output presentation latency was absent. These are separately named,
unsigned diagnostic results, not a product or regular-host certificate.

The observed AddClock behavior fits a shared ENGINE counter, not a compensated CONTENT clock.
The model control in `../live_compare/LoopEntryEvidenceContract.h` verifies that equal engine,
folded native and LOOP metadata can correspond to different audio for delay 0 versus one whole
lap. That is a mathematical counterexample, not an unperformed AAX long-delay host test.
AddClock alone therefore does not authorize initial LOOP playback. The same model correctly
acquires K=4096 in a linear interval, so more waiting inside the LOOP is not the missing proof.
The full plan records the qualifications and remaining host/format gates.
