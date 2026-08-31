# POST ATTACK transient delta design contract

**Status**: internal, default OFF until every public gate passes
**Implementation label**: B-548
**Measurement contract version**: `kirin-transient-v2-draft`

Phase 2-R is governed by `docs/transient_delta_phase2_recovery_plan_20260830.md`; the implemented Evaluator v2 foundation is recorded in `docs/transient_delta_evaluator_v2_report_20260830.md`.
The draft version cannot enter a runtime request or public payload until the fresh holdout passes.

## Product boundary

ATTACK is a POST-only, on-demand observation page. It shows where a transient observation occurred
and how the same content event changed from PRE to POST. It does not classify instruments, score a
sound, prescribe a target, suggest a setting, or process audio.

The page has two explicit measurement profiles: `DRUM` for percussion-dominant full-kit buses and
isolated kick or snare tracks, and `2MIX` for completed mixes. Full-kit material determines DRUM's
shared detector parameters; isolated tracks are an in-domain regression subset, not extra profiles
or separately tuned modes. Only one profile may run at a time, and the runtime never infers or
switches profile from the material.
The first ATTACK view has no selected profile and emits no analysis request until the user chooses
one. The selection lasts only for the current editor lifetime.

The main mark is a signed `POST - PRE` Onset Flux event stem on a six-second observation field.
Event detail contains PRE, POST, and signed delta for Onset Flux, 30 ms Crest, 30 ms Sample Peak,
and the existing 100 ms Sharpness definition. A field that was not measured remains undefined; it
is never replaced with zero.

## Ownership and isolation

- ATTACK owns one of the existing two process-wide Analysis leases.
- FREQ, SHARP, LIVE, and ATTACK are mutually exclusive inside one POST instance.
- Switching among Analysis pages retains the lease. METERS and editor close release it.
- Switching between DRUM and 2MIX retains the lease but retires the request, clears history and
  selection, and starts a new definition epoch.
- ATTACK uses a new top-level `PostAnalysisRoute`; the existing wire-level `AnalysisViewMode`
  values `0`, `1`, and `2` and their decoder remain unchanged.
- Transient request, readiness, payload, cleanup, file namespace, and Windows mapping are separate
  from the existing Analysis exchange layouts.
- PRE has no ATTACK page and starts its transient worker only for an exact paired POST request with
  the same profile and definition hash.
- An unpaired POST does not request PRE work. Confirmed PRE OFF shows
  `PRE OFF - ATTACK paused` and never falls back to an absolute value.
- Audio Thread work remains the current bounded lock-free sample copy and notification. FFT, Mel,
  ODF, event matching, allocation, locks, I/O, and waiting stay off the Audio Thread.

## B-546 baseline analysis layout

Layout is derived from the host sample rate without resampling audio:

- reference rate: 48,000 Hz;
- reference window: 1,024 samples (`21.333333 ms`);
- reference hop: 256 samples (`5.333333 ms`);
- host window and hop: nearest integer to the same rational duration, ties away from zero;
- FFT size: the smallest power of two not smaller than the host window;
- window: periodic Hann `0.5 - 0.5*cos(2*pi*n/N)`;
- power compensation: divide squared FFT magnitude by `sum(window[n]^2)`;
- DC is retained in metadata diagnostics but excluded from the Mel bank;
- Mel range: 30 Hz to `min(20,000 Hz, Nyquist)`;
- epsilon before logarithm: `1e-12` compensated power;
- logarithm: `10*log10(power)`;
- spectral lag: one hop.

This layout remains the frozen B-546 baseline rather than the selected public definition.
The versioned definition hash includes every item above plus filterbank formula, realized centers,
band count, channel mode, peak rule, threshold, floor, matching tolerance, refractory interval, and
event-window rules.
PRE and POST are never joined when hashes differ.

## Candidate ODFs and Phase 2-R selection

The B-546 Mel 32, Mel 40, complex, and hybrid candidates remain diagnostic baselines.
Their old threshold sweep is not the recovery path because the evaluator contract was not stable.

Evaluator v2 first reruns the frozen Mel 32 baseline as a DRUM diagnostic.
DRUM compares a Mel 32 front end under the v2 common rule with a fixed-scale SuperFlux-style ODF.
Only DRUM may add fixed low/broad/high late fusion for a remaining kick-only or hat-only failure.
2MIX uses the fixed-scale SuperFlux-style ODF as its primary candidate because its frequency-neighbor
maximum targets false onsets from vibrato and sustained material.

The B-546 Mel baseline is:

`ODF(t) = mean_b max(0, log_power[b,t] - log_power[b,t-1])`

The SuperFlux-style candidate is:

`S(t) = mean_b max(0, L(b,t) - max_j L(j,t-mu))`

where `j` covers the same log-frequency band and a fixed number of immediate neighbors.
LR transforms L and R independently, averages compensated linear power per FFT bin, and only then
applies the selected bank and logarithm.
MID analyzes `(L+R)/2`; SIDE analyzes `(L-R)/2`; mono SIDE is invalid.
No candidate uses track maximum, running maximum, percentile normalization, adaptive whitening,
auto scale, or material-dependent gain.

