# Reference Capture A — implementation and validation

Date: 2026-09-14 (JST)
Source: B-875, branch `codex/reference-abc-delivery`, extending B-874.
Status: local implementation and regression complete, with real-host/Windows completion criteria below still open. No public package or DAW installation in this change.
Plan: [whole-song Capture A](reference_a_full_capture_plan_20260914.md).

## Product behavior

- CAPTURE A arms original POST input without B, a Work connection, or INSPECT. Capture itself does not require an OS library/license; B/C audition keeps its existing OS access checks.
- The next accepted playing callback starts one continuous pass. DAW stop (when reported by the host) or FINISH closes its accepted sample range. A silent passage does not end it. Callback disappearance closes as partial; it is not guessed to mean a normal stop.
- Capture and LIVE/CAPTURED are available at all five sizes. A small FINISH A control remains accessible on other domains. Blind retains its existing 300% rule and hides captured identity, values, and accessibility content.
- Original A is never replayed from a frozen recording. Raw peak/RMS, LUFS-S endpoints, TP/RMS Crest, range LUFS-I/maximum TP, identity, time, and input fingerprints are retained. No whole-song PCM is stored.
- A seek, clock/format/input failure, queue overflow, or two-hour limit closes a partial pass. A failed retry or CANCEL preserves a prior successful capture. Exact accepted frames and actual endpoints survive coarsening.
- Saved Capture is historical. Revisited fingerprint changes are marked; unvisited input is not certified. A newly created processor can revisit restored data, but cannot inherit old Work qualification: matching input plus a new verified map is required. B uses verified same-Work evidence plus the existing verified host/source map and audible sample-rate converter. Missing/changed B preserves A-only display.
- Ordinary A stays at 0 dB. Existing paired-active-block fixed B gain and peak/headroom policy are unchanged; global LUFS-I is not substituted as an automatic matching policy.

## Boundaries and implementation

`ReferenceACaptureSession` owns a bounded A-only queue and worker. Small blocks use the queue efficiently: 480 slots × at most 256 frames rather than one oversized reservation per small host callback. Its PCM/header allocation and the reduced ordinary Visual queue together fit within 2 MiB. Workers do not signal an OS event from the audio callback; explicit UI/control commands wake the idle capture worker. Idle sessions use a slower wait.

A separate `ReferenceACaptureProjection` worker reads B; B file reads never sit on the A capture worker. Immutable summaries cross to the UI at no more than 10 Hz. Drawing uses shared amplitude/time scales, actual bin endpoints, and peak/energy reduction. Small views omit readouts that would overlap the waveform.

Capture has a distinct kernel admission intent. It can transfer/share an existing ordinary-audition Analysis lease without activating PRE-delta suspension. The same process/project barriers exclude both Blind systems. Version Blind keeps its barrier through the normal-return receipt, not just the END click. Capture completion publishes its snapshot before an asynchronous host dirty-state notification; its state survives editor absence.

The self-contained state includes a schema marker, SHA-256 checksum, canonical bounded encoding, and strict dimensions/range checks. Decode runs on the worker. Unknown, truncated, oversized, or corrupt summaries do not replace A output or invalidate unrelated B/C choices. PCM fingerprints support revisits; they are not used to advertise sample-accurate alignment independently of the existing acoustic/host proof.

TP finalization covers the EBU interpolator's finite zero-extended tail while retaining the pre-flush LUFS-I/S and original accepted-frame/RMS counts. The final full bin stays open until another input frame arrives, so stopping exactly on a bin boundary includes that tail in its Crest as well as the range maximum. The implementation uses the installed official `ebur128 0.1.10` source (`true_peak.rs`, `interp.rs`, `ebur128.rs`) and JUCE's `WaitableEvent`/`AsyncUpdater` contracts. Ordinary metering remains separate.

## Targeted evidence

All paths below are local ignored evidence under `target/`.

