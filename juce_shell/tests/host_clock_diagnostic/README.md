# Raw host clock diagnostic (non-shipping)

This standalone project tests the **patched JUCE wrapper's host-clock facts**, not the complete
Hypha product, its content-clock admission, or a release candidate. It is intentionally absent
from the normal product build/install/release targets. A successful probe does not qualify
CHAIN ACTION, TP CROSSING, another format, or another host.

Configure with an explicit, verified `HYPHA_DIAGNOSTIC_JUCE_ROOT`, then build
`HyphaClockDiagnostic_VST3`, `KirinHyphaPdcValidationDelay_VST3`, and `HyphaClockTraceTest`.
Run CTest in the same configuration. Do not use Debug timings as production CPU evidence.

`node make-fixture.mjs /absolute/path/clock-fixture.wav` creates the fixed-seed, 24-second,
48 kHz stereo float fixture exclusively (an existing destination is refused).
`node analyze-trace.mjs fixture.wav pre.csv post.csv` reads immutable exports and emits JSON
with their hashes, raw validity flags, clock discontinuities, and exact first/last sample-bit
matches. Missing or ambiguous matches are counted separately, not interpreted as zeros or
used to infer an offset. A run means continuous observed `playing`; a seek may occur within it.
Boundary matching is a diagnostic oracle for this known fixture, not a product algorithm.

Use a dedicated disposable Song with a deterministic, low-level, stereo WAV, no instruments or
external inserts, and no changes to the user's device/routing. Insert two Clock Diagnostic
instances, first adjacent, then with the separately identified 4096-sample delay between them.
Record placement, instance IDs, binary/source hashes, sample rate, buffer setting, and GUI state.
Start each probe's one-shot observation. Compare playback, stop/resume, seeks and short loops.
Export both prefixes. Reinstantiate to start a new bounded observation; Start never resets or
overwrites existing observations. After testing, remove only these separately named diagnostic
bundles from the scan path, preserving the signed product and original Song.

Each instance holds at most 65,536 immutable rows in a preallocated array. Only the audio
producer writes; an acquire/release publication count makes exported prefixes immutable while
new callbacks append. Full means no further rows, not wraparound. No audio-path allocation,
lock, file I/O, or output modification is performed. Export runs on the message thread and is
an explicit diagnostic operation; no performance claim includes export time.

Files are explicitly exported to `~/KirinValidation/HyphaClockDiagnostic/<instance>-<count>.csv`,
outside the possibly cloud-redirected Documents directory.
They contain raw clocks, actual callback frame/channel counts, and the exact float bit patterns
of the first/last input samples on each channel. Use only the approved test WAV, not private
working audio. Sample boundary bits are observations, not a content-alignment algorithm.

`flags` bits:

| Bit | Meaning |
| --- | --- |
| 0 | Host playing |
| 1 | Host looping |
| 2 | Project position present |
| 3 | Auxiliary clock present |
| 4 / 5 | Input / output presentation latency present |
| 6 | Input boundary samples present |
| 7 | Non-realtime callback |

Auxiliary sources: 1 = VST3 continuous, 2 = AU render, 3 = AAX native. This standalone build
currently exercises VST3 only. Missing flags are missing evidence, not zero. A reported latency
of zero does not prove that the host implements meaningful per-node latency reporting.
Use known WAV boundary samples to independently test the clock/content mapping with and without
the 4096-sample delay. Equal project positions alone must not join different loop occurrences.
