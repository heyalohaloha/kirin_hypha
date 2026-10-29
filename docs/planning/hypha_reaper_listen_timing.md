# REAPER VST3 LISTEN timing

This is the 1.1.51 implementation contract, updated by the user decision of 2026-10-13. Real-host LISTEN/MATCH/BLIND output acceptance is
separate from the reduced timing measurements below. It does not expand AU/AAX, Studio One,
Studio Pro or Logic certificates.

## Independent counter and linear proof

The existing native executable/version certificate (`REAPER` or `reaper`, a readable version
starting with an ASCII digit, VST3 only) selects `ClockBasis::reaperProcessing`. Each side counts
only its own processed frames. This is neither VST3 continuous time nor a common-origin clock.
Missing optional VST3 continuous time and presentation latency are not replaced by zero values.

The compensated project positions corroborate the offset:

```
K = (Cpost - Ppost) - (Cpre - Ppre)
PRE address = Cpost - K
```

The existing eight-block calibration, coherent PRE run/owner/sequence, selected pair/rate,
retained PCM range and checks before/after copying still apply. A disagreement, generation change,
stop, seek or unexplained gap removes PRE at the start of that block. Named transport re-entry
retains the existing approved gains and selection authority for LISTEN and BLIND. Unverified
blocks use POST; fresh proof automatically resumes the same selection and hidden assignment. MATCH reads only the continuous proven history. State
serialization, audio latency and normal A/measurement/Record data are unchanged.

## Burst pauses and missing callbacks

The raw gap detector retains its thresholds. REAPER additionally checks the actual local counter
and project movement. A linear continuous project run can explain a pause; a loop requires a
measured same-run sample length, a corroborated wrap and a pause shorter than that whole lap.
The POST reader independently checks the coherent PRE owner/generation, monotonic sequence,
processed counter advance and project advance. A gap without peer progress is not excused.
The known address age also fences a POST side that missed a whole loop while PRE continued.
Changing threads is not a discontinuity. No wall time is converted into a PCM address.

All checks are fixed-size Audio Thread state and lock-free metadata reads; there is no new
allocation, lock, host API call, worker, PCM capacity or blocking I/O. The existing shared timing
header uses a previously unused flags bit for the raw gap. Saved plugin state and C ABI do not
change. Chain tracing remains environment-opt-in; disabled tracing creates no recorder.

## First-loop chain proof and interval coverage

`ReaperPdcAdapter` reads the public `IReaperHostApplication` and `TrackFX_GetNamedConfigParm`
interfaces from the existing message-thread service. No host call runs in the audio callback.
Only a normal serial main track with one PRE and one POST, known VST3 class identities, unique
ordered FX GUIDs, ordinary PDC mode and no oversampling is admitted. Take/input/container/parallel
routes and external hardware inserts fail closed. The same process, live project/track and complete configuration fingerprints
must match across the selected pair. Pre-playback `chain_pdc_actual` is discarded regardless of
its success flag. Read brackets and two progressing callback observations fence restarts and
configuration changes. The project change count only brackets each read and is not part of the
chain fingerprint, so unrelated faders and parameter edits do not revoke a stable chain. Atomic payload words plus sequence validation avoid data races.

The single scheduling constant is `ReaperTimingLimits::scheduling` (8192 samples). Static discovery
with anticipative processing on/off observed relative PRE age of D plus one 512-frame block.
The allowance was fixed before that analysis; it is neither a measured maximum promoted to a
guarantee nor the host's render-ahead duration. Supported actual delay is 0–65536. The dynamic
upper age is the API value plus the allowance, with complete block age and capacity checks.

The entire `[E-U,E)` interval must be covered by at most two native callback maps of the same
PRE generation. Exactly one project-to-counter address is required; no PCM correlation or newest
arrival chooses a lap. A map split is at the next callback start, not the actual loop boundary
inside the preceding callback. Admission of the current crossing block uses its current proof;
the next callback separately verifies the wrap and preserves the maps. This covers the first
delayed POST tail before POST itself wraps. Once measured, L must exceed U. An unverified block
or short prefix waits; BLIND retains its assignment while POST covers that wait.
The existing eight-block calibration still applies to REAPER. The API value is an upper bound,
never K. Without it, the previous linear-proof continuation retains its fixed 73728-sample
verified-address envelope. REAPER's measurement-only compensated project coordinate remains K=0.

