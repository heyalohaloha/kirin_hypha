# Independent Reference library receiver

Reference receives saved Kirin OS settings without a Work connection, INSPECT, or receiver selection.
PRE/POST pairing remains a separate operation. Kirin OS publishes its saved library once; each
licensed POST selects its own preset, check, candidate and cue. Receiving or restoring settings
always leaves the ordinary A path selected.

## File contract

The existing platform-specific `plugin_data/reference/v2` root contains:

- `library/manifest.json`: `kirin_hypha_reference_library`, version `1.0`, monotonically increasing
  revision, default preset UUID and bounded immutable preset receipts.
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
corrupt library. Source validation and decoding remain outside the audio callback.

## Display and admission

A is fixed to the live DAW input. B selects a registered Version from its dropdown; C selects a
Check (and its candidate when several are registered) from the independently retained preset.
A/B/C buttons and both dropdowns remain available at every editor size. Changing a dropdown
returns to A without starting audition. One button selects the prepared B or C source; missing
media in one choice does not disable the other. Both controllers share one output admission and
confirm an A return only after an actual A output block, never merely because C changed to B.

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

The Reference library does not invent a live A recording identity. Version Blind still requires
the existing verified A binding and exact-range comparison evidence. The AAX PRE/POST Blind gate
also remains closed until exact-range project-clock/PDC host proof; ordinary Reference A/B and
metering are independent of those trial gates. These are verification boundaries, not completed
host-validation claims.
