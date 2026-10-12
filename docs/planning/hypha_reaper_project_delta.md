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

This certificate is not `certifiedContent`: project positions repeat in loops, whereas the existing
Studio content clock is continuous. No fallback is added to `ContinuousClock`, and the certificate
cannot enter `initialLoopCandidate`'s content-clock branch. Missing VST3 continuous samples continue
to prevent PRE output, MATCH and BLIND. The bilingual notice now describes the missing verified audio
clock, not output latency as the sole cause.

A local `pluginFrames` counter with per-side gap detection could calibrate an index offset during
linear playback under compensated-position assumptions. It cannot identify an initial loop's
occurrence: offsets differing by one loop give the same project positions, and counter origins
can differ. A linear proof could be retained into a loop, but INV-LC5 also requires an actual host
bound for the interval between an audio latency change and a detectable clock change. No such bound
is established for REAPER by periodic meter screenshots. LISTEN and BLIND therefore remain deferred.
Neither a wall-clock age nor audio correlation is promoted to sample/occurrence authority.

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
its position changes or playback is restarted. No REAPER-specific duration is claimed.

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
- Audition refusal: the project certificate supplies no valid continuous clock or initial-loop proof.

These tests do not certify real-host timing transitions or rejection of an undetectable lie in a
certified host's project clock. Real-host acceptance must check those assumptions separately.
