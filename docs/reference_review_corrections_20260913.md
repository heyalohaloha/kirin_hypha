# Reference review corrections — B-869 / W-3078

The B868/W3077 review reproduced four product failures: unchanged PCM moved on
the DAW timeline retained an incorrect map; edited live-A level retained stale
calibration; B choices depended on C checks; normal switching introduced a hard
sample discontinuity. Fix their ownership and lifecycle boundaries together.

## Ownership

- Kirin OS publishes a separate, immutable Version catalog from verified measured
  Works Versions. It does not depend on a Reference screen, enabled C checks,
  INSPECT, or a selected Work. Work/Recording/Version identities select B.
- Check presets retain their existing publication and choices. Version descriptors
  and their history receipts have their own schema; do not invent C presets or
  copy a B Work identity onto live A. Historical v1 Library records remain readable.
- The Hypha worker owns observation, mapping, calibration and source verification.
  Capture identity includes its position and format. Contradictory evidence
  invalidates admission; an idle comparison can rematch the same observation.
- A remains live at 0 dB. Fixed gain is not an adaptive leveller. Compare repeated
  observations at corresponding source positions to detect actual edits to A,
  preserving musical dynamics at different positions. Invalidate active Blind on
  material changes and recompute inactive calibration with existing paired-block
  gain and full-source peak rules.
- An explicit normal return retains its output admission through the short fade.
  B/C transfers share one admission. RT uses bounded, preallocated buffers only;
  offline, bypass, source failure and revoked admission preserve immediate A.

## Change and verification map

| Boundary | Code responsibilities | Required verification |
| --- | --- | --- |
| Input/catalog | OS Version discovery, source resolver, Library service/publication; Hypha Library parsing/model | Measured Versions with no C assignments; check removal/reorder; missing/corrupt/unmeasured source; authority loss; bounded asynchronous discovery |
| Selection/restore | Version selection, comparison settings/controller, workspace publication | Stable Work/Recording/Version selection; legacy restore; removed B stays unavailable; restart returns bit-identical A; C remains independent |
| Position/calibration | Observation identity and guard, Blind preparation/lifecycle, normal gain publication | 48,000-sample move of identical four-second PCM; changed gain; same-position DSP edit; unchanged and musically varying passages; silence; pause/seek; active-trial invalidation; automatic idle rematch |
| Audio transition | Controller admission, normal RT rendering, shared comparison routing | A/B/C rapid and interrupted changes; bounded fade; no overshoot; absent pages; offline/bypass; no allocations/locks/I/O; one shared owner; correct audible return journal |
| History | Version-specific event receipt and OS validation | Native start/completion accepted after restart; immutable Version/source/manifest binding; changed/missing/duplicate artifacts rejected; old Library and Work history retained |
| Presentation | Selection state/readiness and existing UI bindings | A/B/C all sizes; small automatic connection state; Blind only 300%; PRE/POST exclusion; no new confirmation burden |

Preserve the existing source line budget and isolate changed responsibilities from
oversized files before changing behavior. Run targeted regressions while the
affected boundaries are changing, then the full project baselines and a final
review. Native diagnostic builds are not signed DAW or public-release evidence.

## Final local verification

The implementation audit and final code review found no further actionable
defects in the four corrected boundaries. This does not waive the device gates.

- `cargo test --workspace --locked` and owned clippy: pass. Final `xtask` source
  contracts: 141 passed, zero failed. No Rust FFI implementation changed.
- Native Reference runtime, audio-page and correlation suites: pass. Gain-edit
  correction measured −6.021 dB; identical-PCM relocation by 48,000 samples
  produced zero mapping error. Active-trial edit invalidation and END passed.
- Normal A/B/C concurrency and whole-song callbacks: zero observed C++ heap
  operations, one external admission and bit-identical settled A. Direct system
  allocations are outside this counter; bounded JUCE buffer views were inspected.
- Actual OS Library 1.1 output: two POST receivers accepted the Factory catalog
  and independent measured Version. Native journals: 12 accepted, zero failures
  through the OS history reader, including Version 1.1 and legacy C 1.0 events.
- Kirin OS final `npm test`: 11,925 total, 11,920 passed, zero failed, five skipped.
  Focused delivery/resolver/history regressions: 28 passed. The reviewed dependency
  graph adds one main-process module and five edges, with no cycles or unresolved
  imports; the updated architecture contract passed in the final baseline.
- Source line budget, diff whitespace and the tracked JUCE build patch stack: pass.

A edits in never-observed passages are not instantly identifiable by the bounded
repeated-passage guard. Fixed gain does not chase changing musical dynamics.
Signed macOS DAW/AAX listening, Windows device validation and the complete
distribution artifacts are outside these local results and remain unverified
for this source revision. No installation or public release was performed.
