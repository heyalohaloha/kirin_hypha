# PRE/POST Blind completion and acceptance

Date: 2026-09-13
Baseline: B-840 / `95d49b829bc8018826fde00357c400d2235faaf3`
Status: playback fixes and local product acceptance pass; platform CI and current-candidate DAW acceptance remain separate gates. This is not a release-completion claim.

## Scope

Complete the existing four-second, local PRE/POST preference listening trial as a primary Hypha
feature. Preserve explicit PRE selection, 2MIX and TRACK/STEM Gain Match policies, normal-path
transparency, zero reported latency, immutable capture, single audition ownership, and the two
Analysis slots. Reference Blind is a separate product flow.

VST3 and AU have historical Studio Pro capture/PDC evidence. AAX remains disabled until its own
native-clock and PDC acceptance passes. A build, a synthetic host, or a passing unit test cannot
replace that evidence. Public distribution is a separate three-channel release gate.

## Reproduced completion defects

| ID | Baseline behavior | Required behavior |
| --- | --- | --- |
| BL-C01 | First playback callback must start at the exact captured sample. The UI only displays milliseconds. Starting before the range fails. | The user can start before the range; only the exact sample intersection is replaced. No guessed alignment or partial first pass. |
| BL-C02 | A callback crossing the four-second end invalidates the trial. At 48 kHz and 2048 frames this prevents completing a pass. | Render the exact suffix/prefix intersection and preserve live samples outside it. Accept normal DAW callback sizes. |
| BL-C03 | Leaving the range or stopping after a completed first pass prevents completing the second side without an exact loop. | Keep completed-pass evidence and permit an explicitly selected next source to await the same range. An interrupted incomplete pass still fails. |
| BL-C04 | The audition renderer ignores the captured clock source and optional PDC notifications. | Freeze the admission observations and invalidate if their source, availability, or values change before/during playback. Never derive a compensation offset from them. |
| BL-C05 | An offline or bypass callback can acknowledge normal return and release a pending realtime attenuation hold. | Only a nonempty, non-bypassed realtime callback acknowledges the explicit normal-return command. |
| BL-C06 | Range edges and source changes replace the waveform abruptly. | Bound source and range transitions to five milliseconds, preserve fixed gains and equal-PCM controls, and produce identical output across callback partitions. |
| BL-C07 | Equal-width buttons clip answer labels and the explicit POST attenuation at small sizes. Repeated diagnostic PNG output appends to the old image. | Allocate width from actual font metrics, use unambiguous compact labels, test every visible action, and overwrite each preview. |

The original failures were reproduced directly against the baseline renderer before editing it.
The regression matrix uses untrimmed host callbacks at 44.1, 48 and 96 kHz; 64, 257, 512 and 2048
frames; mono and stereo. It checks every output sample, two complete sides, answer, reveal and
normal return. Capture/PDC proof and playback-boundary proof remain separate.

## Current acceptance ledger

| Boundary | State | Evidence needed to close |
| --- | --- | --- |
| Native playback range, explicit next pass, full-pass answer gate | Pass | 24 rate/block/channel configurations, both complete sides, and unchanged A samples outside the range |
| Clock/PDC change and normal-return acknowledgement | Pass | Twelve source/latency change cases, offline/bypass return refusal and real product wiring |
| Actual product UI at all five sizes | Native presentation contract passes | All nine product phases fit at 300, 375, 450, 600 and 900 pixels; complete processor/editor flow passes for stereo 2MIX and mono TRACK/STEM |
| Preparation, capture service, lease and PCM cleanup | Targeted tests pass | All 17 selected native contracts pass (233.07 seconds, including process startup) |
| Source and range transitions; real-audio Gain Match | Targeted tests pass | Five-millisecond transition is partition-invariant; maximum constant-signal step is 0.00208336 (normal) or 0.00104171 (approved attenuation). S-1 matched gain tolerance is 0.002 dB. Complete processor/editor flow passes for stereo 2MIX and mono TRACK/STEM |
| macOS VST3 and AU host acceptance | Pending | Current candidate product round trip, stopped/reopened editor and mix synchronization |
| Windows VST3 host acceptance | Pending | Same candidate and conditions on the validation machine |
| AAX native clock/PDC | Pending; product disabled | Known 4096-sample delay, exact PRE/POST capture, native hashes and residual zero on each supported host |
| Final source gate / Clippy / CI | Rust libraries and Clippy pass; native/CI running | 1,638 library tests pass, nine existing slow tests remain ignored in this command; full Clippy has no owned-source diagnostics. Required PR gates remain authoritative |

## Host coordination

Work is isolated in `codex/hypha-blind-completion` so concurrent Pro Tools diagnostics do not
consume modified source. The main checkout remains unchanged. Pro Tools and the Windows audio
hardware were already in active validation elsewhere when this work started; their sessions and
audio routes must not be replaced concurrently.

The opt-in PDC validation effect now has a Native-only AAX target when the external licensed SDK is
enabled. It is a separate diagnostic identity, reports and implements 4096 samples, and is absent
from normal build/install/release targets. This does not enable the AAX Blind product gate.

## Product flow evidence

`KirinLocalBlindProductTests` hosts the real common PRE/POST processor and Rust C ABI with an
independent audio producer and JUCE message loop. It selects the discovered exact PRE, captures
192,000 frames at 48 kHz through the real request/PCM transport, uses the actual editor controls,
completes both passes, answers, reveals, ends, reopens at 300 pixels and explicitly returns.
The fixture is checked-in S-1, with a known POST gain of -0.5 (gain plus polarity inversion) so
leaving live POST in place cannot falsely pass the assignment/output check. The TRACK/STEM case
uses mono and only one second of signal in the four-second fixture. Neither case changes the
user's stored project, audio device, DAW routing or selected real-host session.

Both Release cases passed (31.93 seconds together). Actual output covers 384,000 audition frames,
the hidden/revealed assignment agrees with PCM polarity, matched-copy maximum error is below
0.00004, and the normal PRE/POST path is bit identical. The stereo observed error was 0.0000115335.
These tests prove product wiring and recovery, not a DAW's clock/PDC implementation.

The isolated renderer benchmark at 48 kHz stereo reported p99 callback times of 0.659, 1.683 and
32.281 microseconds for 64, 256 and 2048 frames, respectively (0.0494%, 0.0316%, 0.0757% of the
audio callback interval). This measures the audition renderer on this Intel Mac, not the full
processor or every supported host. The 64/256-frame cases retain exactly 3,072,000 bytes of
frozen PRE/POST PCM. Runtime contracts also record zero audio-thread allocation/deallocation.
