# Reference whole-song comparison

User direction, 2026-09-13: A is the current DAW input; B is a measured Version
of the same song supplied by Kirin OS. C remains an independently selected
Check. Reference Blind compares the song, not a four-second capture. INSPECT
and PRE/POST Blind are separate. No manual receiver connection or repeated A
registration belongs in this flow.

## Data and playback boundaries

- Kirin OS owns immutable Work / Recording / Version identities, measured source
  hashes, full-source observations and prepared position information.
- A remains live, including edits made in the DAW after the last registered
  Version. A short observation is alignment/calibration evidence, never the
  duration or frozen replacement of a Reference trial.
- A's current content must agree with the selected song. Copying B's Work or
  Recording identifier into a live-A receipt is not evidence of that agreement.
- Full-source measured loudness can be reused only for the exact measured PCM.
  Current DAW audio must be checked against corresponding measured intervals;
  old integrated LUFS cannot be relabelled as the current edited A's LUFS.
- Establish a coarse location from prepared observations, refine with bounded
  PCM probes, reject ambiguous matches, and check consistency over separated
  intervals. Run this on workers. Cache verified source observations by dual
  content hash, audio facts and algorithm revision.
- Map host positions into B's prepared streaming pages. A is never delayed or
  time-stretched. Receiving, restoring or preparing never selects B.
- Freeze approved comparison gains and the accepted map during Blind. Material
  source, mapping or calibration changes invalidate the trial and preserve the
  explicit safe return to A. Distinguish pause/seek from a source change.
- Trial history must describe the live A, measured B, alignment evidence,
  calibration coverage, actual audible coverage and hidden assignment. Do not
  describe a short-probe hash as a whole-song PCM hash.

## Structural corrections

The previous `ReferenceRuntimeV2Workspace` required `activeABinding` with a Work
ID. The Library receiver has no Work ID, so its Blind could never prepare. The
previous renderer played only a frozen four-second probe. The Library Version
path now captures live-A observations under the local receiver identity, verifies
the measured B acoustically, and streams B against live A. No Work identity is
assigned to A. Work-bound legacy trials retain their separate format and parser.

The current OS `kirin_content_features_v1` artifact has bounded onset, band
energy and loudness series; all chroma entries are null. The registered source
measurement waveform also provides per-channel RMS bins. Do not claim that
chroma matching already exists.

## Validation requirements

| Boundary | Required evidence |
| --- | --- |
| Recognition | Gain, EQ, dynamics and head-padding variants accepted; unrelated audio, silence, tones and repeated ambiguous excerpts rejected |
| Position | Offset error and uncertainty in samples; separated windows agree; explicit detection of drift and changed edits |
| Load | First match and cached match elapsed time, bounded reads and memory; no analysis, allocation, locks or I/O on the audio callback |
| Whole-song playback | A remains current live input; B continues beyond probe length; seek, pause, end and unavailable pages preserve safe A |
| Gain | Matched-time observations, measured-source identity, confidence and headroom; no stale measurement relabelled as edited A |
| UI | A/B/C across all sizes; Blind at 300%; tiny automatic delivery state; two Analysis slots and PRE/POST exclusion preserved |
| History/restart | Exact immutable source binding, hidden identities until reveal, real audible evidence; reopen returns A |

## Research references

Coarse-to-fine synchronization and matching of audio landmarks are established
approaches. The implementation must be evaluated on this mastering use case;
these references are not evidence that Hypha already meets its accuracy gates.

- https://github.com/groupmm/synctoolbox
- https://github.com/dpwe/audfprint

## Initial local experiment

The existing OS multi-window offset estimator was tested on a complete 183.94 s
local stereo song (44.1 kHz), with five probes distributed from 8 to 175 s.
Private temporary variants added 317 samples of head padding plus gain, low-end
EQ or nonlinear dynamics. Gain and dynamics returned exactly 317 samples in all
five windows in 96–97 ms. The EQ case returned 317/318 samples but failed its
single worst-window ambiguity threshold. Unrelated noise was rejected. This
small controlled experiment supported bounded probing. Original audio and
registered measurements were not changed.

## Implemented alignment and load bounds

1. Search at most 4,096 bound RMS bins, retaining six separated coarse positions.
   DAW time is only an additional hint, never proof of the source or song position.
