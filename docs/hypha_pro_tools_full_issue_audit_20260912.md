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
| PT-01 | S1 | FREQ can remain empty while audio is playing. The supplied 300% captures show an empty LR and M/S plot. | The JUCE shell represented one Rust Analysis slot with several independent booleans and imperative calls. In particular `setPsbVisible(false)` did not mean “hide PSB”; its argument selected absolute rather than delta, so leaving FREQ could start a hidden absolute analysis. Rust still owns one slot per POST instance; the earlier claim that one editor consumed both process slots was incorrect. | Replace all page-specific starts/stops with one typed demand and one owner token. `none` is the only release state; editor hide, VU/Blind replacement, page exit, destruction, and engine restoration pass through the same application point. Verify LR/MID/SIDE/M+S/PSB transitions and stale-editor rejection. |
| PT-02 | S1 | TIME SHARP and LIVE can remain empty under the same conditions. | Analysis lifetime was distributed across page callbacks, visibility callbacks, timer-driven Pair updates, and processor restoration. A hidden SHARP page could observe a Pair change and request analysis again after the visibility callback had released it. | Derive the sole demand from effective surface visibility plus page/subview/Pair state. Hidden surfaces always derive `none`, so background refresh cannot reacquire; repeated identical requests do not restart analysis. Apply this to FREQ, SHARP, LIVE, and DRUM. |
| PT-03 | S1 | Pair appeared unavailable and the menu exposed only POST-only / selected / in-use states. | The menu described internal claim state but did not distinguish playback lock, an unavailable PRE, or a PRE claimed by another POST; raw instance IDs dominated unnamed candidates. | Use explicit `Stop playback to change connection`, `Use POST only`, `Use PRE`, `In use by another POST`, and `No available PRE` states. Retain the exact claim rules; verify with playing/stopped, zero, one, duplicate-name, and contended PRE candidates. |
| PT-04 | S2 | Pair text contained `Â·` in Pro Tools. | A UTF-8 middle dot passed through an AAX/host text path with a mismatched legacy decoding boundary. | Use an ASCII slash in host-facing menus and status copy. No dotted visual encoding is introduced. |
| PT-05 | S1 | REF looked unusable even though the license was confirmed; a full-width `RECHECK LICENSE` action occupied the measurement area. | Ownership and saved-work connection were collapsed into one access state. The action row was reserved even for a confirmed owner who only lacked a Kirin OS connection. | Show `WAITING FOR KIRIN OS` with the exact INSPECT route. Hide the license action for confirmed owners and return the row to the content area. |
| PT-06 | S2 | AAX PRE/POST Blind was not discoverable, so it looked broken. | AAX local Blind is intentionally fail-closed until exact-range AAX clock/PDC proof, but the product menu omitted the entry entirely. | Keep audio behavior disabled. Expose a disabled `PRE / POST Blind / AAX validation pending` item so the boundary is visible without implying availability. |
| PT-07 | S2 | `WARM 46S` was unexplained and resembled a tonal or quality judgement. | LRA warm-up elapsed time was emitted as a terse override in the LRA value cell. | Render `WARMING 46 S`, suppress the unrelated `LU` unit during warm-up, and preserve the official LRA readiness rule. |
| PT-08 | S2 | `30 S / 10 HZ` exposed an implementation cadence most users do not need. | The history sampling resolution was placed in the primary range selector. | Show duration only (`30 S`, `2 MIN`, `10 MIN`, `2 H`, `24 H`). Keep 10 Hz internal and in technical contracts. |
| PT-09 | S2 | `DAW`, `SESSION + DAW RUNS`, and the graph's clock basis were unclear. | Internal axis-mode names were copied directly to the visualization. | Use `DAW TIME`, `AUDIO TIME / RUNS`, and `AUDIO TIME`. |
| PT-10 | S2 | PLR looked like a nearly invariant time trace; its purpose was unclear. | PLR is a cumulative Meter Session fact (`session max TP - LUFS-I`), not an independent short-window waveform. Plotting each retained cumulative value exaggerates the expectation of movement. | Present the latest PLR as a restrained fact gauge, labelled `SESSION FACT / TP MAX - LUFS-I` at full sizes and `TP MAX - LUFS-I` at 150%. |
| PT-11 | S2 | SIDE `+` / `-` could be read as good/bad width or left/right level. | The polar plot showed signs without naming the signed M/S coordinate convention. | Label the axes `SIDE > 0`, `SIDE < 0`, `MID > 0`, and `MID < 0`; compact sizes use the same signs in abbreviated form. The sign remains the polarity of `(L-R)/2`, not a quality score. |
| PT-12 | S2 | TIME discontinuities after stop/restart were not vertically aligned between M, S, TP, and correlation. | The main curves and CORR mapped the same history endpoint into different physical X rectangles. A later attempted repair also gated every curve on simultaneous M/S/TP finiteness, which hid valid 3 s S, TP, or CORR facts when another measurement window was unavailable. | Use one physical timeline X range and the same generation/run break for every time-derived curve. Validate finiteness per metric so missing M does not erase valid S/TP/CORR. PLR remains a cumulative fact gauge rather than a time trace. |
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

## Why pre-host verification did not stop these defects

The source and current test registration establish the detection gaps below. Historical execution
records do not identify the exact loaded commit for every screenshot, so “not run” is not inferred
from a later CI condition. Where no candidate receipt exists, the historical result remains
untraceable rather than being reconstructed from memory.

