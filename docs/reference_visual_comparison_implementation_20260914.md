# Reference A/B visual comparison — implementation and evidence

Scope: the approved [visual comparison plan](reference_visual_comparison_plan_20260914.md).
This is a development candidate; these changes do not publish or install a release.
Kirin OS remains W-3080 `31fd330c`; its existing measurement 2.0 data is reused without a schema change.

## Product behavior

- A remains the DAW input; B is the verified same-song Version; C remains its independent Check.
- Shared-time stacked A/B waveform, sample peak outside and channel-energy RMS inside.
- B overview is available independently of A coverage. Exact paired readouts require the verified position map.
- Source bins are `[start,end)`. Final bins use actual frame counts. Display reduction uses peak maxima and frame-weighted energy, never dB averages.
- LOUDNESS is LUFS-S at a complete 3-second window endpoint. CREST is true peak / RMS over the same A/B bin. PSR is not substituted.
- The detail chart uses one common vertical scale for both sides. A/B switching preserves the selected display region.
- Click/drag and arrow keys change the view; Shift+arrow resizes it; FOLLOW/Home resumes following. None of these operations seeks, loops or switches DAW output.
- Normal A/B/C stays available at all five sizes. C Preset controls appear in C; compact B uses a location overview and point difference.
- Source-qualified region, FOLLOW and metric preference persist in optional `ReferenceChoices/View` XML. Old settings remain readable. A observation validity is never restored.
- When changing Versions in the same Work, two verified maps carry the selected DAW interval into the new source, including changed head padding. Pending loading does not erase the previous view choice.
- All Version Blind phases conceal waveforms, controls and accessibility identity. PRE/POST Blind retains its existing separate path and exclusion.

## Data and execution boundaries

`ReferenceVisualObservation` copies original A before Reference output replacement into a preallocated SPSC queue.
Its 30 slots, including headers, stay below 2 MiB and are heap allocated before callbacks, including on Windows.
Full queues drop display observations. The audio callback allocates nothing, takes no locks and performs no file I/O.

The worker verifies source revision, reads bounded B chunks and uses the same absolute-phase converter as audible B pages.
Conversion and A/B measurement run inside a non-RT admission fence. Blocking file reads remain outside that fence,
so A-return ownership release does not wait for the source reader. Source-frame boundaries use integer rational ceiling,
including fractional-rate final frames; invalid/extreme host positions are rejected without signed overflow.

No whole-song A PCM is stored. Coverage is bounded by the existing overview's maximum 2048 bins. Seek, dropped blocks,
hidden view, admission changes and map changes restart continuous windows. Earlier passes remain dim history, never current readouts.
The displayed B gain is the installed audition gain while B is selected; normal A always remains at 0 dB.

The observer participates in the existing two Analysis slots. It releases its own slot before audition acquires one,
and can use the admitted audition slot while active. It stops when the view is hidden; summary publication is capped at 10 Hz.
Cached waveform drawing is reduced to screen columns; cursor and detail values do not rebuild source waveform geometry.
The 2 MiB queue and 2 MiB summary/drawing budgets do not include EBU128's bounded 3-second DSP histories.
Those histories alone are approximately 4.4 MiB per A/B observer at 48 kHz stereo and 17.6 MiB at 192 kHz stereo.

`ReferenceDeferredControl` shares one 20 ms ownership-retirement clock per process. Normal A-return/fail-close release
is independent of the runtime's source decoding and journal worker. Blind session data remains retired by the journal worker
after it has observed the completion; early ownership release requires confirmed normal A.

## Validation record

The complete local pass was run once, via `target/reference-visual-full-validation.py`.
Logs and original failures are retained under `target/reference-visual-validation/`; successful Rust suites were not repeated.

- Workspace tests: pass. Clippy workspace/all-targets with warnings denied: pass.
- FFI ignored parity: 20 listed / 20 passed. Ignored pairing_candidates: 6 listed / 6 passed.
- Initial native pass: correlation, runtime and audible pages passed. UI found an inconsistent missing-Crest heading and excessive waveform paint time; both were addressed in the display layer.
- Focused numeric evidence before display correction: same PCM A/B peak/RMS/Crest and LUFS-S agree; non-integer 48k to 44.1k display PCM equals audible page PCM bit for bit.
- Final 48 kHz stereo display measurement, including test input copy: 0.762% of one core for a 4-second fixture. This is an isolated native measurement, not a DAW CPU claim.
- Focused regressions cover queue saturation, absent coverage, seek history, three-second warm-up, two-slot refusal and handoff, stalled-work release, source revision changes, negative preroll and fractional-rate boundaries.
- UI fixtures cover all five sizes, display/audio separation, source-qualified restoration and Blind concealment.

Final focused correction results:

- `reference-visual-runtime-final.log`: `--abc-only` passed. The complete controller observed 75 / 80 bins of the generated eight-second song; calibration/loading gaps remain missing. Known content mapping error was 0 samples and fixed gain was -6.021 dB.
- `reference-visual-ui-final.log`: the formerly failed full Reference UI target passed, including the missing-Crest heading regression.
- `reference-visual-editor-final.log`: all 40 shared-editor surface cases passed. The final display-only adjustment was then checked with the focused UI target, without repeating successful processor/Rust suites.
- `reference-visual-ui-approved.log`: final focused UI passed all five sizes, Blind concealment, restoration, audio/view separation and a same-Work Version change with two seconds of head-padding offset. Fresh 300/900 render images were visually inspected.
- Source line budget and whitespace checks passed. All new owned source remains at or below 500 lines.

900x600 presentation, 2,000-bin synthetic UI fixture, native x86_64 Release build on Intel Core i9-10910 @ 3.60 GHz; quiet local run with five warm-up paints excluded:

| Drawing scope | p95 | p99 |
| --- | ---: | ---: |
| Entire Reference panel | 2.753 ms | 3.122 ms |
| Comparison, cached waveform | 1.000 ms | 1.092 ms |
| Comparison, waveform cache rebuilt | 2.523 ms | 3.170 ms |

The final comparison meets the provisional 4 ms additional-paint target in this native fixture.
The earlier 136.6 ms p95 full-panel result belongs to the rejected rectangle-path implementation.
The remaining gradient background was also replaced with the existing observation-well material; no full-panel image cache was added.
These measurements do not establish live DAW callback time, PDC accuracy, display scaling cost or long-run dropout behavior.

## Remaining evidence boundaries

New exact-commit Studio One/Pro Tools PDC capture, long-run AAE/dropout validation, the full rate/buffer matrix and
signed Windows host/installer validation must not inherit the B-870/B-871 evidence. The previous screenshot's AAE-6101
cause is not established by these native tests. Public release readiness is not implied by this implementation record.
