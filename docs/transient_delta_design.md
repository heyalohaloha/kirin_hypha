# POST ATTACK transient delta design contract

**Status**: internal, default OFF until every public gate passes
**Implementation label**: B-546
**Measurement contract version**: `kirin-transient-v1`

## Product boundary

ATTACK is a POST-only, on-demand observation page. It shows where a transient observation occurred
and how the same content event changed from PRE to POST. It does not classify instruments, score a
sound, prescribe a target, suggest a setting, or process audio.

The main mark is a signed `POST - PRE` Onset Flux event stem on a six-second observation field.
Event detail contains PRE, POST, and signed delta for Onset Flux, 30 ms Crest, 30 ms Sample Peak,
and the existing 100 ms Sharpness definition. A field that was not measured remains undefined; it
is never replaced with zero.

## Ownership and isolation

- ATTACK owns one of the existing two process-wide Analysis leases.
- FREQ, SHARP, LIVE, and ATTACK are mutually exclusive inside one POST instance.
- Switching among Analysis pages retains the lease. METERS and editor close release it.
- ATTACK uses a new top-level `PostAnalysisRoute`; the existing wire-level `AnalysisViewMode`
  values `0`, `1`, and `2` and their decoder remain unchanged.
- Transient request, readiness, payload, cleanup, file namespace, and Windows mapping are separate
  from the existing Analysis exchange layouts.
- PRE has no ATTACK page and starts its transient worker only for an exact paired POST request.
- An unpaired POST does not request PRE work. Confirmed PRE OFF shows
  `PRE OFF - ATTACK paused` and never falls back to an absolute value.
- Audio Thread work remains the current bounded lock-free sample copy and notification. FFT, Mel,
  ODF, event matching, allocation, locks, I/O, and waiting stay off the Audio Thread.

## Exact analysis layout

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

The versioned definition hash includes every item above plus Mel scale formula, band count, channel
mode, peak rule, threshold, floor, matching tolerance, refractory interval, and event-window rules.
PRE and POST are never joined when hashes differ.

## Candidate ODFs and Phase 2 selection

Phase 2 evaluates the following fixed candidates without session-relative normalization:

1. `mel32`: 32 triangular Mel power bands and positive log-power flux.
2. `mel40`: 40 triangular Mel power bands and positive log-power flux.
3. `complex`: rectified complex-domain prediction error on the same FFT frames.
4. `hybrid`: a fixed weighted sum of the winning Mel candidate and complex candidate.

For a Mel candidate:

`ODF(t) = mean_b max(0, log_power[b,t] - log_power[b,t-1])`

LR transforms L and R independently, averages compensated linear power per FFT bin, and only then
applies the Mel bank and logarithm. MID analyzes `(L+R)/2`; SIDE analyzes `(L-R)/2`; mono SIDE is
invalid. No candidate uses track maximum, running maximum, percentile normalization, auto scale,
or material-dependent gain.

Phase 2 fixes one candidate, absolute ODF floor, common-event threshold, local-maximum radius,
refractory interval, PRE/POST event tolerance, and public full-scale. Until the independent holdout
passes, the public unit and navigation remain unset and the capability remains OFF.

## Common event decision

PRE and POST publish continuous exact ODF frames; neither side performs an independent adaptive
peak pick. After an exact-time join, POST forms `max(pre_odf, post_odf)` and applies one fixed event
rule. Around each common candidate it finds bounded local maxima independently in PRE and POST and
applies one-to-one matching with deterministic earliest-time then largest-value tie-breaking.

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

- `event_sample` is the earliest sample in the winning ODF frame support that is selected by the
  fixed onset backtrack rule.
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

The route remains internal and default OFF until all of these hold on one commit:

- independent holdout onset precision, recall, timing, kick miss, hat false-positive, and
  false-match limits fixed by the Phase 2 report are met;
- identity and fixed-gain properties pass, and lookahead/fixed delay never create a false delta;
- 48 kHz marker latency is P95 at most 50 ms and maximum at most 75 ms;
- two slots at 192 kHz have zero ingress drops and do not increase Audio Thread work;
- METERS, FREQ, SHARP, and LIVE match the Phase 0 source and render baseline;
- macOS and Windows Studio One checks pass at every supported scale, including keyboard,
  VoiceOver/Narrator, PRE OFF, loop, gap, panic/restart, and offline bounce.

If any gate fails, route, request, worker, and lease acquisition remain disabled together. Removing
the dedicated ATTACK modules restores the four existing pages without rewriting an old enum, ABI,
transport schema, or saved DAW state.
