# Live PRE/POST compare

LISTEN switches between PRE and POST of the same chain while the DAW plays, with MATCH and AUTO
for level. Where Hypha cannot confirm the timing between PRE and POST, POST plays and PRE returns
once the timing is confirmed. This page lists the host conditions for Pro Tools, Logic, Studio
One / Studio Pro and REAPER.

[Back to the README](../../README.md#the-full-manual)

The user decision of 2026-10-13 makes ordinary experimentation continuous: adjust intervening
plug-ins, track faders, mute/solo or automation, edit loop ranges, and open or close windows while
comparing. Unrelated project edits do not change the chain proof. Unverified PRE is never played;
LISTEN and BLIND retain your choice and return automatically when fresh proof is available.

Switch between PRE and POST of the same chain while the song keeps playing. When you select PRE,
POST plays the input that PRE received, lined up with what POST is processing by the DAW's timing
and delay compensation. Where Hypha cannot confirm that line-up, you hear POST, and PRE comes back
by itself as soon as it can confirm it again. Short checks are quiet and the selected PRE button
stays selected. Measurement and Records are never changed.

LISTEN, level matching and BLIND are available in macOS AU, VST3 and AAX, and Windows VST3
and AAX. Logic AU supports continuous and cycle playback. REAPER VST3 uses its processing
counter, calibrated against compensated project positions; its measurement clocks keep their
own rules. [REAPER loop conditions](#reaper-listen-timing) are described below.

**Pro Tools:** use the signed AAX Native release in regular Pro Tools, with delay compensation
enabled. PRE and POST can be true mono or stereo (multichannel) instances. A multi-mono insert
measures each channel independently and does not offer live comparison, because Pro Tools can
process those channels in parallel. The comparison clock uses Pro Tools' reported timing and
delay-compensation state, available from Pro Tools 12.6. Loop playback without a selected range
is treated as linear playback. If timing cannot be confirmed, POST remains audible and the
comparison resumes when confirmation returns. END stays available throughout.

An I/O (hardware) insert can sit between PRE and POST. Pro Tools may not compensate all of that
path's delay. When Hypha can judge the audio and PRE is early, it reads PRE that much later and
the status line says so, for example **PRE 21.33 ms aligned**. Turn off **Align PRE automatically**
in MENU to hear PRE where the clocks put it. LISTEN and BLIND across an I/O insert were measured on
Pro Tools Developer 26.4.0.5 at 48 kHz with hardware buffers of 256, 512 and 1024 samples.

The signed 1.1.51 release has passed PRE/POST loading, exact pairing, normal audio, Keep,
offline bounce, and saving/reopening in regular Pro Tools Native on Intel macOS and Windows x64.

**Studio One / Studio Pro:** the VST3 comparison profile accepts Studio One 6–7 and Studio Pro 8
with a readable native version. The measured profile is Studio Pro 8.1.2.113407. Separate
real-host measurement checks confirmed zero FREQ, SHARP and PSB differences with a
4096-sample delay and loops in Studio One 6.6.3 / 7.2.3 and Studio Pro 8.0.2 / 8.1.2 on macOS,
and Studio Pro 8.1.2 on Windows.

In another AAX host, a looped selection that cannot be proven asks you to turn loop playback
off while POST plays. Brief timing checks keep your selected controls and stay quiet. After
three seconds without recovery, the status area explains the observed cause and remedy.

<a id="reaper-listen-timing"></a>
**REAPER LISTEN timing (1.1.51; stereo output checked in REAPER 7.71 on Intel macOS).** Both PRE and POST
must use VST3, mono or stereo, in the same compensated chain. The executable/version policy is the
same as [REAPER's measurement certificate](observation_domains.md#sharp--how-perceptual-brightness-changed-over-time). Each instance counts the frames it actually
processes; eight consecutive agreeing linear blocks establish the offset between those independent
counters. An unverified block plays POST immediately. Named LISTEN can reacquire after an identified
stop or seek; BLIND keeps the same hidden assignment and resumes after fresh proof. Saving or reopening restores no
selection or MATCH permission.

The first loop can establish its offset using REAPER's live `chain_pdc_actual` observation.
The supported route is a normal two-channel track with mono/stereo inserts, exactly one PRE and
one POST in that order in the main serial FX chain. Input/take FX, containers, parallel FX,
oversampling, external hardware inserts, special PDC modes and unreadable identities are withheld from this new route.
The message thread reads the public host API, binds every FX GUID and its order to the live
project/track/process, and discards pre-playback observations even when the API reports success.
Two agreeing observations with callback progress are required. API/identity/configuration changes,
stop/seek, prepare or stale audio callbacks revoke the observation. Project-wide edit counts only
reject edits during a read; unrelated track edits do not change the chain fingerprint.

The address limit is **actual chain delay + 8192 samples**; the supported actual delay is at most
65536 samples. The allowance covers relative PRE processing and block boundaries, not the DAW's
playhead render-ahead time. The complete age interval must lie in PRE's measured native linear
segments and have exactly one matching address. At PRE's first observed block-boundary wrap,
both the segment before the wrap and the segment after it remain available, so POST can still
read the delayed tail. Once the loop length is measured it must exceed the limit; the ring must
also exceed it. Incomplete coverage, ambiguous addresses and unverified wrap boundaries keep
POST playing. This permits entry during the first lap after observation/calibration and PCM
availability; it does not promise immediate output on the first callback. MATCH still needs
at least three seconds of continuous proven audio. Without the API proof, the existing linear
calibration and its 73728-sample verified-address envelope remain available.

The timer normally reads at 50 ms. Evidence expires after 500 ms without refresh or two seconds
of processed samples without a fresh observation. Expiry withholds PRE for that block; the same
fingerprint and delay in the same run can resume LISTEN or BLIND with its approved gain. A changed
proof still revokes K; both LISTEN and BLIND resume only after new proof. Idle instances check at 250 ms;
only relevant playing instances inspect the API at 50 ms. These are freshness limits, not a guarantee
that a host report precedes an audible PDC change. Dynamic API notification order, scheduling
outside the measured conditions remain host tests.

Timing measurements used REAPER 7.71 on macOS, 96 kHz, stereo, 512-frame blocks, anticipative FX
processing off and on (200 ms render-ahead). Reported 4096→0 latency changes and bypass reductions
were detected in the same POST block in all 12 measured transitions: the observed undetected window
was **0 ms**, at callback resolution. Reported 0→4096 increases, including bypass release, preserved
the correspondence because REAPER processed upstream FX ahead. This is a reduced timing sample,
not a guarantee for every host version. Separate signed-release checks in REAPER 7.71 on Intel
macOS verified LISTEN and BLIND output, recovery during the tested edits, END, and session reopen;
the timing measurements above retain their own narrower scope.
Unreported latency changes were not detected by the clocks; an independent negative test remained
misaligned for about seven seconds until playback stopped. As required by R-12, every intervening
plug-in must report latency correctly and REAPER must compensate it. No clock-only rule can repair
an unreported delay. The REAPER results do not establish native Windows REAPER acceptance or coverage of every
rate, buffer size, delay, and plug-in configuration. [Timing contract and regression coverage](../planning/hypha_reaper_listen_timing.md).

1. In POST, select the exact PRE pair. At 200% and above, press **LISTEN** (**PRE/POST LISTEN** at
   300% and above) in the footer.
2. Play the DAW and choose **PRE** or **POST**. Each switch crossfades over 5 ms. The next LISTEN
   starts on the side you chose last, saved with the project; BLIND and A/B always start on their
   own terms, and opening or restoring a project never starts LISTEN.
3. Press **MATCH** to level PRE to POST. Hypha measures the latest four seconds of playback (at
   least three, BS.1770 loudness, up to ±24 dB) and applies that gain to PRE only; the PRE control
   shows it, for example **PRE +3.2 dB**. If PRE would have to rise above the larger of −1 dBTP and
   the measured true peaks, which is common with a loud, limited master, MATCH asks first:
   - **Lower POST** by the whole difference and keep PRE at its own level. POST then reads, for
     example, **POST -7.0 dB**.
   - Or raise PRE only up to that ceiling. MATCH reads **TP LIMIT**, PRE stays quieter than POST,
     and Hypha reports the gain a full match would need.

   Closing the menu changes nothing. MATCH is fixed: it does not follow later level changes, so
   press it again after changing the chain. Each session starts PRE at unity gain. If a raised PRE
   would still peak above the ceiling later in the song, that block plays POST. PRE returns
   automatically when it is safe at the approved gain. BLIND does not play POST there: it matches
   on the quieter side for the rest of the trial ([BLIND](blind.md)). A new gain while PRE plays glides over 50 ms instead of jumping.
4. Once matched, press **MATCH** again to turn it off: PRE returns to its own level and a lowered
   POST rises back to where it was when LISTEN began, in half a second. Press it once more to turn
   the same match back on, with the same gain, ceiling and approved attenuation. While MATCH is
   off, AUTO rests and BLIND makes its own match. **MATCH again** (a new measurement) and **AUTO**
   are in MENU and on the AUTO key. With AUTO, PRE follows POST:
   every second of playback Hypha measures again and moves PRE once it is 0.5 dB or more away. The
   control reads **AUTO**. AUTO never moves POST, never raises PRE above the ceiling approved at
   MATCH and moves PRE at most 6 dB from that MATCH, except lower when the ceiling needs it. At a
   limit PRE stays there, AUTO keeps following, and the status line says how far PRE is from POST.
   After a loud passage PRE stays down until the next MATCH, so the level does not pump. Silence
   changes nothing. AUTO is not available after a TP LIMIT match, and it stops at END.
   Hiding the editor rests AUTO at its last gain. It continues from the same MATCH when you show the
   editor again in the same LISTEN; if it cannot, the editor says it stopped. These values are
   experimental until listening tests settle them.
5. Press **END** once to end the comparison and clear MATCH. If POST was lowered, the button
   names the rise in advance, for example **END +7.0 dB**. Hypha first returns from PRE to POST,
   then restores normal POST level within half a second. While the DAW is stopped it returns at
   once; when the DAW sends no audio at all, the return finishes after half a second of quiet and
   the next playback starts at normal level.
6. Closing and reopening the window keeps LISTEN, MATCH and the same BLIND trial running.
   A changed pair, format or restored saved state ends the old comparison and holds approved attenuation.
   **RETURN +7.0 dB**, for example, explicitly restores normal level. Offline render, a bypass
   the DAW reports, and another audition are never attenuated. Measurements remain before this path.
   A DAW does not tell a plug-in when a realtime bounce runs, so a realtime bounce or export while
   LISTEN, BLIND or A/B is on records what you hear. Press **END** first. The first comparison you
   start in each Hypha says this once.

Logic's cycle path relies on every intervening plug-in reporting its latency correctly and Logic
compensating it, as required by R-12. Set Logic’s Plug-in Latency Compensation to **All**.
Off, Audio and Instrument Tracks on auxiliary/output chains, and Low Latency Mode require
separate host acceptance; an unreported bypass or delay cannot be inferred from clock equality. Equal presentation values are accepted together with the
render proof; they cannot distinguish zero latency from an unreported delay. A latency-changing
setting can also leave a short interval before its change becomes observable. Hypha cannot repair
that missing host information. The callback proof and regression fixtures do not replace actual
host acceptance for low-latency mode, auxiliary buses, live inputs, other buffers/rates or dynamic
PDC changes. An unverified block plays POST; fresh proof resumes the same BLIND trial.

For a DAW loop, press **LISTEN** or **BLIND** during loop playback. No Hypha LOOP button,
LOOP-off step or per-lap approval is needed. Initial comparison needs independent timing proof:

- VST3: the common content-clock profile measured in Studio Pro 8.1.2.113407, enabled for
  Studio One 6–7 and Studio Pro 8. REAPER uses the separate conditions above.
- Logic AU: continuous render stamps name the same content sample, and the difference between
  the contemporaneous PRE/POST render positions must equal their reported output presentation
  difference, including zero. This is restricted to the native `Logic Pro` executable with a
  readable version; the discovery measurements used 12.2. Missing, reversed or contradictory
  reports are refused. Other AU hosts retain the positive-difference and unique observed-loop
  rule. Initial VST3/AU mixed-clock pairs are not certified.
- AAX: the AddClock profile measured in Pro Tools Developer 26.4.0.5, granted to every Pro Tools
  build from 12.6 on, plus the documented sample-rate-specific maximum compensation bound
  (16,383 samples at 44.1/48 kHz, 32,767 at 88.2/96 kHz, 65,534 at 176.4/192 kHz). The loop must
  be longer than that bound. The bound is the only occurrence authority: a chain whose delay
  exceeds what Pro Tools compensates would pair a neighbouring lap, which no content check
  catches, so other builds are certified by the floor rather than measured. Pro Tools reports
  an insert's position compensated by the delay before it, so for that many samples after each
  wrap POST's position lies before the loop start; Hypha reads it as the previous lap's tail,
  which is the audio it carries. With an I/O insert in the chain, POST can run at the hardware
  buffer while Pro Tools names one position per engine chunk. Hypha fills in the positions the
  host did not report only after a chunk closes exactly: the engine clock advanced by one block
  length per callback, and the next position stepped by exactly those frames. The filled-in
  positions are not evidence on either side: K still comes from the host's own reports. A
  repeated position whose PPQ moved or is missing, or a new sample rate or clock report, ends
  this until a chunk closes again. Below such a buffer K can fall below zero, with PRE ahead of
  POST on the engine clock. In a loop that candidate must lie inside what PRE has already written,
  and no other lap may fit both the written range and the host's delay bound.

If proof stays unavailable for three seconds or the loop is too short, Hypha explains the condition and keeps POST
playing and leaves **END** available. It does not present LOOP-off or re-MATCH as the normal
workflow. The rules also allow ordinary **LISTEN → MATCH** playback followed by LOOP, while
retaining the fixed gain and selection at confirmed wraps. Short loops can accumulate real
playback time for **MATCH again**; Hypha does not duplicate recorded samples.

The approved MATCH gain and ceiling stay fixed during timing recovery. A stop, seek, latency
change, missing clock, callback gap or brief API expiry withholds only unverified PRE. Both LISTEN
and BLIND resume automatically after fresh timing and PCM proof. This is not a new level match.
State restore and pair, format or PRE-owner changes end the old authority. A failed or cancelled
rematch never discards the previous gain.

Some hosts hold POST's reported position at the loop start while delayed audio is still arriving.
That interval stays on POST until the occurrence is proven, not on unverified PRE. Native
full-frame tests cover initial entry, dynamic processing, clamped wraps and failure paths.
Real-host evidence belongs to its exact commit, host, format and configuration; it does not
certify every host, all transitions or signed retail AAX merely because a bundle builds.

While a session runs, **END** and **MENU** stay at every size; MENU includes any controls that
do not fit the footer. You can close Hypha or open another plug-in while comparing; reopening
Hypha shows the existing comparison, including the same BLIND assignment. MATCH accepts a request
before enough audio is available and completes on the processor, including while its editor is
closed. If lowering POST is needed, it still asks for approval when the editor is available.

- The line-up relies on every plug-in between PRE and POST reporting its latency correctly. Every
  two seconds of settled playback Hypha also compares the audio itself. Initial alignment and
  a changed clock mapping are checked every 100 ms using fresh, validated audio windows;
  **Aligning PRE** shows that this check is incomplete. Initial silence, a held tone, a limiter or
  outboard between PRE and POST may not confirm a read position: POST plays for at most one second
  of proven playback, then PRE plays where the clocks put it, as with alignment off. A later
  confirmed early PRE is still aligned through the usual fades. A confirmed late PRE cannot be
  read earlier, so it keeps playing where the clocks put it, and after three seconds the status
  line says so, for example **PRE 2.31 ms late / Check chain delay and DAW compensation**. When PRE is early, as with a
  plug-in between that under-reports its latency, Hypha reads the audition's PRE that much later
  and says so, for example **PRE 2.31 ms aligned**; the amount stays through seek, stop and loop and
  follows a change. With **Align PRE automatically** off, or when PRE is late, the status line
  shows the difference instead (**PRE 2.31 ms early**, **late**). An intentional delay, reverb or
  heavy processing can look the same, and silence cannot establish a position. Repeated audio uses a check of each current block, with a short notice; a nonmatching block uses POST immediately. Only the
  audition's read position moves: the clock proof, MATCH, A, the meters and Record stay as they are.
- During a BLIND trial a new alignment waits until you switch to POST and the fade finishes.
  A seek, stop or timing interruption alone does not move it under the same selected source.
  When the clock mapping changes, an unverified correction uses POST until fresh content
  confirms the new read position, including when the previous correction was zero, for at most
  one second of proven playback; then PRE plays at its clock position. A publication
  collision alone keeps the trial's fixed read position. Recheck guidance is the same for both
  anonymous sources; neither source earns a listening receipt from substitute POST.
  PRE then returns with the same selection and approved gain;
  no samples at the old, invalid position are used for a fade.
- If a strong, unambiguous content offset changes to a difference Hypha cannot align, or shows
  that PRE is late, PRE keeps playing at its clock position and the comparison continues; the
  audio estimate alone never switches you to POST. After three seconds the status line shows the
  measured amount and asks to check chain delay and DAW compensation. In BLIND the words are the
  same for both sources, and RESULT says by how much PRE may have been off. Two fresh estimates
  back at the original baseline clear the notice; stopping and restarting also resets it. Weak
  peaks caused by processing do not create a notice, and do not keep one: after one second of
  playback without a strong estimate that still shows the change, it ends. A late PRE under
  0.25 ms is not treated as a delay and plays as it is. A bass-heavy window that the chain has
  changed is skipped rather than counted as doubt.
- While Pro Tools reports delay compensation off, POST plays. Enable compensation to resume
  automatically. A brief interruption is quiet; a condition lasting three seconds gets guidance.
- If the chosen PRE is one channel of a multi-mono plug-in in Pro Tools, LISTEN does not start
  and the status line says so.
- Right after a seek or a new start, you hear POST for about the latency of the chain plus a few
  blocks while Hypha confirms the line-up.
- Silence does not require a new MATCH. PRE returns automatically when independent timing
  evidence still establishes continuity. If the DAW leaves an unexplained clock or callback gap,
  Hypha uses POST while proving new timing, then returns automatically.
  An inactive meter alone cannot distinguish silence from host suspension or bypass.
- If you change a plug-in setting that changes its latency (look-ahead, oversampling, linear
  phase) while comparing, PRE can be misaligned for a moment right after the change: a few audio
  blocks (measured at up to four blocks, 171 ms at 2048 samples).
- PRE follows the exact occurrence of audio currently reaching POST, not just the displayed
  playhead or the newest PRE block. A previous-pass occurrence is valid only when the delayed
  POST audio actually belongs to that pass and the timing proof uniquely identifies it.
- As in Reference, a stop or a seek does not deselect PRE: it waits and returns by itself.
