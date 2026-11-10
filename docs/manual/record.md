# Record mode and Kirin OS

With a Kirin OS license, Keep records a playback's measurements and STOP writes them to the
`.kirin` record. Session data goes to `plugin_data` as JSON. Without Kirin OS, Watch and the local
PRE/POST Blind Compare still work.

[Back to the README](../../README.md#the-full-manual)

## Record mode (Kirin OS required)

With a Kirin OS license, POST can keep a session record.

1. Choose **MENU → Keep → Keep selected pair** (or **All Keep**) to begin a session recording.
2. Press **STOP** in the footer, or choose **MENU → Stop selected pair**, to end the session. The
   session is written to the `.kirin` record.

Integrated Loudness and session-maximum True Peak accumulate across DAW transport stops while the
same Keep remains active. If the DAW loops during Keep, the Keep continues: the record's timeline
holds the first continuous pass, and later passes count toward the Keep summary (Integrated, LRA,
Max TP and PLR) and the live display. During an offline bounce/export, POST auto-runs the same
cleanup as **Stop** when the host reports that offline processing has ended. If a host does not
emit that offline-end edge, Keep remains armed until manual **Stop** or the idle auto-stop
backstop after 10 minutes without Active signal.

During Keep, intermediate I/LRA readouts reuse an exact-energy cache with a bounded auxiliary
node budget. This avoids rescanning the complete recording after every small input block.
Stopping Keep still drains the input and uses the original canonical finalization for the saved
Record; True Peak and the measured time range retain their original definitions.

After Stop, the final Record display remains visible until the next playback produces its first
newly computed Watch result; that result returns the grid to Watch without showing stale Watch data
in between. Multiple pairs record independently.

If measurement samples are ever dropped during a recording (for example, on a buffer overflow), the dropped-sample count and an integrity flag are written into the session data. Incomplete measurement is recorded as incomplete, never presented as complete.

## Kirin OS ecosystem

Kirin Hypha is one piece of a larger ecosystem. With Kirin OS, session data is written to `plugin_data` in a structured JSON schema and can be bundled with C2PA provenance into a tamper-evident `.kirin` file alongside the audio.

Hypha itself remains **standalone and free** — Kirin OS is not required to use Watch mode or local
PRE/POST Blind Compare.

Kirin OS is available now. More at [kirinmastering.com](https://kirinmastering.com).