Phase 2-R fixes a candidate, fixed amplitude reference, absolute ODF floor, common fixed local mean
plus fixed offset, local-maximum widths, 30 ms event separation, ±25 ms evaluation tolerance, and
public full-scale independently for each profile.
Values from different profiles are not presented as numerically comparable.
Each profile remains absent from the public selector until its own fresh holdout passes.
The ATTACK route remains absent from public navigation until at least one profile passes.

## Common event decision

PRE and POST publish continuous exact ODF frames; neither side performs an independent peak pick.
After an exact-time join, POST forms `max(pre_odf, post_odf)` and applies one common fixed peak rule.
Its moving mean is computed only from the joined common trace and never rescales the published PRE,
POST, or delta values.
Around each common candidate it finds bounded local maxima independently in PRE and POST and applies
one-to-one matching with deterministic earliest-time then largest-value tie-breaking.

Matched events retain both exact endpoints and expose the signed difference. A single-sided event
is retained as PRE ONLY or POST ONLY without a delta. Ambiguous dense events are not promoted to a
delta.

ODF correlation is diagnostic-only. It may never shift, interpolate, or authorize content-time
alignment.

## Exact content-time authority

Every frame carries request ID, definition hash, host sample rate, window samples, hop samples, FFT
size, channel mode/count, exact content support start/end, presentation latency and source, state
epoch, worker generation, exchange generation, and transport run.

A delta requires both producers to prove the same content support under one definition. Pair name,
wall clock, host endpoint alone, curve similarity, and ODF correlation are insufficient. A missing
or changed timing field fails closed and starts a new alignment arm; no consumer-side shift or
interpolation is allowed.

## Event details

- `event_sample` is the center of the selected zero-padded ODF frame under the fixed Phase 2-R grid;
  candidate, track, and session timing offsets or onset backtracking are not applied.
- Crest is `sample_peak_dBFS - RMS_dBFS` over
  `[event_sample, event_sample + round(0.030*sample_rate))`.
- Sample Peak is the maximum absolute sample over that same 30 ms window and is not True Peak.
- Sharpness is the existing continuous 100 ms definition. It is attached only when its exact
  content support contains the event and the final Phase 2 endpoint-error bound is met.
- The onset mark can publish before detail. Crest/Peak and Sharpness amend the same immutable event
  identity when their complete windows arrive.

## History and discontinuities

Each slot uses fixed-capacity ingress, ODF history, pending detail, and event history. No steady
state product path grows a collection or allocates after worker preparation.

Exact host content samples remain measurement truth. A separate monotonic observation position
places events in the six-second view. Backward locate/loop creates a new run but retains displayed
events and explicit selection until the user clears them. Pair, sample rate, channel definition,
analysis definition, or latency-contract changes clear incompatible history and selection.

Verified silence is a valid no-event frame. Missing observation is a gap and never becomes silence
or event zero. Short gaps retain prior facts and explicit gap metadata. Long gaps, worker panic,
exchange replacement, or real overflow start a new continuity epoch without joining across it.

## Presentation and accessibility

- Positive stems use ice cyan `#75D6E8`; negative stems use amethyst `#A695D6`.
- Positive endpoints are round; negative endpoints are diamond-shaped.
- PRE ONLY is a neutral hollow circle; POST ONLY is a neutral hollow diamond.
- Raw values are not clipped. Only fixed plot coordinates are bounded.
- Hover readout and click lock are measurement interaction and remain available when explanatory
  hover help is disabled.
- Left/Right and Home/End move event focus. Enter/Space locks it. Escape and an accessible Clear
  control release it.
- Accessibility announcements occur only when focus or selection changes, never at repaint rate.
- Public UI copy is ASCII. Undefined values use `--`.

## Publication gates

Each profile remains internal and default OFF until all of these hold on one commit:

- its fresh holdout onset precision, recall, timing, and false-match limits fixed by the Phase 2-R
  recovery plan are met, with DRUM also passing kick/hat and isolated-hat gates;
- its real-performance PRE/POST transform pairs pass through one common decision trace;
- identity and fixed-gain properties pass, and lookahead/fixed delay never create a false delta;
- 48 kHz marker latency is P95 at most 50 ms and maximum at most 75 ms;
- two slots at 192 kHz have zero ingress drops and do not increase Audio Thread work;
- METERS, FREQ, SHARP, and LIVE match the Phase 0 source and render baseline;
- macOS and Windows Studio One checks pass at every supported scale, including keyboard,
  VoiceOver/Narrator, PRE OFF, loop, gap, panic/restart, and offline bounce.

If any gate fails, route, request, worker, and lease acquisition remain disabled together. Removing
the dedicated ATTACK modules restores the four existing pages without rewriting an old enum, ABI,
transport schema, or saved DAW state.
