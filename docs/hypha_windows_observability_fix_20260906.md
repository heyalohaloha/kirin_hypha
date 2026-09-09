# Windows observation and readability repair

Status: implementation complete; full validation has remaining gates. Some final macOS test
executables stalled before entering their test harness. Windows host validation is paused at
Daisuke's request because another implementation is using that machine.
This is not a public release and not a claim that every Windows observation is resolved.
Baseline: B-701 `d1e6b53`; responsibility extractions: B-702 `0c9ed28`,
B-703 `70e561e`, B-704 `37b477c`.

## Scope and preserved boundaries

PRE and POST share typography, responsive shell geometry, meter facts and feedback placement.
PRE retains LEVEL / TIME / SPACE; no public PRE optional-analysis pages are added.
SHARP remains exact PRE/POST difference, LIVE remains local POST absolute, and RUN remains
an absolute summary of the selected history resolution. Their effective target never overwrites
the user's preferred POST/delta target for pages that support both.
Normal A audio, Record, TRACE and persisted PSB are unchanged. Optional analysis retains one
mode per owner and the existing two-owner limit. The audio callback publishes the already
computed live-input flag with one atomic store; no new analysis, allocation, lock or I/O is
added to that callback. The existing Watch silence timeout is unchanged.

## Responsibility map

- `PluginEditor`, `PluginEditorMeter`, `PluginEditorObservatory`, `PluginEditorAnalysis`,
  `PluginEditorCapture`: base polling, active-page routing, effective target, frozen capture.
- `HyphaObservatoryContract`, `HyphaObservatoryView*`, `HyphaObservatoryMetrics`,
  `HyphaObservatoryLevelStrips`, `HyphaTypography`, `HyphaTheme`, `HyphaTimePageNavigation*`:
  role-independent type, chrome, hit bounds, feedback and lifetime definitions.
- `HyphaSpectrum*`, `HyphaPsbPainter`, `HyphaPerceptual*`, `HyphaAbsolute*`, `HyphaAttack*`,
  `HyphaTimeHistoryPainter`, `HyphaCaptureHistory*`, `HyphaRunSummary`, `HyphaSpacePainter`:
  factual drawings, labels, time direction, missing-data and stopped-live presentation.
- `meter_session`, `watch_display_ffi`: maxima captured by the non-RT producer, independent
  from UI polling, transport passes, Record and editor lifetime.
- `phase_d` display adapter, `perceptual`, `absolute_timeline`, `perceptual_exchange_codec`,
  `analysis_exchange_transport`, new PSB FFI: optional exact display shares only.
- CMake, Rust/C++ contracts and CI: production-route, lifecycle, ABI and visual regression gates.

## Display PSB definition

Display PSB uses the existing 240 specific-loudness bins at an exact 100 ms endpoint.
Aggregate twelve adjacent bins into each of twenty equal-width groups covering 0–24 Bark,
average independently measured channel contributions for LR, then normalize the twenty
contributions by their sum. Zero total is unavailable, not twenty fictitious equal shares.
Absolute units are percent; POST-minus-PRE units are percentage points. Both sides must share
the exact presentation endpoint, state epoch, channel definition and analysis format.
The existing Record `psb_bark` formula and files are not relabelled or rewritten.
Reuse the optional psychoacoustic worker, not the Watch/Record result or FFT spectrum bins.

## Completion gates

1. PRE/POST five sizes and free-resize boundaries: no necessary label overlap, truncation or
   forced horizontal text shrink. Context, names, status and disabled controls remain readable.
2. Real input reaches PSB through both local absolute and paired exact routes. Malformed,
   silent, stale, unpaired and slot-in-use states remain distinct and never reuse old shares.
3. Meter maxima and history continue with the editor closed. RESET is the session boundary.
4. Page-specific controls, preferred/effective target and Capture metadata agree.
5. ATTACK uses one selected event identity, accepts late details and clears live ink on inactivity
   without destroying explicit locks or historical/session facts.
6. Focus Trail preserves factual gaps and recovers retained batches; no invented samples.
7. Cargo test/clippy, ignored FFI parity/pairing, C++ contracts, Windows build and real-host
   inspection must be reported separately. Missing test executables do not count as passing.
8. A-path bit identity / zero latency and Record/pairing regressions remain mandatory.

Windows baseline loaded PRE SHA-256: `F17FBC015C71C6D1FA6028F9DFFD39F5C1EF43B9D357E578856A8EABEC609209`.
Windows baseline loaded POST SHA-256: `E1313FE4933A757453488820E116A271F406F86DAFBB056D7688587D582AF1E1`.
The baseline version string is insufficient to establish its source commit.

## Additional findings during verification

- Windows compilation exposed an existing `windows.h` `max` macro collision in
  `ReferenceBlindSession` and an MSVC lambda-capture incompatibility in the ATTACK UI test.
  Use the macro-safe maximum call and capture the test's local constants explicitly.
- The POST CMake source list omitted `HyphaReferenceVisuals.cpp`; restored it to the common
  plugin source list to resolve the existing undefined reference on a clean build.
