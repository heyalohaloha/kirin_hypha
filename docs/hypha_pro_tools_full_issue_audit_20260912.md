# Kirin Hypha Pro Tools full issue audit

Date: 2026-09-12

## Scope and severity

This audit covers the shipped JUCE editor shared by AAX, AU, and VST3: PRE and POST, all five
editor sizes, LEVEL / TIME / FREQ / SPACE / REF, Pair, Reference audition, local PRE/POST Blind,
transport stop/restart, hidden/reopened editors, and normal/Jungle appearance. The user's supplied
Pro Tools captures are evidence, not specifications embedded in those images.

- S0: audio integrity or materially wrong measurement
- S1: a product function is unavailable or cannot be reached
- S2: the UI can cause a wrong interpretation or operation
- S3: legibility, responsive layout, or finish below the product standard

## Findings

| ID | Severity | Finding and evidence | Root cause | Resolution / gate |
|---|---:|---|---|---|
| PT-01 | S1 | FREQ can remain empty while audio is playing. The supplied 300% captures show an empty LR and M/S plot. | Pro Tools may hide an editor without destroying it, so the hidden editor retained a process-wide optional analysis lease. Switching normal Spectrum and PSB also enabled the new lease without releasing the old one, consuming both slots from one editor. | Make Spectrum and PSB leases mutually exclusive, release both on page exit or `visibilityChanged(false)`, and reacquire only the visible subview on show. Verify by alternating PRE/POST windows, LR/M/S and PSB, then reopening FREQ. |
| PT-02 | S1 | TIME SHARP and LIVE can remain empty under the same conditions. | The perceptual, absolute, and attack leases had the same hidden-editor lifetime error as Spectrum. | Apply the same visibility ownership rule to every optional analysis page, not only Spectrum. |
| PT-03 | S1 | Pair appeared unavailable and the menu exposed only POST-only / selected / in-use states. | The menu described internal claim state but did not distinguish playback lock, an unavailable PRE, or a PRE claimed by another POST; raw instance IDs dominated unnamed candidates. | Use explicit `Stop playback to change connection`, `Use POST only`, `Use PRE`, `In use by another POST`, and `No available PRE` states. Retain the exact claim rules; verify with playing/stopped, zero, one, duplicate-name, and contended PRE candidates. |
| PT-04 | S2 | Pair text contained `Â·` in Pro Tools. | A UTF-8 middle dot passed through an AAX/host text path with a mismatched legacy decoding boundary. | Use an ASCII slash in host-facing menus and status copy. No dotted visual encoding is introduced. |
| PT-05 | S1 | REF looked unusable even though the license was confirmed; a full-width `RECHECK LICENSE` action occupied the measurement area. | Ownership and saved-work connection were collapsed into one access state. The action row was reserved even for a confirmed owner who only lacked a Kirin OS connection. | Show `WAITING FOR KIRIN OS` with the exact INSPECT route. Hide the license action for confirmed owners and return the row to the content area. |
| PT-06 | S2 | AAX PRE/POST Blind was not discoverable, so it looked broken. | AAX local Blind is intentionally fail-closed until exact-range AAX clock/PDC proof, but the product menu omitted the entry entirely. | Keep audio behavior disabled. Expose a disabled `PRE / POST Blind / AAX validation pending` item so the boundary is visible without implying availability. |
| PT-07 | S2 | `WARM 46S` was unexplained and resembled a tonal or quality judgement. | LRA warm-up elapsed time was emitted as a terse override in the LRA value cell. | Render `WARMING 46 S`, suppress the unrelated `LU` unit during warm-up, and preserve the official LRA readiness rule. |
| PT-08 | S2 | `30 S / 10 HZ` exposed an implementation cadence most users do not need. | The history sampling resolution was placed in the primary range selector. | Show duration only (`30 S`, `2 MIN`, `10 MIN`, `2 H`, `24 H`). Keep 10 Hz internal and in technical contracts. |
| PT-09 | S2 | `DAW`, `SESSION + DAW RUNS`, and the graph's clock basis were unclear. | Internal axis-mode names were copied directly to the visualization. | Use `DAW TIME`, `AUDIO TIME / RUNS`, and `AUDIO TIME`. |
| PT-10 | S2 | PLR looked like a nearly invariant time trace; its purpose was unclear. | PLR is a cumulative Meter Session fact (`session max TP - LUFS-I`), not an independent short-window waveform. Plotting each retained cumulative value exaggerates the expectation of movement. | Present the latest PLR as a restrained fact gauge, labelled `SESSION FACT / TP MAX - LUFS-I` at full sizes and `TP MAX - LUFS-I` at 150%. |
| PT-11 | S2 | SIDE `+` / `-` could be read as good/bad width or left/right level. | The polar plot showed signs without naming the signed M/S coordinate convention. | Label the axes `SIDE > 0`, `SIDE < 0`, `MID > 0`, and `MID < 0`; compact sizes use the same signs in abbreviated form. The sign remains the polarity of `(L-R)/2`, not a quality score. |
| PT-12 | S2 | TIME discontinuities after stop/restart were not vertically aligned between M, S, TP, and correlation. | Each path independently accepted its own finite value before applying generation/run boundaries; min/max columns could remain when a sibling primary fact was absent. | Gate M/S/TP means and ranges on one shared primary-validity boundary. Apply the same boundary to correlation and retain the same generation/run break. PLR no longer claims a time trace. |
| PT-13 | S2 | Stopped transport could continue to look live for roughly seconds, with the visible break appearing late. | The common heartbeat liveness window was 30 × 100 ms = 3 s, despite the current 200 ms supervisory boundary. A direct-engine regression also exposed that an expired heartbeat discarded already accepted Watch tail samples. | Use four 100 ms ticks = 400 ms, allowing one supervisory miss while separating callback stall from the independent 3 s musical-rest gate. Drain the finite accepted Watch tail before Inactive; Pair lock still reads the evaluator directly and unlocks at 400 ms. Verify stop for more than one second, then restart. |
| PT-14 | S3 | `TRACK/STEM` clipped to `TRACK/STE` at 300%. | The context control had a 94 px maximum independent of the resolved font width. | Increase its maximum allocation while preserving the one-row header. Verify both context names at all five sizes. |
| PT-15 | S3 | TIME's lower left axis unit (`LUFS`) and right unit (`dBTP`) collided with the lower lane and each other in supplied 300% captures. | Duplicate unit labels were anchored at the plot bottom while auxiliary lanes began at the same vertical boundary. | Remove the redundant bottom unit labels; the legend and values already state M/S/TP and units. |
| PT-16 | S3 | M and S endpoint marks were hard to read when values converged. | Both labels were attached to the same latest x-position with insufficient semantic separation. | Retain distinct fixed vertical offsets and rely on the top legend for exact values; tooltips are cleared on page changes so they do not cover the endpoints. |
| PT-17 | S3 | The 150% PLR definition truncated as `Session max TP...`. | The 300% definition was used in a half-width 150% label slot. | Use density-specific copy and size the PLR label from the exact string. |
| PT-18 | S3 | Analysis status collapsed to fragments such as `SYN`, `DAT`, and `OBS`. | Long internal status sentences were hard-clipped by painter width. | Replace them with bounded product states: `PREPARING ANALYSIS`, `ANALYSIS DATA UNAVAILABLE`, and `ANALYSIS IN USE / n`. |
| PT-19 | S3 | A tooltip could remain over a newly selected page and obscure the graph. | Page/domain changes did not dismiss the previous hover tooltip. | Hide the tooltip when domain, analysis page, size menu, or editor visibility changes. |
| PT-20 | S3 | The size popup could remain visible after resizing, show the old check mark, or stack behind the resized window. | Resize ran from the popup completion callback before JUCE had completed popup dismissal. | Dismiss active menus and defer the resize by one message-loop turn through a safe editor pointer. |
| PT-21 | S2 | Capture history described internal sampling (`10 HZ`) instead of measured meaning. | Diagnostic cadence was used as user-facing legend copy. | Use `M / momentary LUFS`, `TP / 2 S peak hold / dBTP`, and `60 S AUDIO`. |
| PT-22 | S3 | Host menus used dense middle-dot separators and could look corrupted or cheap across text paths. | Decorative Unicode separators were mixed with host-owned typography. | Use restrained ASCII slash separators for host-facing menus while keeping solid visual lines everywhere. |
| PT-23 | S3 | FREQ/SHARP/LIVE empty-state panes had no stable explanation when a slot was busy or data was not yet ready. | The status painter exposed abbreviated internal state and lease owners. | Use the bounded analysis status copy from PT-18. Functional silence remains only for background fallback failures; explicit user actions receive a result. |
| PT-24 | S3 | Version/build text consumed footer width in small real-host layouts. | Older captures used a uniform footer allocation. | Current responsive contract shows product version only where density permits; 100%, 125%, and 150% reserve footer width for actions and measurement. |
| PT-25 | S3 | Static render fixtures for FREQ and REF can appear empty even when the shell layout is correct. | The generic five-domain composite intentionally owns only the shell; external analysis/reference components are tested in dedicated composites. | Do not treat generic fixture emptiness as product success. Validate FREQ, SHARP, LIVE, and Reference using their dedicated populated components and in Pro Tools. |
| PT-26 | S0 audit | No evidence yet shows Hypha changing or interrupting the normal A-path audio. | The supplied “gap” is a history-presentation symptom. Audio interruption and measurement discontinuity are different failure classes. | Keep R-12 unchanged. The final host pass must still check reported 0 samples and uninterrupted playback; do not infer audio transparency from the graph alone. |
| PT-27 | S1 audit | Crash reports exist near the test period, but current evidence does not attribute them to Hypha. | The Pro Tools reports are multipart/minidump artifacts; the readable host log shows load/instantiate and many stop actions without a Hypha fault. A separate LocalBlind test `.ips` aborts in fixture loading because of its working directory. | Record as unresolved evidence, not a product-cause claim. Reproduce in the exact current build before assigning a Hypha crash defect. |
| PT-28 | S2 | The earlier capture mixed `POST`, disabled delta, and pair state without explaining why delta was unavailable. | Delta requires a verified simultaneous PRE and must fail closed; connection state and observation target were visually adjacent but semantically separate. | Keep delta disabled until verified pair data exists. Pair menu and bounded analysis states now explain the prerequisite without inventing POST absolute data. |
| PT-29 | S1 | The complete UI gate found the 100% Focus Trail changing-frame path at 4.56 ms, just over its 4.5 ms budget. | Every 30 Hz snapshot recomputed frequency-axis low-band calm weights even though the axis definition was unchanged. | Cache the display-only weights by the exact minimum/maximum frequency definition. The final isolated five-size Focus Trail gate passes at 2.325 / 2.509 / 2.807 / 3.482 / 5.565 ms changing-frame cost without changing measurement or appearance. |
| PT-30 | S3 test | The Jungle capture reversibility check failed intermittently although normal editor renders were deterministic. | Each comparison image generated a new current-time capture stamp, so crossing a one-second boundary created unrelated pixels. | Keep real Capture timestamps unchanged. Supply one fixed timestamp to every image in the appearance-only contract so it measures Jungle state and nothing else. |
| PT-31 | S1 | The empty rounded control beside MARK in the supplied FREQ captures looked like an unfinished field, and PSB could not be discovered. | The PSB/SPECTRUM toggle painted its control material, then inherited the material painter's final graphics colour instead of selecting a text colour; the label disappeared against the surface in the real host. | Set an explicit high-contrast text colour after painting the control, for both `PSB` and selected `SPECTRUM` states. Verify both states at all five sizes. |