2. Compare three separated one-second windows inside the four-second observation
   using FFT correlation, selecting the more energetic channel without summing
   anti-phase stereo. The 180 Hz–6 kHz analysis band establishes content agreement.
   Each correlation must be at least 0.75, each ambiguity margin at least 1.5 dB,
   and the median margin at least 3 dB. An accepted 700 Hz timing refinement may
   adjust a broadband estimate by no more than two samples.
3. Window offsets must agree within max(2, sampleRate/20,000) samples. Distinct
   accepted song positions more than 10 ms apart are ambiguous. They do not start
   comparison; a later passage can establish the map.
4. Once accepted, verify only the mapped location. Keep the map and gain through
   silence, transport pause and seeks. Positive content/timing contradictions
   invalidate playback to A; they never silently move an ongoing trial.
   A host-reported pause may omit its sample clock. Preserve the trial and require
   a valid clock again on resume; never fabricate a stopped position.

All decoding, hashing, FFTs and gain analysis run on workers. At most two alignment
jobs run together per process; busy instances retry. A capture is lazy and bounded
to four seconds, not the full song. A chosen Version is observed while Reference
is presented or auditioning. Reopening after hidden idle time requires fresh
evidence. Playback uses the existing prepared pages and a preallocated two-channel
scratch, with a fixed 5 ms transition. These limits do not cap 300% windows.

## Fixed gain contract

Use the existing ITU-R BS.1770 aligned-active-block policy: 400 ms windows at a
100 ms hop, at least 27 consecutive paired active blocks, and the median A−B
loudness difference. A stays at 0 dB and B receives the full fixed gain.
The ceiling is max(-1 dBTP, A observation TP, B measured whole-source TP).
If the full B gain exceeds that ceiling, normal B stays at original level; Blind
requires explicit approval of the displayed full attenuation for A, with B at
0 dB. END authorizes the return to normal A, including while the host is paused.
Partial gain, split gain, dynamic leveling, EQ and limiting are excluded.

The initial observation is not the whole-song LUFS of an editable A. That value
and the claimed whole-song loudness delta remain unavailable. Different dynamics
or EQ can produce different local loudness differences; this policy fixes one
calibration and does not chase those musical changes during playback.

## History contract

Library events use `kirin_reference_whole_song_trial_start` and
`kirin_reference_whole_song_trial_completed` version 1.0. They carry live A's
observation identity/hash, B's verified Work/Recording/Version and file/PCM
hashes, the bounded calibration ranges, full B playback range, frozen gains,
commitment/reveal and callback-confirmed audible frames. Each hidden stimulus
must be heard for at least three seconds before an answer. This is a preference
trial, not a claim that every second of the song was heard or an ABX result.

Kirin OS verifies the exact immutable manifest/preset/source, start bytes,
runtime, hidden assignment, source peak and range before showing completion.
Duplicate starts/completions, missing artifacts and changed source identities
fail closed. C++ canonical event JSON preserves UTF-8 Japanese text and UTF-16
key ordering, and accepts only the schema's safe integer numeric domain. It is
not a general floating-point JCS serializer.

## Current evidence and remaining device gate

- One actual measured stereo song, 44.1 kHz, 183.94 seconds: original, -6 dB,
  low-end EQ and nonlinear dynamics variants at 8, 91 and 170 seconds all matched
  with zero source-sample error (12 cases). Three unrelated probes were rejected.
  Source files and saved measurements remained unchanged.
- Constant 0/-6 dB variants recover the known gain within 0.01 LU at all three
  positions. This does not claim perceptually identical loudness for different EQ
  or dynamics, or universal matching across untested recordings.
- Synthetic tests cover anti-phase stereo, approved 16→48 kHz conversion,
  repeated identical passages, missing/stale files, silence, changed content,
  playback past the calibration range, source-end crossings, pause/END, and
  live-A bit identity. Four hundred whole-song callbacks produce zero observed
  C++ heap operations. This counter does not interpose system malloc.
- Native UI rendering covers ordinary A/B/C at 100–300%, concealment during
  Blind, safe END and 300%-only entry including the lower-A approval path.
- Signed AAX loading and real DAW listening at this new revision still require
  a fresh device pass. Older B-835/B-865 host evidence is not reused. Windows
  signing, installer and dedicated DAW gates remain part of release completion.

Additional primary references:
- https://tech.ebu.ch/docs/tech/tech3285.pdf — BWF TimeReference is metadata, not
  evidence of current DAW placement.
- https://www.rfc-editor.org/rfc/rfc8785.html — canonical JSON string/number rules.