- Static wiring tests now inspect the extracted processor/editor modules, retaining their
  original assertions rather than accepting missing production paths.
- The ATTACK integration test discarded successful non-blocking observations and then
  unwrapped another `try_read` poll. It now consumes the observations obtained within the
  same original two-second deadline; this changes no production locking or timing policy.
- HISTORY rendering was variable on the Windows software renderer. Retain only immutable
  backdrop resampling and unchanged domain decoration, keyed by size, physical pixel scale
  and all visually relevant state. Dynamic values invalidate the decoration immediately.
  Pixel comparisons cover resize, DPI and state transitions; the performance gate also
  measures continuously changing live values. No opacity or PRESENCE settings were changed.
- Preview exports append when a previous PNG output stream is reused. Every validation run
  uses a fresh temporary directory so visual evidence belongs to the tested binary.
- Recovering every retained exact Spectrum frame exposed a backwards-seek boundary: a batch
  could reinsert an old high endpoint before the new low endpoints. Restrict each worker's
  acquisition-ordered history independently to its current run before taking the exact
  intersection. Regression tests cover retained old frames, staggered restarts and real gaps.
- Reference-curve image tests now use the data plot below the two-row chrome and compare
  composited translucent ink, not an opaque palette swatch. The original 90% continuous
  coverage gate remains; the macOS run observed 231/231 columns for both PRE and POST.

## Reported observations and implemented acceptance checks

| Observation | Structural response / required check |
| --- | --- |
| POST ABS / POST button meaning | Absolute/delta capability shared by label, hit target and data route; compact Spectrum says POST dBFS. |
| Large 6 S FIELD HOLD hides PSB | Separate two-row Spectrum chrome; PSB hit region has its own width. |
| PSB appears nonfunctional | Dedicated display ABI and optional worker route, real 20-band shares, percent versus percentage points, explicit unavailable states. |
| Spectrum time field is indistinct | Stronger six-second field with NOW / -6s direction labels; frequency magnitude remains factual. |
| RUN is unreadable / unclear | Full navigation row, readable labels, absolute run facts, empty-state explanation; visiting RUN preserves preferred delta selection. |
| ATTACK blockiness / missing left-right facts | Compact sizes prioritize four numeric facts; larger sizes reserve a separate specimen area; late details are polled without waiting for a new raw endpoint. |
| Sound and drawing seem out of sync | Exact sample identities and complete retained batches; LIVE curves use 100 ms observations, numeric readouts retain their documented cadence. No assertion of zero visual latency. |
| Unexplained white line / frozen last drawing | Remove ATTACK selection line; live-inactive body clears to black, while an explicitly locked event remains inspectable. |
| ATTACK allows FOCUS / adjacent controls | Page capability contract removes irrelevant loudness/history controls and their hit targets. |
| HISTORY PLR unreadable / horizontal | Readable dB label and Session max TP - I definition; fixed 0–24 range, with a separate compact auxiliary row. A stable PLR may truthfully stay horizontal. |
| KEEP overlaps READY TO BOUNCE | Session and explicit-action feedback use an independent footer row; preserve the underlying Record/Keep operation. |
| Focus Trail unstable / broken | Publish every retained exact match; preserve real missing endpoints and do not interpolate invented observations. |
| 150% items beside 2MIX unreadable | Context owns the first header row; domain navigation owns the second. |
| LEVEL purple / orange meanings unclear | Colored M momentary LUFS and TP two-second peak dBTP legends; readable axes and supporting facts. |
| MAX text too small | Shared font floor and larger label bounds; maxima originate in the producer and survive editor closure until explicit RESET. |
| SHARPNESS seems unavailable with POST alone | SHARP shows local POST Sharpness on its 0..3 acum scale until an exact pair exists, then switches the same page to signed POST-minus-PRE without relabelling either value. |
| LIVE seems unrelated to delta | Fixed POST absolute target, disabled target switching, explanatory help and consistent capture metadata. |
| PRE / common small text | Apply common typography, geometry, maxima and capture fixes to PRE and POST, keeping PRE's existing page boundary. |

## Evidence and remaining Windows gate

- Final `cargo clippy --workspace --all-targets --locked`: pass. Owned code has no warnings;
  upstream vendored warnings remain outside the audit scope. Log:
  `/tmp/hypha-clippy-final-confirm.log`.
- `cargo fmt --all -- --check`, source-line budget, `git diff --check`, and final
  `cargo test --workspace --locked --no-run`: pass. Compilation is not a substitute for
  running the full suite.
- Latest ignored FFI parity suite: 20 pass. The five-test pairing-candidates suite passed
  earlier in this session, but its final rerun did not enter the test harness before the
  validation pause. Counts were measured with anchored `: test$` enumeration.
  Logs: `/tmp/hypha-parity-currentrun.log`, `/tmp/hypha-pairing-final.log`,
  `/tmp/hypha-pairing-currentrun.log`.
- Full workspace run: 227 tests passed through JUCE lifecycle wiring, zero failures in those
  completed suites; the remaining suites are not certified by that partial result.
  Log: `/tmp/hypha-workspace-test-final.log`.
