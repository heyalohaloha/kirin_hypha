# What it measures

Each metric with its window, unit and standard: Watch (LUFS-M, LUFS-S, True Peak, crest), the
FREQ spectrum, the SHARP perceptual Δ, the LIVE absolute facts and Record mode. Loudness and
True Peak follow ITU-R BS.1770.

[Back to the README](../../README.md#the-full-manual)

## Watch mode (real-time)

| Metric | Window / Unit | Standard |
|---|---|---|
| LUFS-M / LUFS-S | Momentary (400 ms) and Short-term (3 s) loudness (LUFS) | ITU-R BS.1770 |
| True Peak | Recent peak, last 400 ms (dBTP) | ITU-R BS.1770 |
| Crest Factor | Peak − RMS, 400 ms (dB) | — |

PRE displays absolute values. POST shows its own values (**POST**) or the difference from the
paired PRE (**Δ**). If that exact PRE is explicitly bypassed, the Δ views say **PRE IS OFF — ENABLE
PRE TO COMPARE** without releasing the pair; choose **POST** for absolute values. A stop, silence,
stale read, or temporary absence never claims that PRE was bypassed. An inactive PRE says
**PRE NOT RUNNING — CHECK PRE IS ENABLED**; stopping POST pauses the comparison. A distinct
POST-bypass reason is displayed only when supplied by the measurement ABI. LUFS-M and LUFS-S each keep
their own MAX value for the current Watch playback pass.
In LEVEL, the live 400 ms True Peak and its playback-pass MAX are distinct facts. Starting **Keep**
(MENU → Keep) changes the presented result context; **Max TP** then means the maximum for the whole Keep session,
including across transport stops. Watch, every MAX, and everything Keep and Record write are raw.

## FREQ: Spectrum (on demand)

A paired POST uses top-level **FREQ** to inspect processing between PRE and POST. Spectrum runs only
while FREQ is open. The signed **Δ (POST − PRE)** curve is the primary
**RAW** display, with absolute PRE and POST spectra retained as reference curves. **SHAPE** is a
one-click alternative: it removes the measured whole-aperture energy difference from each valid
band to make relative spectral reshaping visible. RAW and SHAPE use the same exact PRE/POST
apertures and require no additional FFT. Bands without sufficient energy remain absent, rather
than being joined across a gap or shown as a zero difference. Both modes use a ±24 dB
scale; the underlying difference is not clipped. A difference is produced only when PRE and POST
frames have the same sample rate, aperture length, FFT layout, channel mode, channel count, and
output-presentation sample endpoint.

| Control | Observation behavior |
|---|---|
| LR / MID / SIDE | Selects exactly one channel definition; the three analyzers never run in parallel |
| M/S | Overlays the POST-local `(L+R)/2` Mid and `(L-R)/2` Side spectra from one stereo aperture; unavailable for mono or Δ |
| RAW / SHAPE | Switches the same exact-pair curve between gain-inclusive difference and energy-normalized shape; clears a mode-specific MARK and Focus Trail |
| Hover / click | Reads frequency and the selected RAW/SHAPE value; click locks the probe, shows its six-second Focus Trail, and × releases it |
| MARK | Captures or replaces one temporary display-only reference in the selected mode; × clears it |
| PSB | Perceptual share by Bark band: how the loudness is shared across 20 Bark bands |
| LOW | Replaces the 10 Hz–22 kHz view with 20–300 Hz at a four times longer aperture (about 341 ms), so close bass notes separate; the highest peaks of the POST curve are tagged with note, frequency and level. It is the same analysis on the same exact PRE/POST pairing, never a second one, and needs about 0.4 seconds of unbroken audio |
| Free resize / 100–300% presets | Keeps a fixed 3:2 aspect ratio from 300×200 through the native 900×600 Inspection View and remembers the exact loaded-instance size. Drag the editor's own bottom-right grip in any host (Studio One on Windows and Pro Tools have no window frame to drag) or choose a size from the size menu. Above 300% the Inspection View is magnified in steps that keep it on whole device pixels for the display (450% and 600% on a Retina display; 600% at 100% Windows scaling; 400% and 600% at 150%; none fits a 1920×1080 display at 125%) |

The page analyzes one selected channel view at a time. **LR** transforms L and R independently and
averages their power, so opposite-polarity channels do not cancel. **MID** analyzes the `(L+R)/2`
waveform. **SIDE** analyzes `(L−R)/2` and is available only for stereo input; mono never fabricates a
SIDE result. Switching LR / MID / SIDE clears the old frame and waits for an exact PRE/POST match in
the newly selected mode. Record-mode N and Sharpness use their own independent-channel definition,
described below, so they can differ from MID or SIDE Spectrum on wide or phase-opposed material.

When POST has no verified PRE, Spectrum shows POST on its own: the current curve and a rolling
peak hold. **LR** keeps this flat view at every size, without a history field or receding ridges.
**MID** and **SIDE** retain the **six-second field** behind them. The field is a second axis inside the same
rectangle — vertical is time, not level. The newest observation is drawn along the bottom edge and
rises as it ages, leaving the top edge six seconds later, so a resonance that appeared, a sweep, or
a level move traces a visible shape. Each of its 180 rows is one thirtieth of a second and shows
the observation nearest that instant, at the full band resolution; density follows the measured
level, so a loud band is far denser than a quiet one. Nothing is averaged, blended, or invented
between observations. A row stays empty when the nearest observation is further away than the
cadence the host is actually publishing at, so the field is continuous at any buffer size while a
break in the measurement stays a break in the field.

For MID and SIDE at 200% and 300%, the same six seconds are drawn as a landscape instead of a flat field. The newest
spectrum stands on the plot itself as the front ridge, on the same frequency and level axes as the
curve, and older spectra recede toward a horizon: higher, narrower, and fainter with age. Each of
the 24 ridges is the one observation nearest its age, keeping the loudest band in each of its 96
columns so a narrow peak is not thinned away. A ridge with no observation within half a ridge
spacing is left out and grid lines join only neighbouring ridges, so a gap stays a gap and two lone
observations never grow a range between them. The landscape adds no value to the reading; the
current curve drawn over it does. LR and the paired Δ view stay flat. Hiding LR history does not
change measurement, peak hold or the retained history; changing the display does not discard it. At these two sizes the plot also
names what it draws (`Δ(f) = POST(f) - PRE(f)` or `L(f) = POST(f)`) and the analysis behind it —
aperture, FFT size, band count, and presentation rate — read from the frame itself.

In the POST target, **M/S** is a fourth display choice beside LR / MID / SIDE. It overlays solid
cyan Mid and violet Side curves calculated from the same aperture, with a shared
0 to −96 dBFS scale and a two-value probe at every editor size. M/S is an absolute POST observation:
the Δ target is disabled until LR, MID, or SIDE is selected, and M/S is disabled while Δ is active.
M/S retains AVG and a locked frequency when the UI skips continuous observations; a new analysis
aperture segment resets the curve without interpolating across the gap.
M/S creates no PRE request, six-second field, peak hold, MARK, or Focus Trail, and its choice never
leaks into SHARP. The loaded plug-in instance remembers the choice across editor close/reopen, but
new instances and restored DAW sessions start from the existing LR default.

Hovering the plot shows frequency, the note below 1 kHz, and the selected RAW/SHAPE value; larger views also show PRE and POST values.
Below the cycle-derived low-frequency confidence boundary (about 35 Hz), the frequency alone carries
an unobtrusive `~` prefix. The measured band and Δ remain visible and are not dimmed, hidden, or
replaced by a warning. Hover help explains that `~` means an approximate low-frequency position.
All Analysis hover help wraps and repositions inside the plug-in at every size. At 300 % and above,
pointing at an item on LEVEL, TIME, FREQ, SPACE or REF's B, C and V shows its help in one line over
the footer instead of a popup, in readable ivory text: across the whole footer row for the graphs, values and tabs (what the
item is and how it is used; PLR, for one, reads as the song's average dynamics, compared between PRE
and POST to see how much limiting reduced it), and in the status at the left for the footer's own
controls, which stay in view. The popup at 200 % and below keeps the full text. **MENU → Display →
Show hover help** disables or restores explanatory popups for every PRE and POST; the user
preference survives plug-in and DAW restarts. FREQ inspection, click lock, Focus Trail, and MARK stay
available while help is hidden. A click in the plot
locks that readout to the same frequency until its × is pressed. While locked,
**Focus Trail** shows six seconds of the selected mode: compact at the smallest size and in its own lane when space
permits. Its newest point is the same exact PRE/POST presentation frame as the live Δ, not a
UI-clock estimate. A missed UI poll does not erase valid older observations: retained points keep
their true sample-time positions. The work-surface stroke joins the surrounding exact points across a
missing endpoint so Windows scheduling jitter does not look like a broken curve; the missing time is
still retained as gap metadata and no measurement is inserted. Reversed or incompatible frames, and
a forward discontinuity beyond the six-second view, start a clean trail. After a backwards transport
move, PRE and POST may resume one analysis cadence apart; FREQ waits until both have crossed the old
endpoint, then resumes from their newest exact shared endpoint. The frequency lock and MARK remain
where the user placed them across a loop, silence, temporary warming state, or short I/O gap; only
the factual trail restarts on the new exact time axis. **MARK** freezes one display-only full-band curve as a solid amber reference beneath
the cyan live curve; pressing MARK again replaces it, and its × clears it.
MARK is temporary and is cleared when the pair, sample rate, FFT layout, channel mode, or page changes.
It adds no analyzer and changes no measured value. Exact 3:2 size is remembered for the loaded
instance, while Spectrum itself still opens off.
Focus Trail retains only fixed-capacity display snapshots while Spectrum is open. It adds no analyzer,
does not smooth or delay the live curve, and is discarded on pair, rate, layout, channel-mode, or page
changes.

Continuity has two bounded layers. POST keeps the newest eight already-computed exact Spectrum
differences in a fixed recovery ring, so a short UI scheduling stall can collect missed frames on the
next poll without another FFT. If a stall exceeds that ring, Focus Trail still keeps the surviving
exact points at their sample-time positions and connects only those surrounding observations for the
work-surface stroke. Both layers are display transport only: no dynamic growth, extra analyzer,
filesystem polling, or Audio Thread work.

The optional PRE/POST exchange is supervised by the existing 10 Hz IO update. On Windows its
short-lived request, readiness, and Analysis snapshots use a small pagefile-backed shared-memory
mapping, avoiding dependence on filesystem create/rename latency at the 30 Hz Spectrum cadence.
macOS retains the atomic-file transport. Watch, Record, and `plugin_data` keep their existing file
contracts on both platforms. Each shared slot is double-buffered and committed as one generation;
a reader keeps the last complete value during a contended update rather than accepting partial data.

The watchdog advances only after a request, readiness fact, or PRE snapshot was actually published.
If publication stops, the stable IO path retries one non-blocking exact exchange before the
1.5-second request lease can expire. During a transient gap, the last exact FREQ or SHARP
presentation is held only within that same lease boundary instead of flashing an empty page. A
publication that completes after its lease has already expired is not counted as live. No second
analyzer is started, no mismatched frames are joined, and no Analysis transport work enters the
Audio Thread.

Where both spectra are extremely quiet, the displayed Δ alone is faded toward zero: it is fully
suppressed at and below −120 dBFS and reaches full strength at −96 dBFS. This display floor does not
alter the captured PRE or POST values. The analyzer follows the host sample rate without resampling.
Its Hann aperture is rounded from the 48 kHz reference time of `4096 / 48000` seconds (about
85.33 ms), and its FFT is the smallest power of two that preserves at least 2× zero-padding. Thus
48 kHz remains exactly 4096 samples / 8192 FFT points, while 44.1, 96, 192, and 384 kHz use
3763/8192, 8192/16384, 16384/32768, and 32768/65536 respectively. Analysis remains at 30 Hz. The live
curve advances on every display VBlank after the 30 Hz source poll; numeric probes retain the raw
values at 2 Hz. Monotone cubic curves do not overshoot or join missing frequency bands. Missing
windows, clock gaps and pair/layout boundaries restart the drawing without interpolating across them.
LOW's LIVE curve follows with a 250 ms time constant; its note/frequency/dB labels stay together for
one second. PSB keeps its 10 Hz measurements and advances only its drawing between observations.
Neither presentation changes the measured endpoint. All curve points, hover interpolation, MARK, and Focus Trail use the
same logarithmic band centres `(index + 0.5) / 256`. The first and last edge labels remain plot
boundaries, not invented samples. It compares measured programme energy rather
than reconstructing a plug-in transfer function, so narrow low-frequency EQ shapes can appear broader
than the corresponding EQ control graph.

**MENU → Display → FREQ** offers **RANGE** (60 / 90 / 120 dB, with the established 96 dB default),
**TILT** (0 / 3 / 4.5 / 6 dB/oct), **SMOOTH** (OFF / 1/24 / 1/12 / 1/6 oct, with the established
low-band smoothing as STD), **LIGHT** (0.7 / 1 / 1.3), and **PEAK** (ON / OFF). Tilt is referenced to
1 kHz and applies only to absolute displays; Δ keeps its original meaning. Peak readings, numeric
probes, MARK and measured histories bypass curve processing. The measured history background is
cached between observations. Default settings retain the established range, smoothing, brightness
and peak line. At compact sizes FREQ leaves the bottom axis clear of the status strip.
The unpaired badge keeps its copper frame with neutral text.

**MENU → Display → LEVEL / NUM RATE** selects **LIVE** (the established cadence), **0.2 S**, **0.5 S**
or **1 S** for numerical readings. Slower choices hold one measured reading rather than average it,
including the MAX TP lower-bound number while its Session summary is pending.
State and availability boundaries appear immediately. Measurement, history, meter movement,
Capture, Record, Keep and Kirin OS output retain their raw values. Like FREQ AVG, these display
choices belong to the project state: only non-default values are saved, and older states retain
the established defaults.

## TIME / SHARP: Perceptual Δ (on demand)

From **TIME**, **SHARP** opens Perceptual Δ; the top-level **FREQ** domain opens Spectrum. The first
Perceptual Δ observation is **Δ Sharpness History**. It plots the signed Sharpness difference
`POST − PRE` over the latest six seconds, with the newest exact value shown in acum. The measured
difference is not clipped; the stable display scale is ±2 acum. The curve is spatially rounded for
legibility, but no temporal smoothing delays it. The curve keeps the same visual strength across the
full history. Its fill is controlled only by distance from zero: quiet at zero and progressively
denser toward either display edge, never by sample age. Only a small dot identifies the newest exact
observation. All measured
10 Hz points remain in a retained six-second timeline: the curve is repainted at 5 Hz and the exact
PRE / POST / Δ numbers are held for 500 ms. A delayed UI update catches up from that factual
timeline instead of exposing scheduling jitter as broken segments. A true measurement discontinuity
starts one clean new run; no missing value is interpolated.

Each point comes from one non-overlapping 100 ms aperture and is published at 10 Hz. Before the
first point, PRE reports readiness and POST commits one shared, aperture-aligned presentation epoch
at least 200 ms in the future. PRE and POST reset their Phase D and optional sample-rate-converter
state once at that epoch, then preserve that state continuously across later apertures. They must
match in schema, host sample rate, aperture length, state epoch, channel definition, channel count,
and output-presentation sample endpoint. If any of those facts differ, no Δ point is produced.

The psychoacoustic engine runs at its defined 48 kHz analysis rate after following the host-rate
input; at 44.1, 48, 96, or 192 kHz each endpoint still represents exactly 100 ms of host
presentation time. A non-48 kHz converter may buffer an additional chunk before publishing an
already-measured endpoint; it does not shift or interpolate that endpoint. A dropped block,
timeline discontinuity, mode edge, or missed epoch clears the short history and requires a new
shared future epoch instead of continuing state across a gap.

**LR** measures L and R independently and uses their arithmetic mean, so channel polarity cannot
cancel the observation. **MID** measures `(L+R)/2`; **SIDE** measures `(L−R)/2` and remains stereo-
only. Changing channel mode starts new history. Returning to **TIME / HISTORY**, another
non-analysis domain, or closing stops Sharpness and discards display history. Spectrum and
Sharpness stop each other before starting without releasing the owned optional-analysis slot.
Neither changes audio nor rewrites Watch or Record results.

## TIME / LIVE: absolute facts (on demand)

From **TIME**, **LIVE** opens a POST-only absolute timeline; top-level **FREQ** opens Spectrum.
LIVE does not subtract PRE and does not create a PRE analysis request. It overlays three measured
facts on the same latest-six-second time axis:

| Trace | Colour | Fixed display scale |
|---|---|---|
| LUFS-M | Cyan | −42 to 0 LUFS |
| Recent True Peak | Pale violet | −30 to +6 dBTP |
| Sharpness | Amber | 0 to 3 acum |

Each trace has its own fixed scale, so vertical position is comparable over time within that metric,
not numerically between the three metrics. Values outside a display scale remain measured facts and
are only bounded at the plot edge. In a sparse source, an exact aperture whose LUFS-M or recent True
Peak is below the measurement floor remains unavailable internally and in the numeric readout; the
curve alone reaches the lower plot edge so silence does not resemble a dropped UI frame. There are no
targets, score bands, warning colours, or verdicts.

All three values are committed at the same exact 100 ms POST endpoint in the current clock.
Presentation, project and local render coordinates support absolute facts; local coordinates do
not prove PRE/POST alignment. A clock-kind change rebuilds analysis state. Measurement runs
at 10 Hz, Rust retains 64 exact points, the curve repaints at 5 Hz, and current numbers update at 2 Hz.
No missing measurement is interpolated or stored as a value, and no temporal smoothing delays the
display. A short forward observation gap retains the verified points on both sides at their exact
sample times and joins only those points for presentation. A single verified point is shown as a
dot, and a temporary worker re-arm does not erase the last verified field. Backward transport, an
incompatible format, or a gap spanning the six-second field starts a new run.
LUFS-M and recent True Peak reuse Watch's measurement definitions; recent True Peak is the latest
400 ms maximum rather than a session hold. Sharpness keeps the established independent-channel
arithmetic-mean definition. Switching **ATTACK**, **FREQ**, **SHARP**, and **LIVE** replaces only the
analyzer in the owned slot. A non-analysis domain or editor close releases it and discards history.

At most two of ATTACK, FREQ, SHARP, and LIVE run per DAW process. This supports a mix bus plus one
working track without starting 12 costly analyzers. A third view names the two owners and waits.
PRE/POST pairs, Watch, Record, and audio pass-through have no such two-slot limit. Switching optional
views keeps ownership; only a non-analysis domain or editor close releases it.

## Record mode (Kirin OS required)

| Metric | Window / Unit | Standard |
|---|---|---|
| LUFS-M / LUFS-S | Momentary (400 ms) and Short-term (3 s) loudness (LUFS) | ITU-R BS.1770 |
| Integrated Loudness | Current Keep session (LUFS) | ITU-R BS.1770 |
| Max True Peak | Current Keep session maximum (dBTP) | ITU-R BS.1770 |
| Crest Factor | Peak − RMS, 400 ms (dB) | — |
| PSR | Peak-to-Short-term Ratio, 3 s (dB) | — |
| Sharpness | acum, independent-channel arithmetic mean | DIN 45692 |

PRE displays all six values. A paired POST displays Δ for LUFS-M and LUFS-S, PSR, Crest,
and Sharpness; Integrated Loudness and Max True Peak remain absolute POST session values. The
Integrated value spans transport stops within one Keep. After Stop, the final Record result remains
visible until the first newly computed Watch result arrives.
PSR always uses the engine's 3 s Short-term loudness.

**On True Peak.** Two distinct True Peak quantities are reported. The *recent peak* is the maximum inter-sample peak within the last 400 ms (the same window as LUFS-M) and is shown live in Watch mode; it is not held, so a transient drops out of the reading once that window has passed. The *session maximum* is the running maximum inter-sample peak over the whole recording and is what the Record data stores. When a single dBTP figure is quoted for a file, it is the session maximum. Peak windows are tracked by sample count, so offline / faster-than-real-time rendering does not shift them.

**On Crest Factor.** Crest Factor is the sample-peak level minus the RMS level (both in dBFS) over the same 400 ms window. Peak and RMS are both computed across the pooled samples of all channels — not a mono sum — and the peak is a sample peak, not the inter-sample True Peak. A silent window produces no value (shown as `---`).

**On the psychoacoustic metrics.** N (Zwicker loudness) remains measured and stored for Kirin OS
even though it is no longer one of the six DAW display cells. For multichannel Record data, N and
Sharpness are measured independently per input channel and combined by arithmetic mean. They do not
sum the waveform before the nonlinear psychoacoustic pipeline. Perceptual Δ uses that same definition
for LR, while its explicit MID and SIDE selections intentionally measure `(L+R)/2` and `(L−R)/2`.
