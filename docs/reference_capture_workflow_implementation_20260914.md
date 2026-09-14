# Capture A and workflow implementation evidence

Date: 2026-09-14
Status: In progress. Implements the approved B-879 plan; this is not a release or host-validation claim.
Plan: [Integrated plan](hypha_capture_and_workflow_integrated_plan_20260914.md), [evidence and discovery contract](reference_evidence_and_discovery_contract_20260914.md).

## B-880: Capture ownership, historical evidence and input observations

- Separate serialized ownership from asynchronous decoding and active capture. Pending restore is immediately saveable. Capture/restore generations reject older results; malformed restore retains the last valid document. Explicit empty state clears it.
- Preserve partial capture state and its notice on restore. Keep fit-to-capture separate from manual range selection, including continued growth of one capture ID.
- Store historical position and gain receipts per Capture ID and immutable B file/PCM identity. A current live map, Work name, gain change or reselected B never replaces an old receipt.
- Derive capture-specific receipts from the existing bounded four-second input and independently aligned B probe, using the existing 400 ms / 100 ms paired-block gain analysis. Receipt creation has no audio-output command. Later observations start at capture-index boundaries; four exact complete units and non-repeated evidence are required. No additional whole-song PCM is retained.
- Index original input in fixed 64-byte one-second units: canonical float32 SHA-256, four band energies, RMS and sample peak. Compute only on the worker. Notification distinguishes exact digest, material difference, other raw difference and unknown timing. A live difference never attributes the change to a particular plugin or asserts an entire song is unchanged.
- Carry callback clock/PDC signatures and runtime-only time epochs. Restore needs fresh exact sequence evidence for a current cursor or material-change interpretation. Per-range pass/time observations remain separate from immutable A and historical B receipts. Concealed Reference suspends new checks; previous difference is labelled LAST CHECK.
- Write bounded v2 capture payloads; continue accepting bounded v1 data without inventing missing evidence. Captured B display explicitly shows its fixed dB offset or ORIGINAL LEVELS.

## Focused evidence recorded so far

These are native harness and algorithm results, not DAW-host results.

| Check | Result |
| --- | --- |
| Streaming index tests | 3 pass: canonical zero/tail/nonfinite/format boundaries, processing versus dither, worker timing |
| Processing fixtures | Gain and RMS-normalized EQ/dynamics detected; 16/24-bit dither produces no material notification; polarity-only raw difference is not called exact |
| Index worker, 48 kHz stereo | 100 ms audio: p95 0.283 ms; target at most 1 ms |
| Index worker, 8–768 kHz | Tested rates remain below 10% realtime; 768 kHz stereo p95 3.327 ms per 100 ms audio |
| Capture runtime | Pass: exact frames, immutable original A, zero callback allocations, seek, cancel, missing/changed clock, PDC notification, format change, nonfinite input, queue overflow, restore/save generations and partial restore |
| Historical projection | Pass: live one-second anchor shift and gain change leave historical sample windows and gain unchanged; v1/missing receipts do not inherit Work alignment; missing B retains A |
| End-to-end Capture/B evidence | Pass: B-free capture → later measured Version → exact replay → fixed −6.021 dB receipt, 0-sample position error → persisted receipt → current A edit with historical A/B unchanged |
| Position evidence boundaries | Pass: one-sample and one-second moved observations, wrong rate and repeated four-second sequence cannot create capture identity |
| Two-hour capture | 57,600,000 actual input frames at 8 kHz mono; 1,126 display bins; 734,770 serialized bytes; callback p95 0.004093 ms and p99 0.019051 ms; RT allocations zero |
| Maximum serialization | Pass: 7 rates × mono/stereo, 7,200 units + 2,048 bins + 16 receipts; full XML remains below 1 MiB and round-trips without truncation |
| Reference UI | Pass at all five sizes: growing fit range, preserved manual range, source/view separation, complete Blind concealment. 300/900 renders inspected |
| Source/format checks | Pass: new source ≤500 lines, existing ratchet unchanged; Rust formatting and diff whitespace checks |

Logs and rendered images are local under `target/b880-*`. The JUCE helper build initially used an unset deployment target and failed against the macOS 15 SDK. Setting the existing helper build cache to macOS 12.0 resolved this; vendor source was not changed.

## Remaining work in the same approved task

- Finish G0 RAM/lifetime and cross-feature boundaries. Current results do not establish DAW wall-clock latency or all-wrapper peak resident memory.
- Implement and verify bounded, demand-driven PRE candidate discovery, including completeness, concurrent claims, teardown and module-unload behavior (G0-S).
- Complete H01–H08: labels, exact single-PRE action, large-screen Blind entrance, fixed progress/answer placement, matching audio-confirmed return without extra Close, typed refusal/repair, direct TIME range and primary Reference choices.
- Consolidate the full Rust/FFI ignored/native baseline once the final implementation is ready. Run new focused tests only for changed or unresolved paths.
- Record actual Studio One / Pro Tools and Windows verification separately. No new release build, installation, signing, notarization or public distribution has been performed for these changes.
