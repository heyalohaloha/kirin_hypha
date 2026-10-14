# REAPER VST3 project-coordinate differences

REAPER's compensated project positions identify the input content for FREQ, SHARP and PSB
measurement across all versions. The native executable names `REAPER` and `reaper`, with a non-empty
version string starting with an ASCII digit, select a measurement-only VST3 certificate. Missing or
unreadable versions, other executable names and other formats receive no REAPER certificate.
FREQ already uses compensated project positions across REAPER versions; extending SHARP and PSB to
the same range introduces no additional timing assumption. Other hosts keep their previous policy.
The Windows code path shares the policy; Windows host acceptance remains separate.

Real-host checks used REAPER 7.71 (native macOS bundle version `7.71.0_ba637e1u`): FREQ, SHARP and
PSB differences were zero during normal playback, loops and latency changes during playback.
The accepted range is a version policy, not a claim of testing every version.

The common processor sends original A scratch through the existing deferred content-analysis
transaction. The project sample is the join coordinate (K = 0); output latency is never added.
A wrap, position move, changed clock definition, stopped/bypassed/offline callback or callback gap
retires the old aperture. The existing exchange still checks clock kind, selected PRE lifetime,
request generation and whole matching windows. AU, AAX and Studio content clocks keep their existing
pair observers. Record, saved state, audio output and reported plug-in latency are unchanged.

## Audition boundary

The certificate remains distinct from Studio's `certifiedContent` clock. LISTEN in 1.1.51 uses
an explicitly selected `reaperProcessing` counter, with an independent origin on each side.
Eight agreeing compensated linear joins establish K; project positions alone never select an
initial loop occurrence. Measurement uses its unchanged project-coordinate ingress.
See [REAPER LISTEN timing](hypha_reaper_listen_timing.md) for the bounded continuation, callback
evidence, initial-loop hold and reduced real-host timing measurements. Windows acceptance is
separate. AU/AAX and other VST3 hosts do not receive this counter.

## Detectable and undetectable changes

Changed sample positions or latency notifications invalidate the old analysis window immediately;
new complete windows may then resume. Missing positions, non-realtime processing and callback gaps
also invalidate it. A position reset does not modify A or restore an audition selection.

An uncompensated host that sends the same clock reports as a compensated host is indistinguishable
from clocks alone. In particular, stable equal project positions with unreported delayed PCM cannot
be rejected by a general clock-only test while also accepting a genuine zero-delay chain. This is
a limit of the requested host certificate, not an independent proof of compensation. Unknown hosts
are not given this certificate; their existing FREQ project fallback is intentionally unchanged.
For the certified host, an audio-only delay change can briefly produce a false difference until
its position changes or playback is restarted. The reduced reported-latency measurements and the unreported negative are recorded in the LISTEN timing contract; the negative is not a finite detection guarantee.

[The VST3 ProcessContext specification](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html)
makes continuous sample validity explicit; it does not certify a host's PDC/project relationship.
The project-coordinate rule here is a host policy based on the reported compensated-position
observation, not a guarantee derived from the generic VST3 API.

## Studio version policy

The content certificate accepts `Studio One` 6–7 (minimum 6.0.0) and `Studio Pro` 8. Native numeric
`major.minor.patch[.build]` and `major.minor.patch Build build` strings are accepted, malformed or
missing values are refused. `studioContentMinimum` is the single adjustable lower bound.
Only Studio Pro `8.1.2 Build 113407` / `8.1.2.113407` is marked measured. The Windows Debug-only
`HyphaChainClockPolicy.h` diagnostic remains exact-build-only.

Separate real-host measurement checks used Studio One 6.6.3 / 7.2.3 and Studio Pro 8.0.2 / 8.1.2:
FREQ, SHARP and PSB differences were zero with a 4096-sample delay and loops. These checks do not
extend the runtime `measured` marker or claim testing every accepted version. Studio One 5 remains
outside this policy; its `studioapp` executable name is not added.

## Regression coverage

- Host policy: Studio's 6.0 floor and native version spellings, all readable REAPER versions,
  family/name boundaries, missing or unreadable versions, measured distinction and wrong wrappers.
- Portable REAPER clock: 96 kHz, delayed PCM, initial loops, wraps, position/latency changes and missing proof.
- FFI/workers/exchange: no auxiliary or presentation latency, physical 4096-sample delay, zero delay,
  delay switches, changing content across loop occurrences and immediate proof revocation.
- Common processor: mono/stereo REAPER fixtures plus existing AU, Studio content and AAX suites.
  Native shared-memory support is required; compiling these fixtures is not an execution result.
- Audition: separate processing-counter provenance, linear PRE/MATCH/BLIND and bounded known-K loops; no initial-loop permission from a modulo candidate alone.

These tests do not certify real-host timing transitions or rejection of an undetectable lie in a
certified host's project clock. Real-host acceptance must check those assumptions separately.