## Verification matrix

The implementation is not accepted from screenshots alone. The final pass must record:

1. Static render contracts for PRE and POST at 300×200, 375×250, 450×300, 600×400, and 900×600.
2. Dedicated populated renders for FREQ LR/MID/SIDE/M/S, TIME HISTORY/SHARP/LIVE, SPACE, VU,
   Reference access, normal appearance, and Jungle appearance.
3. Pro Tools Developer AAX: PRE + POST insertion, Pair zero/one/contended states, hidden/reopened
   editor analysis ownership, REF access, disabled AAX Blind reason, every size preset, and
   stop for more than one second followed by restart.
4. Reported latency and uninterrupted A-path playback are checked separately from visible history
   discontinuities.
5. Performance and source-budget gates remain green; no dashed or dotted line encoding is accepted.

## Current automated evidence

- Complete JUCE UI render contract, including normal/Jungle reversibility and all five editor sizes:
  pass.
- Five-size Focus Trail changing-frame cost: 2.325 / 2.509 / 2.807 / 3.482 / 5.565 ms;
  all below the density-specific gates.
- TIME one-slot / two-slot / changing-frame cost: 8.187 / 16.204 / 8.287 ms per tick;
  the two-slot path remains below its 25 ms gate.
- Static 5-size PRE/POST domain compositions and dedicated populated FREQ / SHARP / LIVE renders:
  generated and visually inspected without text overlap. Dedicated FREQ and PSB renders visibly
  show both `PSB` and selected `SPECTRUM` labels after PT-31.
- The post-fix measure library passes 1,454 tests with 9 intentionally ignored tests. The FFI
  library passes all 86 tests, including 48,000 active frames published in 0.69 seconds after the
  accepted-tail correction. JUCE lifecycle wiring passes 16 tests; FFI panic-safety and RT handoff
  contract suites pass 3 and 1 tests respectively. The monolithic workspace invocation reached
  these product suites but stopped at one obsolete Pair-menu string assertion; that assertion was
  corrected and its complete 16-test suite rerun green.
- Pro Tools Developer post-fix pass: still required. Native computer-control is not exposed in the
  current Codex surface, and the regular Pro Tools process is open; do not replace its loaded signed
  diagnostic AAX bundle with an unsigned build while that session is active.
