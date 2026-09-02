# POST absolute observation timeline design contract

The LIVE page is a POST-only, on-demand observation timeline. It adds no PRE subtraction, score,
target, warning state, or audio-path control. Its purpose is to keep three current measured facts
visible on one time axis while a plug-in is adjusted.

## Displayed facts

| Fact | Definition | Colour | Fixed display scale |
|---|---|---|---|
| LUFS-M | Watch-compatible Momentary loudness | Cyan | −42..0 LUFS |
| True Peak | Watch-compatible recent 400 ms peak | Pale violet | −30..+6 dBTP |
| Sharpness | Independent-channel arithmetic mean | Amber | 0..3 acum |

The scales are independent. Vertical coincidence between different traces is not numerical
equivalence. Raw measured values are not clipped; only their plot coordinates are bounded. If a
sparse source places LUFS-M or recent True Peak below the measurement floor, the exact frame retains
`None` and its numeric readout remains unavailable. The curve presents that aperture at the fixed
scale's lower edge so a factual silent interval is not mistaken for a failed paint.

## Time contract

- One exact, non-overlapping 100 ms host-presentation aperture produces one joined frame.
- LUFS-M, recent True Peak, and Sharpness in a frame share the same
  `presentation_end_samples`, state epoch, generation, sample rate, and channel count.
- Measurement publishes at 10 Hz. Rust retains 64 frames. UI paints the newest six seconds at
  5 Hz and updates the current numeric readouts at 2 Hz.
- Curve segments join measured points directly. There is no temporal smoothing, averaging,
  animation extrapolation, or missing-value substitution in measurement state. A below-floor
  LUFS-M / True Peak aperture uses only the documented presentation-floor coordinate.
- A short forward gap retains the surrounding exact points and their true sample-time positions.
  The presentation stroke joins those measured points without inserting a frame or value. One
  verified frame is presented as a dot instead of an empty plot.
- Backward transport, a sample-rate/aperture/channel-layout change, or a forward gap spanning the
  six-second field clears the old run before a new one begins. A forward generation/state-epoch
  edge alone does not erase compatible verified history.

LUFS-M and recent True Peak use a private display instance of the established `MeasureEngine`.
Sharpness uses the established continuous Phase D analyzer. These display states do not replace or
rewrite Watch, Record, TRACE, or `plugin_data` state.

## Isolation and lifecycle

LIVE uses the existing bounded optional-analysis ingress. The Audio Thread performs only the same
lock-free sample copy and metadata notification as FREQ and SHARP. All measurement runs on the
isolated analysis worker.

LIVE is local to POST. It never publishes an Analysis request to PRE and never starts a PRE worker.
Returning to METERS or closing the editor disables its worker and clears its retained display
timeline. Switching mode disables the prior analyzer before enabling the next one while retaining
the same Analysis slot.

The DAW process provides two stable kernel-backed Analysis slots across FREQ, SHARP, and LIVE. A
third page stays idle and displays:

```text
Both slots in use — Mix, Vocal
```

The two names come from the slots observed locked during that acquisition attempt. If both names
cannot be verified, the fallback is `Both slots in use`; the UI never guesses names or commands the
user to close a page that may be off screen.

When either active owner returns to METERS or closes, the waiting page may acquire the released slot
automatically. FREQ / SHARP / LIVE changes retain the owner's slot. The two-slot bound affects only
optional Analysis displays; it does not limit loaded pairs, Watch, Record, or audio pass-through.

## Failure behavior

- No retained exact frame yet: show the neutral warming state.
- Both slots occupied: show the one-line factual owner status (or its name-free fallback); do not
  analyze.
- Invalid, non-finite, stale, duplicate, or incompatible frame: reject it without altering audio.
- Worker failure: audio continues and the view becomes unavailable.
- Rendering delay: the next batch recovers every retained exact frame; it does not invent points.
- A transient warming response after verified data retains the last exact field until the next
  frame; explicit unavailable or slot-in-use states replace it with their status message.
