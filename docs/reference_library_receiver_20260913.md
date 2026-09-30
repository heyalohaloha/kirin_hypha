# Independent Reference library receiver

Reference receives saved Kirin OS settings without a Work connection, INSPECT, or receiver selection.
PRE/POST pairing remains a separate operation. Kirin OS publishes its saved library once; each
licensed POST selects its own preset, check, candidate and cue. Receiving or restoring settings
always leaves the ordinary A path selected.

## File contract

The existing platform-specific `plugin_data/reference/v2` root contains:

- `library/manifest.json`: `kirin_hypha_reference_library`, version `1.1` (legacy `1.0` accepted), monotonically increasing
  revision, default preset UUID and bounded immutable preset and Version receipts.
- `library/versions/<sha256>.json`: independent measured Work / Recording / Version
  descriptors. Their stable entry ID derives from those three real identities;
  the immutable SHA identifies the exact publication. C assignments and enabled
  checks do not determine which Versions appear in B. No synthetic Check template
  or Work association is persisted. At most 512 measured Versions are delivered.
- `library/presets/<sha256>.json`: `kirin_hypha_reference_library_preset`, version `1.0`, the original
  template receipt, display name, enabled checks and their candidates/profile bindings. An empty
  check remains empty; an unresolved candidate remains visible with `preparation_status: pending`.
- `sources`, `measurements`, `alignments`, `profiles`: existing content-addressed runtime artifacts.
  Audio identity, file revision, PCM identity, sample rate, channels and coverage retain their
  existing verification rules. An unavailable observation does not fabricate a measurement.
- `library/presence.json`: a five-second Kirin OS lease. This drives the small OS indicator; an
  expired lease does not erase an already verified library.
- `library/open/<request UUID>.json`: an explicit request to continue in the OS Reference editor.
  Removing a received request acknowledges the handoff, not successful navigation past an OS draft guard.
- `library/events/<runtime UUID>` and `library/manifests/<revision>.json`: immutable audition
  history bound to the exact library revision. Library events have `work_id: null`; they do not
  manufacture a Work association.

Hypha rejects malformed, oversize, changed same-revision and rollback publications. A partial or
rejected replacement retains the last complete verified library. A fresh receiver cannot accept a
corrupt library. Source validation and decoding remain outside the audio callback. Optional observations arriving
for the same verified file/PCM/format/revision do not restart audition or its SRC approval.
The admitted gain remains frozen; a changed audio identity or cue still returns to A.

## Display and admission

The initial C selection uses the OS default Preset, not the first receipt. Restored choices keep
their own identity. C's Check menu names the source even for one candidate; Preset and Cue stay
reachable while either visual view is selected and while audition is unavailable.

Visual focus and audible output are independent. `VIEW A/C` displays the Preset's configured
views; `VIEW A/B` displays the aligned Version and its Blind entry. Changing visual focus neither
selects A/B/C nor changes gain, source, cue, or admission. An unavailable audio button reveals that
source's explanation and controls without starting or interrupting audio. Ready audio buttons do
not move visual focus. The audible badge and gain always come from the audible owner, not the
viewed source. Capture's historical display is identified separately and is not silently discarded
by selecting audio.
Configured plots reserve height for their curves before choosing a stacked layout. At 600x400,
the default two views sit side by side when vertical stacking would collapse the plotting area;
the Preset's view order and data are unchanged. A/C line labels use the same cyan/gold as the curves.

Display evidence is verified on the existing non-RT worker before SRC approval. The approval-only
visual binding carries the immutable source and exact Cue bounds, but never `ready` or an aligned
audio mapping; no new decoder, analysis thread, queue or RT work is added. Sound still requires
explicit conversion approval and a separate audition click (which may be a queued B/C). Spectrum labels live A versus the
whole-track C distribution; the existing Tonal curve retains its own Cue aggregation contract.
The visual binding's Cue bounds are explicitly **source-rate samples**, independent of the
host-rate playback mapping. Both live and captured-A tonal readers consume that same source range,
including before SRC consent and after conversion. A transport-only output revocation does not
erase the verified display identity; invalid or replaced publications do.

Stopped B/C presses create a transient, identity-pinned next-play intent, not an output selection.
The existing processor control timer services it only while pending, including with the editor
closed; there is no new worker or decoder. Audio callbacks publish bounded safety transitions only.
View/capture and queued-B observation own separate demand bits on the existing observer, so closing
the editor cannot cancel the queued alignment. Completion/cancellation releases the queue's bit;
the audio callback reads the same bounded atomic demand instead of adding another observer.
The source/Cue/format identity, explicit conversion permission, callback/clock and prepared pages
must still match before the one-shot intent is claimed. The runtime selection generation spans
gain preparation and activation; an intervening A command or reconfiguration invalidates it.
Loudness/peak-matched queued starts require a valid match and never silently fall back to original
volume. A Preset explicitly set to original mode keeps that mode. Cancellation/failure reasons
remain visible. A cancels; B/C replaces the intent; source controls, restore, configuration and
Blind admission clear it. It is never serialized as playback authority or rearmed after stopping.

A is fixed to the live DAW input. B selects a registered Version from its dropdown; C selects a
Check (and its candidate when several are registered) from the independently retained preset.
A/B/C buttons and both dropdowns remain available at every editor size. Changing a dropdown
returns to A without starting audition. One button selects the prepared B or C source; missing
media in one choice does not disable the other. Both controllers share one output admission and
confirm an A return only after an actual A output block, never merely because C changed to B.
Ordinary A/B/C changes use a preallocated 5 ms fade. Overlapping return tails retain
one external admission; token-bound deferred releases cannot free a newer selection.
Missing pages, bypass, offline output and invalid source evidence preserve immediate safe A.
The DAW state stores both choices and cues, never audible selection or source media. Restoration
before/after preparation starts at A. Missing saved IDs stay unavailable until selected again;
malformed state is bounded and cannot replace the other valid choice. Legacy C-based
B IDs migrate only through matching source identities in verified preset receipts
(the current publication or at most 128 previous Library 1.0 revisions). Missing
proof leaves the saved choice unavailable, with no arbitrary Version fallback.

Preset/check selection and ordinary A/B/C remain available at every editor size. At smaller sizes,
`BLIND 300%` opens the 900 × 600 editor; starting a trial is a subsequent explicit action. Active
Blind screens stay at that size. Closing PRE/POST Blind restores the previous editor size.

The existing two optional Analysis slots remain the resource limit. Window size alone does not
reserve a slot and this change does not impose a window-count cap. Reference audition and local
PRE/POST Blind retain the shared process/project admission gate, including rollback of partial
acquisition. Neither route can silently displace an existing audition owner.

## Verification and remaining host boundary

The native library fixture exercises two independent POST receivers, actual OS-produced audio and
measurement artifacts, explicit B playback, bit-identical A return, source mutation rejection,
host reprepare and immutable completion history. UI contracts cover all-size selectors, compact
numeric values, connection accessibility, Blind concealment and the Reference access surface.
Workspace Rust tests and clippy cover the unchanged shared admission implementation.

The Reference library does not invent a live A recording identity. Version Blind verifies
the live A acoustically against the selected measured Version, then streams the full song
using the accepted source/host map and fixed gain. The AAX PRE/POST Blind entry is enabled
by user direction, with exact capture and runtime clock/PDC checks retained. Its host
validation is pending; Reference remains an independent comparison flow. These are verification boundaries, not completed
host-validation claims.