| Findings | Pre-host detectability | Detection gap / current correction |
|---|---|---|
| PT-01, PT-02 | Yes | Component snapshots proved that plots could draw, but did not exercise editor visibility, page changes, Pair timer updates, Processor restoration, and the real Analysis lease as one sequence. The typed surface demand now makes hidden state `none`; its transition/owner contract rejects a stale editor and verifies all 15 executable demands. Real-host confirmation remains required. |
| PT-03 | Mostly; Pro Tools menu timing remains host-specific | Candidate rules and popup copy were tested separately, not as playing/stopped/contended interaction states. The acceptance matrix now requires zero/one/duplicate/in-use candidates and a playback lock in one sequence. |
| PT-04, PT-22 | Source-level risk was detectable; mojibake manifestation is host-specific | Host-facing copy admitted decorative non-ASCII separators without an encoding-boundary rule. Host menus now use ASCII slashes and the host pass still checks the rendered string. |
| PT-05, PT-28 | Yes | License ownership, saved-work connection, Pair verification, and observation target were accepted as neighbouring states rather than an explicit state matrix. Dedicated access and Pair fixtures now keep these prerequisites separate. |
| PT-06 | Yes | Audio fail-closed behavior existed, but the UI contract did not require a discoverable disabled reason. Product-entry tests now require the AAX validation-pending entry. |
| PT-07–PT-11, PT-21 | Yes, with product/measurement review | Tests asserted that values and labels existed, not that the copy conveyed the measurement window or avoided a quality judgement. The product contract now fixes WARMING, hidden 10 Hz cadence, clock-basis copy, PLR as a fact gauge, and signed SIDE axes. Pixel tests remain insufficient for semantic acceptance. |
| PT-12 | Yes | Fixtures made M/S/TP/CORR finite together and checked normalized time before separate plot transforms. That could not reveal either cross-metric data loss or physical X drift. Partial-missing fixtures and one shared physical timeline projection now cover both independently. |
| PT-13 | Partly; final latency is host-specific | The liveness threshold was unit-tested as configured, but no acceptance sequence tied accepted tail, 0.1/0.4/1/3 s stops, restart, and visible run boundaries together. Core tests now cover the 400 ms/tail rules; the exact Pro Tools chronology remains open. |
| PT-14–PT-18, PT-24, PT-31 | Yes | Width/alpha/image-difference checks allowed clipped, overlapping, abbreviated, or same-colour text to pass. Tests now use the exact product strings, populated states, allocated bounds, and contrast checks at all five sizes; the final host typography pass remains required. |
| PT-19, PT-20 | Mostly; host popup ordering remains host-specific | Static layout could not exercise tooltip/page lifetime or popup-dismiss/resize ordering. The interaction contract now includes page dismissal and deferred resize; Pro Tools still verifies its native modal order. |
| PT-23, PT-25 | Yes | Generic shell images were interpreted too broadly even though they intentionally contained no external Analysis/Reference data. Acceptance now names the dedicated populated components and treats shell-only output as layout evidence only. |
| PT-26 | Audio transparency is pre-host testable; DAW continuity is host-specific | A correct graph was allowed to stand in for A-path evidence. Bit identity/zero-latency and visible-history continuity are now separate gates; neither proves the other. |
| PT-27 | Not fully without the exact host report/build | Nearby crash artifacts lacked an exact Hypha attribution. The correction is evidentiary: preserve the report, exact commit and reproduction conditions, and do not mark either cause or resolution without them. |
| PT-29 | Yes; it was detected by the complete gate | A focused entry/layout run did not include the changing-frame performance path. The complete render gate found it; cached axis weights and the dedicated five-size performance fixture close the defect. Partial runs are no longer reported as full UI acceptance. |
| PT-30 | Yes | The appearance fixture injected current time, so its own unrelated timestamp made the comparison nondeterministic. A fixed test-only timestamp isolates Jungle reversibility without changing the product timestamp. |

The main process failure was therefore not one missing test. It was the combination of favourable
fixtures, disconnected layers, weak visual or semantic assertions, partial-suite ambiguity, and
candidate identity that was not always recorded. The release-source gate now registers the focused
TIME history contract in its exact CTest inventory in addition to the complete UI run. Focused
development tests remain local evidence only; they are not a release or host-pass claim.

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

- The focused Analysis-demand contract passes all 15 canonical demands, owner replacement,
  stale-owner rejection, hidden-surface release, and unavailable-ATTACK rejection.
- The focused TIME contract passes S-only, TP-only, CORR-only, all-missing, and shared physical-X
  projections. Its Debug correctness run measured 11.920 / 23.297 / 11.976 ms per tick for one-slot,
  two-slot, and changing-frame paths; shipping performance limits remain a Release-build gate.
- PRE and POST Debug VST3 targets compile after the coordinator replacement. The focused Rust
  source/wiring suites pass 2 ATTACK-wiring tests and 16 JUCE-lifecycle tests.
- Source line-budget, shell syntax, whitespace, public-history, and clippy checks pass. The single
  integrated run passed its Release native 14/14, measure 1,454/1,463 with nine intentional ignores,
  and FFI 86/86 stages, then stopped on one stale xtask Pair-menu copy assertion after 139/140 xtask
  tests had passed. B-834 changes only that assertion; its focused test passes, followed by the
  inventory-pinned realtime parity 20/20 and Pair-candidate 6/6 suites. The complete wrapper was not
  restarted, preserving the agreed one-run policy; this is composite source evidence rather than a
  second monolithic receipt.
- Pro Tools Developer post-fix pass: still required. Native computer-control is not exposed in the
  current Codex surface, and the regular Pro Tools process is open; do not replace its loaded signed
  diagnostic AAX bundle with an unsigned build while that session is active.
