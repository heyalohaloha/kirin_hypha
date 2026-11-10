# Watch mode

Watch is the real-time display: LUFS-M, LUFS-S, True Peak and crest during playback, and POST's
difference from the paired PRE. This page also covers the header buttons, the layout at 100% and
125%, the Hybrid VU and its 0 VU reference level.

[Back to the README](../../README.md#the-full-manual)

Real-time display of LUFS-M and LUFS-S, True Peak (recent), and Crest Factor during playback.
LUFS-M and LUFS-S keep independent playback-pass maximums. POST displays the
difference between its own measurements and the paired PRE.
Top-level **FREQ** shows signed POST − PRE spectrum. Under **TIME**, DRUM, SHARP and LIVE provide
their on-demand time observations. Only the visible optional analyzer runs; two POST instances may
own slots, while a third identifies the owners and waits.

Closing the GUI does not stop measurement. The audio thread continues running as long as the plugin is loaded in the DAW.

The header's **REFERENCE** button opens the Reference page and retains the existing Kirin OS
license and connection help. Its label shortens to **REF** only when the available width requires it.
Only one of TRACK, 2MIX and REFERENCE lights: REFERENCE while its page is open, otherwise the
selected measurement context. From Reference, TRACK or 2MIX selects that context and returns to
the last LEVEL, TIME, FREQ or SPACE page (LEVEL if none was visited), using the normal return to A.
Pressing the lit button does nothing.
PRE shows the same header action disabled. The measurement row and its view menu contain
**LEVEL / TIME / FREQ / SPACE**; PRE keeps unavailable FREQ disabled. POST and Δ have equal-width
buttons wherever both are shown. Reference shortcuts and saved page selection are unchanged.

At 100% and 125% the footer folds into the header's second row: the domain cycle, VU, MENU, the size
and POST / Δ share one row, and the measurement reaches the bottom edge. Status lines (a toast, a
persistent status, a running capture, or WAITING and BYPASSED when nothing else is shown) appear
in a bounded one-line strip over the bottom edge of the measurement while they last; clicking feedback
opens the full details. At 150% and above, feedback stays in the footer's left status area even
when the full text is longer than the available width. On Hybrid VU, the bottom notice and the
centred calibration legend occupy separate parts of the existing row.

100% is for reading, not operating. The buttons that take room (CURRENT / MAX, the history range
and FOCUS, LR / MID / SIDE, M/S, PSB, MARK, RAW / SHAPE, and DRUM's VIEW and BAND) are chosen at
125% and above; a choice made there stays in force at 100%, where only a non-default one (MID, SIDE,
SHAPE, MARK, HOLD, LOCK, a DRUM band) is named. Clicking the plot itself (FREQ's frequency lock, DRUM's hit selection) works at every
size. The room goes to the measurement: LEVEL reads S, I (Crest for a track or stem) and the
Session's MAX TP; TIME HISTORY draws S and TP; DRUM shows one row of history over its four values,
large; FREQ's plot takes the control rows and the right-hand absolute axis; SPACE's scatter takes
the full height with BAL and CORR beside it.

The **VU** button in the footer (in the header's second row at 100% and 125%) opens the same Hybrid VU during ordinary playback; it is not a separate
measurement mode and does not alter the selected domain. The selection survives closing and reopening
the editor while that plug-in instance remains loaded. In the Hybrid VU, **CLEAR** releases only the
per-channel held True Peak markers and Clip indicators. Live TP, the 300 ms VU needles, Meter Session
statistics and history, and Record/Keep data remain intact. A signal that is still clipping lights the
indicator again on the next 100 ms observation.

The live needles use a second-order moving-coil response on full-wave rectified input, with unity
steady gain: 300 ms to the first 99% crossing, 1.25% overswing, and about 112 ms to halfway,
identically on rise and return. Coefficients are prepared for the current sample rate. The UI
continues the same position and velocity toward rest if callbacks stop; it never substitutes a
stale published reading. Publications retain their monotonic time and processed audio horizon,
so reopening accounts for hidden time and normal callback bursts keep a steady needle. Invalid
blocks advance both needles with zero input for the entire block. This display path leaves Rust's 300 ms VU values, footer numerals,
Capture, Record and calibration unchanged. The engraved dial mapping is retained; its coordinates
are not a linear amplitude scale. These are specified model timings, not a measurement of a
particular physical meter.

Click **0 VU = −18 dBFS** below the meter to choose **−12, −14, −16, −18 or −20 dBFS**
(default −18). Both needles use the same reference, and an exact PRE/POST pair shares that
reference on this computer. The choice is saved per chain; changing pairs selects that chain's
reference. It changes the needle's displayed level only. Audio, LUFS, True Peak and the
300 ms response stay unchanged. Temporary identity contention retains the adopted reference and
disables selection until the chain is resolved. A failed save retains the previous choice and reports
the reason.
