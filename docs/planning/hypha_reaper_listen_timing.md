# REAPER VST3 LISTEN timing

This is the 1.1.51 implementation contract. Real-host LISTEN/MATCH/BLIND output acceptance is
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
retains only the existing approved gains and selection authority; BLIND stops after any unverified
block and never resumes the old trial. MATCH reads only the continuous proven history. State
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
change. Stage 1 chain tracing remains environment-opt-in; disabled tracing creates no recorder.

## Loop envelope and the unresolved initial-loop bound

`ReaperTimingLimits` is the single source of constants: D = 65536 samples, S = 8192 samples,
age = D + S = 73728. D is the planned stress-delay range; S is the allowed processing/block
headroom. The reduced measurement maximum (4608) is not treated as a future upper bound.
When linear K is already proven, the selected address can be checked at runtime against this
envelope in PRE's own counter. Both measured loop lengths must agree and exceed the envelope;
the ring must also exceed it. Short loops retain POST. Until L has been measured in this run,
POST plays while the first lap is measured. A BLIND trial cannot bridge this wait.

For an initial loop, the admissible candidates would be `K0 + nL` with
`frames <= E - (Cpost - K) <= D + S`, and exactly one candidate would be required. But this only
proves the occurrence if D really bounds the chain. The current VST3 process context supplies no
such bound. Neither a unique modulo candidate, the ring size nor a small observed age can exclude
an actual delay one whole lap longer. Therefore the implementation **does not grant initial-loop
admission**. It explains the missing delay proof after measuring L. This preserves INV-LC22;
it does not label the requested initial-loop feature complete.

[REAPER's VST3 interface](https://github.com/justinfrankel/reaper-sdk/blob/main/sdk/reaper_vst3_interfaces.h)
exposes host functions and parent objects. The documented
[TrackFX_GetNamedConfigParm API](https://www.reaper.fm/sdk/reascript/reascripthelp.html#TrackFX_GetNamedConfigParm)
has chain PDC observations, but no chain-bound, callback-coherent, RT-safe read path has been
established here. Polling an unrelated/stale chain value cannot grant sound. A future initial-loop
implementation needs that proof (including change invalidation and scheduling bounds) first.

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
Stage 1 analyzer; a zero error count with unclassified frames is not whole-output acceptance.