- Latest macOS PRE/POST Debug and the five additional native contract targets built
  successfully. The final native CTest run remained at its first executable's startup;
  it is not a passing five-test result. ATTACK and the separate optimized UI contract
  passed earlier in this session. Logs: `/tmp/hypha-native-final-build.log`,
  `/tmp/hypha-native-final-tests.log`, `/tmp/hypha-attack-contract-final.log`.
- macOS optimized UI contract: pass, including all five sizes, DPI/state cache comparisons,
  page controls, captures, Focus Trail and reference continuity. HISTORY measured
  11.8632 ms one slot / 23.3671 ms two slots / 9.23505 ms changing live state against
  unchanged 12.5 / 25 / 12.5 ms gates. Logs: `/tmp/hypha-mac-ui-recheck.log`.
- Current-run exchange regressions: 45 pass, 0 fail, 2 explicitly ignored performance probes.
  Log: `/tmp/hypha-exchange-final.log`.
- Real PSB signals include mono, in-phase/anti-phase stereo, 200 Hz, 1 kHz, 6 kHz and silence.
  Mono/stereo/polarity share error is bounded below 1e-12, normalized sum error below 1e-9,
  and shifted endpoints cannot produce a difference.
- Windows final source build, including the backwards-seek repair: PRE/POST Debug pass;
  optimized UI-test executable build pass. This last build was completed before the Windows
  pause, but was not deployed or exercised in Studio Pro. Log: `/tmp/hypha-windows-build-currentrun.log`.
- Before the last seek repair, Windows seven native contracts and the actual Debug VST3
  audio-transparency host passed: PRE/POST stereo realtime/offline and mono realtime,
  299,680 checked samples total, bit-identical output, zero reported latency.
- Windows whole UI contract is not green: timing failures occurred under simultaneous Studio
  playback/export load. Standalone HISTORY samples improved, but do not substitute those for
  a passing complete suite. Re-run with the host stopped, without PNG exports, after access is
  coordinated; do not widen the timing budgets. Export fresh PNGs in a separate run.
- Windows PNGs already copied locally are deterministic fixtures, not evidence of successful
  Studio Pro interaction. Native PRE 150%, POST LEVEL 300%, HISTORY 150% and ATTACK 200%
  fixtures were inspected; fresh final PSB legend images and actual playback/stop behavior
  still need host verification. End-to-end audiovisual alignment is not yet established.

### macOS startup diagnostic and remaining local gate

A one-second sample of the waiting workspace `pairing_candidates` executable at 11:47:58 JST
showed all 620 samples at `_dyld_start + 0`, a 12 KB physical footprint and no loaded-image
description. The executable had been launched at 11:43:12 and had not printed the Rust test
harness header. This supports a pre-test startup stall, not a test-body deadlock; it does not
identify the underlying OS cause. Diagnostic: `/tmp/hypha-pairing-startup-sample.txt`.
Do not disable security checks, restart the computer, or close another application's process
as an implicit fix. The extra test-enumeration attempt was stopped and did not run fixtures.
At 11:50 JST the pending workspace, pairing and native-contract processes owned by this
session were terminated after checking their exact PIDs and executable paths. Their runs
are incomplete, not assertion failures. No other application's process was stopped.

Once executable startup works normally, rerun the complete workspace suite, the ignored
five-test FFI pairing suite and the five native contracts. Preserve the successful parity,
UI and compilation results above, but do not label all local tests green until those gates
complete. Windows validation remains a separate gate regardless of the macOS result.

### Windows state at pause — coordinate before resuming

No further remote command, UI input, deployment, rollback or cleanup is authorized by this
handoff alone while another implementation owns the machine.

- Last observed Studio Pro song: `Peach_Hypha_Demo*`, playback active, Main stereo;
  Kirin OS was already open. Neither application was closed, and no song save or audio-device
  setting was intentionally changed. These are last observations, not current-state claims.
- Installed diagnostic PRE SHA-256:
  `068D3956D6E73B9D1DAB3521CADED61EAE297CFCE74B5E90A671ADD628D56DEA`.
- Installed diagnostic POST SHA-256:
  `BB10616E16596383E17A6710266E10165D6CD436A30E7D52C2171591BA4D9C49`.
- Recoverable original bundles:
  `C:\Users\hello\Dev\vst3_backups\hypha-observability-20260906-105924`.
- Isolated latest build:
  `C:\Users\hello\Dev\validation_staging\hypha_observability_20260906`.
- Temporary manual-only tasks created for this session:
  `HyphaObservabilityUI20260906` and `HyphaObservabilityActions20260906`.
  They have no recurring trigger. The first task owns the Studio Pro launch process; do not
  stop it while the song may be open. After safe host closure, remove only these two exact
  tasks. Cleanup is deferred to respect the Windows pause. Existing validation tasks and
  permanent SSH/RustDesk services must remain untouched.
- Recheck installed hashes and current host ownership before deploying the latest candidate;
  another implementation may have changed the machine after the observations above.

This is a diagnostic Debug build, not a public release or signed installer candidate.
No version bump, publication, notarization, macOS installation or Notion write is performed.
