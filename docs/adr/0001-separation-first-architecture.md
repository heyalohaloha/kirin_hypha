# ADR 0001: Separation-First Architecture

Date: 2026-06-26
Status: Accepted

## Context

Hypha has accumulated fixes around the same fault line: PRE / POST run as separate plugin roles,
VST3 and AU use different shells, and DAW hosts do not guarantee restore, prepare, playback, and
GUI timing in a friendly order. Many recent defects were not caused by one bad condition. They came
from a behavior being fixed in one entry point while another entry point kept older semantics.

Long-term stability and measurement accuracy are more important than preserving the current file
shape. Large internal refactors are acceptable when they reduce cross-role ambiguity, improve test
coverage, or make Windows support less fragile.

The most important failure mode is not an ugly error state after a broken recording. It is allowing
Hypha to create broken measurement data in the first place. Recovery paths, quarantine folders, and
diagnostics are useful only as development evidence and last-resort containment. They must not
become the product strategy. The product strategy is deterministic, complete, measured data from a
simple user workflow.

## Decision

Hypha will move toward a separation-first structure. Each layer has one job and exposes explicit
contracts to the next layer.

1. Measurement core
   - Owns numeric correctness only.
   - No DAW, shell, GUI, install, or release knowledge.
   - Tests use known signals and deterministic fixtures.

2. Runtime state
   - Owns signal state, heartbeat, liveness, record state, and watchdog contracts.
   - Audio-thread callable functions stay in a clearly marked RT-safe surface.
   - No filesystem scanning or UI policy in RT paths.

3. Pairing service
   - Owns PRE discovery, candidate listing, name matching, scope inference, latch semantics, and
     Keep/Delta target selection.
   - GUI, FFI, JUCE, and IO threads must call the same selection semantics.
   - Display target and Keep target may have different activity gates, but not different identity
     rules.

4. Persistence and file exchange
   - Owns `$TMPDIR/kirin`, `plugin_data`, record signals, broadcasts, reservations, stale cleanup,
     atomic writes, and path safety.
   - Platform-specific paths are hidden behind an adapter before Windows work begins.
   - Shared mutable files must not be used as consumable truth when multiple roles or stems need the
     same fact. Capture metadata, record identity, and clock authority are session facts, not
     after-the-fact timestamp reconstructions.

5. Shell adapters
   - nih-plug VST3 and JUCE AU/VST3 are adapters, not policy owners.
   - They may translate state into controls, but must not reimplement pairing or record rules.

6. Release and install
   - macOS codesign, notarization, `.pkg`, AU resource usage, and Lemon Squeezy artifacts remain
     macOS release concerns.
   - Windows build and install gates must be separate, not bolted onto macOS release scripts.

## Consequences

- Existing large modules may be split even when the immediate user-facing behavior is unchanged.
- Public compatibility can be preserved by re-exporting moved functions during migration.
- Tests must follow the new boundaries: pure selection tests in pairing, realtime/FFI tests in FFI,
  shell parity tests around adapters, and release tests under platform-specific tooling.
- Fixes should be classified by boundary. A pairing bug is fixed in the pairing service, not in a
  GUI branch or one shell.
- Record / TRACE fixes must prefer prevention over presentation fallback. A change that hides,
  infers, or prettifies missing data is not a fix unless the underlying measurement path is also made
  unable to create that missing data under the same conditions.
- User workflow must stay simple. Internal strictness is allowed to reject bad artifacts before they
  reach the normal shelf, but it must not push new manual steps, timing rules, or operational burden
  onto the user.

## Migration Order

1. Extract pairing scope and selection from `record_signal.rs` and `io_thread_post.rs`.
2. Centralize shell-visible pairing UI state so egui and JUCE read the same derived facts.
3. Split PRE/POST IO loops into pollers, writers, cleanup, and broadcast handlers.
4. Introduce platform path adapters for temp, plugin data, install, and release roots.
5. Make Windows VST3 a new platform adapter path after macOS parity gates remain green.

## Acceptance Criteria

- A PRE/POST pairing scenario can be tested without instantiating a GUI or DAW shell.
- A shell change cannot alter pairing semantics without failing a shared test.
- Audio-thread functions can be audited as a small RT-safe set.
- macOS-specific release logic is isolated before Windows implementation starts.
