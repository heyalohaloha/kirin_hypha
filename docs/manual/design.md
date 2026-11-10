# Design and channel scope

Hypha observes and does not advise. In normal measurement it passes the audio through unchanged
with zero added latency and reports numbers without a score or recommendation. This page covers
what the display settings smooth and what they never touch, the measurement audit, and the
channel layouts the plug-in accepts.

[Back to the README](../../README.md#the-full-manual)

## Design

Kirin Hypha is built to **observe, not to advise**.

It produces measurement data. During normal measurement, it does not generate, modify, attenuate,
or delay audio: the same input produces the same output, every time. An explicit comparison audition
is a separate output-only path; it never rewrites the input, the captured PRE/POST measurements, or
Record data. Numbers are reported as captured — no interpretation, no scoring, no recommendation.

Presentation controls in the shipping JUCE surface affect what is drawn, never what is
measured, stored, or read back.

FREQ's **AVG** chooses **LIVE** (the existing response), **0.2 S**, **1 S**, **3 S** or **10 S**.
The timed choices are symmetric exponential averages of the live drawing in dB: after one chosen
interval the curve covers about 63% of a step. Use a short time for quick changes and a long time
for the overall trend. The same setting covers LR, MID, SIDE, M/S, LOW, RAW and SHAPE. It does not
filter numeric readings, FOCUS Trail, PSB, MARK, peak hold, Record, Keep or values sent to Kirin OS.
Choose it at 125% or larger; 100% only names a non-default choice. Each project's state remembers
it, omits LIVE, and opens older states with LIVE selected. Continuity travels with each
publication: a GUI poll that skips the short recovery batch keeps AVG, while a real gap starts a
new curve even if its warming state was never drawn. PSB eases its vertical scale together with
its bars; numeric readings remain the captured values.

- **Live FREQ curves (AVG LIVE).** The absolute PRE / POST / MID / SIDE spectra rise with a 30 ms
  time constant and fall at 20 dB per 500 ms. The signed Δ curve follows its target symmetrically
  over 150 ms, so neither sign is favoured. LOW follows in both directions over 250 ms. Drawing
  advances between the 30 Hz observations on each display frame.
- **MONO's live curve.** A band with nothing to measure in the current 100 ms observation (the rest
  between two drum hits, for instance) keeps the value it was last measured at for up to one second,
  drawn faintly, so the curve does not blink between hits. After that the band breaks the line.

Watch uses the measurement engine's own Watch snapshot; LEVEL's NUM RATE can hold its numerical
reading for a chosen interval. Each metric becoming available or unavailable is shown immediately,
as are Session completion and changes to confirmed coverage. SPACE's six-second MONO field draws observations as measured.

When playback stops, or a rest outlasts the 3-second Watch window, the pages with a six-second history
(FREQ's field or landscape, LIVE, SHARP and SPACE's MONO field) keep what was measured on screen,
dimmed, and withdraw only the present: the live curve and the current values. Rests shorter than
3 seconds, such as the gaps between drum hits, keep every page live.

Everything else is the measurement itself: the playback-pass and Keep maximums, LUFS-S, the FREQ
numeric readout, the six-second field, MARK, Focus Trail, peak hold, Meter Session statistics, TIME
history, the SHARP and LIVE timelines, Record, Keep, and `plugin_data`. A value read back later is therefore the measured
one, and FREQ's live curve can differ from its unsmoothed numeric readout while it is moving.

Every metric is backed by a known-signal golden test: the expected values are derived independently from the signal definition and the ITU-R BS.1770 filter coefficients, not asserted by hand. The measurement layer demonstrates its precision rather than claiming it.

The public [BS.1770-5 / EBU R 128 v5 measurement audit](../hypha_bs1770_5_r128_v5_audit_20260831.md)
records the verified scope: BS.1770-5 Annex 1/2 for mono and stereo, all 70 assets in the EBU
Loudness Test Set v05, the source archive SHA-256, and comparison of the Hypha wrapper with the
pinned `ebur128` reference. The suite was rerun on 2026-10-06. While surround is held back, the
5.0/5.1 test assets are checked against the pinned reference only and are not evidence for a
surround mode. Hypha does not claim EBU Mode conformance and does not use the EBU logo.

## Channel scope

- **Mono and stereo:** the complete LEVEL / TIME / FREQ / SPACE, Record / Keep, Reference, local
  PRE/POST Blind and live PRE/POST compare surface is available where its normal role, license and
  platform gates allow it.
- **Surround, including exact 5.1:** not offered in this release. The measurement core keeps an
  exact 5.1 (`L, R, C, LFE, Ls, Rs`) LEVEL / TIME measurement-only mode, but the plug-in does not
  accept a 5.1 bus until its host acceptance is complete.
- **Other multichannel layouts:** Hypha refuses the layout instead of guessing channel roles or
  presenting stereo-only facts as surround measurements.

Pro Tools can still insert Hypha on a 5.1 track as a multi-mono plug-in. Each mono instance then
measures its own channel, which is not a 5.1 measurement.
