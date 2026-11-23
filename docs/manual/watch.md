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
The True Peak rails in both VU and LU extend to **+3 dBTP**. Above 0 dBTP, the live bar and held
marker change to the Clip lamp's colour family; this does not change the sample-clip lamp's condition.
Values above +3 stop at the end of the rail.

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

### The LU face: loudness against a Reference song

With a Kirin OS license, while POST can read Kirin OS's Reference library, the Hybrid VU has a
second face. **LU** beside the VU button switches to it and back; the control is not shown when the
library cannot be read, and the VU is then exactly as before. The choice and its window are saved
with the DAW project. A project reopened where Reference cannot be read shows the VU and keeps the
choice for the day it can be read again.

The box, the True Peak rails, the Clip indicators and the three value cells stay as on the VU. The
left dial reads M or S, and the right dial always reads I (the Meter Session so far). Both use
the same LU scale from −18 to +9 (the EBU +9 scale); the song defining 0 is named below the dials.
There is no numeric target: the 0 comes from Reference, in three ways.

- **The song is the 0.** 0 LU is the loudness of the Reference song chosen in B or C: its Cue's
  Integrated loudness from Kirin OS, or the whole song's when the Cue has no value. The face names
  it, for example **0 = North Road** and **−9.8 LUFS**. When B and C both hold a song, the one
  that is heard decides, then the page in view. The value is Kirin OS's own; the audition's MATCH gain
  is never added. A mono song on a stereo track plays 3.01 LU above its value: the needles use the
  played level and the face adds **MONO +3.0**. A measured target dropped on B is not a Kirin OS
  song and is never the 0. With no song chosen the needle rests on the left stop, the readout shows
  **---** and the face says **CHOOSE A SONG IN B OR C**; a song Kirin OS has not measured says
  **NOT MEASURED IN KIRIN OS**.
- **The set's range is a band.** The songs of the chosen Reference set that have a Cue loudness
  from Kirin OS give a band on the **right I scale only** from their p10 to their p90, as B's TARGET RANGE does for
  the spectrum. It takes two songs with a value; otherwise there is no band. It sits behind the
  needles and the figures.
- **The song at the same place is a needle.** While **FOLLOW A** places the song at A's position
  in the DAW, a cyan needle (**REF**) shows the song's own loudness at that position, the offset
  included: Kirin OS's LUFS-M for the M window and LUFS-S for the S window. Where Kirin OS has no
  value at that position (outside the measured part, or before or after the song) the needle is not
  drawn and the face says **REF: NO VALUE HERE**. It is not drawn in LOOP, during a Blind, or for
  the I window, whose 0 is already the song's Integrated loudness.

The control beside LU chooses the left window: **M** (Momentary, 0.4 s) or **S** (Short-term, 3 s).
The right window stays **I**: POST shows its own Integrated loudness, and PRE shows its own.
There is no PRE comparison needle on POST's right dial. Older projects that selected I for the left
dial reopen on M. The needle points at the value itself, not through
the VU's 300 ms response; between two publications it only eases over 60 ms so that it does not
step. The readout under the dial repeats the window and the value in LU (**M −2.1**). A value that
does not exist rests the needle on the left stop and reads **---**; values beyond the scale rest
on its ends while the readout keeps the figure.

In POST, a thinner needle in PRE's colour shows the PRE of the same moment, and the face says the
difference, for example **POST − PRE +1.2 LU**. It appears only where POST confirmed a PRE of the
same moment (M and S); it is never estimated, and I has none. PRE has no Reference page: PRE's LU
face reads the 0 and the band that the POST of the same chain shares on this computer, shows PRE's
own needle, and offers LU once that POST has read Reference. Without a song it says **CHOOSE A SONG
IN POST REFERENCE**. The REF needle belongs to POST's window.

All of this is display only. Audio, the measurements, Record and Keep, Reference's audition and
MATCH are unchanged.

### PSR, CREST, and SHARP needle faces

When Kirin OS Reference is readable, the face selector also offers **PSR**, **CREST**, and, in
POST, **SHARP**. PRE uses a single central dial. POST uses two dials, PRE on the left and POST
on the right. The left reading is available only when PRE and POST have a valid paired observation.
PSR and CREST span −24 to +24 dB; SHARP spans −1.5 to +1.5 acum. Zero is the median of the
selected Reference Cue, not an ideal value or a target score.

These faces measure Reference's audio in Hypha, using the same measurement engines and windows
as the live values. They do not substitute Kirin OS's differently defined PSR, crest, or sharpness
summaries. Reference is measured at the host's supported sample rate and mono/stereo format,
without MATCH gain. The native reader is separate from audition playback.

The own needle shows the current value minus that zero. POST can add a PRE needle only when the
same-time paired observation is valid. On PSR and CREST, both needles then show the newest 100 ms
boundary that PRE and POST both published, so they always belong to the same moment and stay through
ordinary publication delay; PRE goes away when no such boundary is within one second. Its REF needle
requires FOLLOW A and a measured value at that position; missing data is not drawn as zero. The set
band is the p10–p90 range of the songs' Cue medians, with each song counted once. It stays absent
until every member has completed the same-definition measurement. A failed, missing, or
still-preparing song is not silently dropped from the set.

**PREPARING** means the Reference baseline is being prepared. **UNAVAILABLE** means that no
usable baseline is available; when the REFERENCE page says the source cannot be used, the face says
**UNAVAILABLE** with the reason, in POST and in the PRE of its chain, and returns to **PREPARING**
once the source can be used. Changing a face never changes audio, Record, Keep, or MATCH.
PRE measures its own PSR and CREST and uses the Reference selected by POST in the same chain.
Opening a PRE metric face can request that measurement without opening POST's window or
selecting the same face in POST. If no POST or Reference selection is available, PRE explains
where to select the Reference; it does not invent a baseline. Only one POST publishes that
chain's metric baseline at a time, and expired or mismatched publications are rejected.

Completed observations are reused between the Reference roles. The memory-only cache keeps up
to eight songs or 32 MiB of observation storage, whichever limit is reached first, evicting the
least recently used song. It does not store whole PCM or write a disk cache. Cancelled unfinished
analysis may restart when the selected source or role changes. Display snapshots and working
buffers are separate from this cache budget.

At 44.1, 48, and 96 kHz, a later Cue can be prepared from five seconds before its
start; those preceding samples warm up the existing measurement engine and are not
included in the Cue statistics. Other rates retain source-origin measurement.
Full-song analysis follows in the background. A Cue outside the prepared range
returns to **PREPARING** until its own observations are available.

**SHARP is available in POST's window only**: the optional PRE sharpness worker exceeded its CPU
acceptance budget, so PRE does not offer that menu item or run that optional worker.
