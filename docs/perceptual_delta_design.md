# POST Perceptual Delta design contract

Perceptual Delta is a POST-only, on-demand observation page. It extends Hypha's PRE/POST comparison
without changing the product boundary: audio remains bit-transparent, the UI reports facts rather
than advice, and a difference is never fabricated from frames that describe different moments.

## First observation: Delta Sharpness History

- Quantity: signed `POST - PRE` Sharpness in acum.
- Presentation: 10 points per second, six seconds retained by the UI, fixed ±2 acum plot.
- Raw truth: the measured difference is never clipped. Only plot coordinates are bounded.
- Timing: one non-overlapping 100 ms host-presentation aperture per point.
- Host rates: common rates whose sample count represents 100 ms exactly; an incompatible rate fails
  closed instead of rounding the observation boundary.
- Exact join: schema, host sample rate, aperture samples, shared state epoch, channel mode, channel
  count, and output-presentation endpoint must all match.
- Responsiveness: no temporal display smoothing. Curve rounding is spatial only.
- Missing data: forward gaps remain gaps; a backward transport edge or definition change starts a
  new history.
- Semantics: no score, target, warning colour, recommendation, or pass/fail state.

## Channel definitions

- LR: measure input channels independently and take their arithmetic mean. Polarity cannot cancel
  the result; mono and identical dual-mono remain on the same scale.
- MID: measure the `(L+R)/2` waveform.
- SIDE: measure the `(L-R)/2` waveform. Stereo only; mono fails closed.

The explicit MID/SIDE views are observations of those waveforms. They do not redefine Record-mode
N or Sharpness, whose multichannel definition remains an independent-channel arithmetic mean.

## Execution and isolation

The existing optional analysis ingress is reused. The Audio Thread performs the same bounded
lock-free copy and metadata handoff; it never runs FFT or psychoacoustic work. Analysis is performed
on the isolated worker and file exchange remains on the IO thread.

Exactly one optional analyzer mode may be active for the lease-owning POST:

1. Meters visible: Spectrum off, Perceptual Delta off.
2. Spectrum visible: FFT on, Sharpness off.
3. Perceptual Delta visible: Sharpness on, FFT off.

Only one POST Analysis page in a DAW process may hold the process-wide kernel lease. A second open
page does not start its POST worker or request a PRE worker and reports `ANALYSIS — IN USE`. Closing
the owner or terminating the DAW releases the lease; the stable lock file is not removed, avoiding
an unlink-and-recreate race between new owners.

For Perceptual Delta, POST first publishes an uncommitted request. PRE replies with its current
presentation endpoint, and POST chooses an aperture-aligned epoch at least two apertures beyond the
newer PRE/POST endpoint. Both sides reset Phase D and the optional host-rate converter once at that
shared epoch. Those states then remain continuous across every later 100 ms aperture.

The Perceptual worker uses the established Phase D Sharpness pipeline but stops before the
independent STFT/PSB branch that this page does not display. Record continues through its existing
complete Phase D path; its values and state are not reused or rewritten by Perceptual Delta.

Closing either page disables the shared ingress and clears its display history. PRE has no public
Perceptual Delta page; it analyzes only while an exact paired POST holds a renewable request lease.
Request expiry or page close removes the PRE snapshot and stops its optional worker.

## Failure behavior

- No exact pair: show pair-unavailable state; do not start POST analysis.
- Pair warming or no matching endpoint: show sync state; do not retain a false point.
- Another POST owns Analysis: show `ANALYSIS — IN USE`; do not start either analyzer.
- Dropped input, timeline discontinuity, missed epoch, or definition edge: clear the display and
  negotiate a new future state epoch.
- Malformed, oversized, non-finite, or incompatible snapshot: silently reject it.
- Mono SIDE: reject the user action and retain the last valid channel choice.
- Worker failure: audio continues; the view becomes unavailable.

## Deferred Perceptual Delta observations

Additional observations may occupy this page only after the Sharpness contract is validated in
real sessions. Candidates include specific-loudness band delta, stereo correlation/balance facts,
or another single psychoacoustic history. They must reuse the same exact-time join, remain
on-demand and mutually exclusive, and must not add scoring or audio-path controls.