| Check | Evidence/result |
| --- | --- |
| Core/FFI admission | `capture-a-gate-test.log`, `capture-a-ffi-test.log`: two Capture owners, third rejected, B transfer/return, both Blind exclusions, unchanged PRE delta and non-audition state |
| Meter accuracy | `capture-a-numerical-test.log`: 44.1/48/96/192 kHz × mono/stereo × 64/128/512/1024 frames; range error 0, independent peak/RMS calculation, EBU integrated/short-term comparison, one-frame TP tail |
| Capture lifecycle | `capture-a-tail-final.log`: arm while stopped, stop, retry, seek, cancellation, denied start, corruption, restored/copied receiver revalidation, changed revisits, immutable A, RT allocation count 0 |
| B projection | Same log: identical A/B sample windows and short-term endpoints, gain changes, restored identity gate, missing B preserving A |
| Two-hour accelerated input | 8 kHz mono, 57,600,000 accepted frames, 1,126 coarsened bins, 120,338 encoded bytes. Limit closes PARTIAL. Final input-copy callback p95 0.00428 ms / p99 0.02109 ms in this fixture |
| Error paths and exact-bin tail | Same final log: missing clock, offline/bypass, clock/rate/channel change, nonfinite input, queue overflow; exact-bin final overshoot retained in both final bin TP and range maximum |
| Common Reference UI | `capture-a-ui-test.log`, `capture-a-ui-final/`: all five sizes, A-only overview, LIVE/CAPTURED separate from audio, FINISH, Blind concealment. Comparison rebuild p95 1.560 ms in the recorded run |
| Shipping processor | `capture-a-product-final.log`, CTest `LastTest.log`: unconnected stereo POST, editor-free 192,000-frame capture, bit-identical original A, zero reported latency, host dirty notification and actual plugin state save/reopen PASS; existing 40 editor surface cases PASS |

The accelerated 8 kHz test is a storage/coarsening/queue-boundary test, not a 30-minute real-host CPU or AAE result. The sample-rate/block-size matrix above measures the Rust meter, not every DAW's scheduling behavior.

## Final regression

One consolidated run: `target/capture-a-final-validation/results.json` and per-command logs. Rust workspace: 1,908 passed / 0 failed / 38 ignored across 34 test-binary/doc-test groups. Owned clippy with warnings denied, formatting, line budget and typography passed. Ignored parity inventory was 20 and pairing inventory was 6; all 26 passed.

All four Reference native targets passed. The processor/editor target initially failed because the new test host negotiated mono but sent a stereo buffer. Diagnostics identified that mismatch; after the fixture explicitly negotiated stereo and set its rate/block details per JUCE's host contract, this target passed, including Capture save/reopen and all 40 existing surface cases. The initial failure remains in the consolidated log; `capture-a-product-final.log` records the correction.

Final review also corrected restored-receiver revisit admission and reproduced/fixed a final-bin TP-tail omission. Only the affected native Capture tests and static build were repeated. `capture-a-tail-before.log` records the numerical failure and `capture-a-tail-final.log` records the passing boundary/error/projection/revisit and two-hour checks. Successful Rust and broad Reference suites were not repeated. Source budget and whitespace were checked after the final edits.

## Still required for whole-feature completion

- Real Studio One/Pro Tools operation on this implementation: 30-minute continuous capture, A/B/C switching, 0/nonzero PDC, transport stop behavior, hidden/editor reopen, actual CPU/AAE observations.
- Windows execution of Capture/Blind shared-lock transitions, plugin-state round trip, native UI, and real-host scheduling. No Windows CI result from an older commit is evidence for this implementation.
- Review the captured same-Work/revisit interaction using real edited sessions and different intro/tail lengths, especially restored or copied plugin state.

These are open completion criteria from the approved plan, not a newly deferred phase. B-873's remaining Reference alignment/PDC evidence remains open. No fresh public release, notarization, installation, or Windows installer is claimed by the local test builds.