Relevant playing instances poll at 50 ms; idle instances check callback activity at 250 ms
without querying host APIs. Tokens expire after 500 ms or two seconds of processed samples.
Expiry preserves the fingerprint, delay, timing generation and K but withholds PRE until the
same proof is current again. BLIND also resumes with the same assignment after fresh proof.
These fixed lifetimes do not prove host notification freshness.
Prepare/context changes, stop/seek, callback gaps, configuration edits, failed reads and identity
changes revoke the old token and timing generation. RT uses no allocation, lock, I/O or host API.
The private ring uses version 9 and a separate name; saved plugin state and public C ABI do not
change. Diagnostics alone use a 272-block queue (under 36 MiB per traced instance), allocated only when tracing is enabled.

[REAPER VST3 interface](https://github.com/justinfrankel/reaper-sdk/blob/main/sdk/reaper_vst3_interfaces.h)
and [named configuration API](https://www.reaper.fm/sdk/reascript/reascripthelp.html#TrackFX_GetNamedConfigParm)
are the acquisition contracts. Notification order during dynamic PDC changes and broader
scheduling conditions still require real-host output acceptance, under R-12's existing premise.

## Reduced timing measurements and INV-LC5

REAPER 7.71, macOS, stereo, 96 kHz, 512 frames; anticipative FX off/on, 200 ms render-ahead:

- Linear delay 0 and 4096: every decoded frame matched the oracle after eight-block calibration.
- Reported delay increases 0→4096 and bypass release: upstream processing advanced, K stayed
  unchanged, and no mismatched interval was observed.
- Reported decreases 4096→0 and bypass reductions: 12 transitions detected a project jump in the
  same POST block, with an observed undetected window of 0 ms at callback resolution.
- The first four-second loop: L became known at the first wrap. An experimental age bound of
  73728, declared before reading the recordings, yielded one correct candidate. This is an
  experiment under that bound, not a runtime guarantee of the bound.
- Anticipative execution used 20 processing threads and bursts; no replay-time callback gaps or
  correspondence errors were observed in the reduced sample.
- Unreported 0→4096 delay: clocks did not detect the error for about seven seconds before stop.
  The analyzer now right-censors this interval at stop; it does not call the stop a detection.

These measurements did not audition PRE. Output acceptance must repeat with real PRE/MATCH and
both BLIND sources. Larger delays, mono, different rates/buffers, FX insertion/removal and Windows
remain unaccepted on a real host. R-12's correctly reported latency and host PDC premise remains.

## Regression and acceptance

Portable tests use an in-memory instance of the production atomic ring, production publisher,
observer, consumer, renderer and Blind state. Distinct sample IDs across laps check actual PRE
output; POST input is attenuated so an output-source assertion is meaningful. Cases cover
independent origins, missing VST3 optional clocks, mono/stereo, delay increases via PRE bursts,
decreases/project jumps, MATCH gain, stop/seek/pair/owner changes, full-lap loss, short-loop
boundaries and the unreported negative. RT allocation/free counts must both be zero.

`kirin_reaper_listen_mono_product` and `kirin_reaper_listen_stereo_product` use real common
processors, Rust workers, native shared memory, MATCH and BLIND receipts. They require OS shared
memory permission. The REAPER timing UI contract renders English/Japanese at 300×200, 375×250 and
450×300, checks actual footer paint/full-detail access and label/control bounds, and preserves END.

Host acceptance must use the built candidate's hashes, complete zero-drop chain traces and the
independent sample-ID oracle. Check same-block fallback, normal A bit identity/zero latency,
gain/ceiling and END/RETURN, source receipts, stop/seek/reopen and negative censoring separately.
Non-stable fades and gained output that cannot be decoded remain explicitly unclassified by the
trace analyzer; a zero error count with unclassified frames is not whole-output acceptance.

First-loop regression targets are `kirin_reaper_loop_entry` (actual output, split wrap, burst,
short/ambiguous coverage and BLIND recovery), `kirin_reaper_pdc_adapter` (identity, API errors,
preplay values, epochs, expiry and atomic coherence), and `kirin_chain_trace_burst` (256 undrained
callbacks). `kirin_logic_cycle_entry` separately checks Logic render/presentation proof, zero delay,
within-block wraps, pre-roll movement, paired analysis qualification and irreversible trial loss.
Callback timing fixtures use the common product test clock. `kirin_live_compare_match` also
measures production MATCH windows across the first wrap and later cycles. The twelve
`kirin_logic_analysis_{mono,stereo}_{included,zero}_cycle_{freq_listen,sharp_listen,blind}_product` cases exercise real processors,
workers, cycle delta, LISTEN, MATCH and BLIND loss; they require native shared memory.
Fixtures are not host acceptance.
