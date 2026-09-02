# ATTACK Phase 0 baseline

**Date**: 2026-08-30
**Implementation label**: B-546
**Base**: `6d09d63bbf2ceba08c16ed3bce512bbb1ac99df5` (`[B-545] Preserve pre-engine host component off [ci full]`)
**Branch**: `codex/hypha-perceptual-presentation`

## Purpose

This is the pre-ATTACK source and presentation baseline. ATTACK work must preserve these results and
must not relax an existing limit to make a new implementation pass.

The tracked JUCE patch state was verified before measurement as upstream
`4f43011b96eb0636104cb3e433894cda98243626` plus the seven repository patches.

## Automated source baseline

| Gate | Result |
|---|---|
| `cargo test --workspace --locked` | pass |
| `cargo clippy -p kirin_measure -p kirin_hypha_ffi -p xtask --all-targets --locked -- -D warnings` | pass |
| ignored `parity` inventory and run | 20 / 20 pass, serial |
| ignored `pairing_candidates` inventory and run | 5 / 5 pass, serial |
| pure C++ UI contract | pass |
| JUCE PRE runtime contract | pass |
| JUCE UI render contract | pass on two repeated runs from one Release binary |

The first UI render invocation measured the compact Focus Trail at `4.065 ms/frame` against the
unchanged strict `< 4.0 ms/frame` gate. Two immediate repetitions of the same Release binary passed
at `3.915` and `3.928 ms/frame`. This is retained as measurement variance, not used to raise the
budget, and must be watched after ATTACK is linked into the same editor.

## Existing optional-analysis performance

Measured in the optimized release profile at 48 kHz:

| Existing mode | Measured work | Projected CPU |
|---|---:|---:|
| one paired SHARP view | `5.218 ms / 100 ms aperture` per worker, PRE + POST | `10.435%` |
| two local POST LIVE views | `12.047 ms / 100 ms` for the pair | `12.047%` |

The ATTACK comparison must report the same quantities at 48 and 192 kHz, one and two slots, plus
hop P50/P95/P99/max, ingress drop count, queue high-water mark, and fixed memory per slot.

## Existing render baseline

The table records the slower value from two passing runs of the same x86_64 Release test binary.

| Preset | LIVE | SHARP | FREQ | FREQ with Focus Trail |
|---|---:|---:|---:|---:|
| 100% | 0.207 ms | 0.375 ms | 3.761 ms | 3.928 ms |
| 125% | 0.258 ms | 0.459 ms | 4.123 ms | 4.609 ms |
| 150% | 0.270 ms | 0.527 ms | 4.336 ms | 4.764 ms |
| 200% | 0.283 ms | 0.710 ms | 4.912 ms | 5.068 ms |

## Manual baseline still required before public navigation

Automated source gates do not replace the plan's host baseline. Before ATTACK can be public, the
same Studio One session and generated signals must be captured on macOS and Windows for:

- METERS, FREQ, SHARP, and LIVE at 100%, 125%, 150%, and 200%;
- all page transitions, editor close/reopen, two occupied slots, and one waiting slot;
- identity pass-through, silence, stop, loop/locate, PRE OFF, PRE-only load, and POST-only load;
- worker/exchange interruption and recovery;
- Audio Thread time, analysis drop count, and plugin-reported latency.

Windows work must use `docs/windows_validation_remote_access.md` from the Kirin Sense Lens
repository before connecting to the validation machine.

## Files and symbols that ATTACK must not rewrite

- `AnalysisViewMode::{Spectrum = 0, Perceptual = 1, Absolute = 2}` and its `TryFrom<u8>` decoder.
- Existing `KirinSpectrum*`, `KirinPerceptual*`, and `KirinAbsolute*` C structs, field order, size,
  offsets, status values, exported functions, transport slots, and Windows mapping size.
- Existing Spectrum, Perceptual, and Absolute Rust analyzer/history modules and their golden values.
- Existing FREQ, SHARP, and LIVE JUCE components, painters, hit targets, palettes, and render goldens.
- Watch, Record, Keep, Pairing, PRE Display, TRACE, `plugin_data`, and release package schemas.
- Audio Thread calls already admitted by `process_block_calls_only_rt_safe_ffi_surface`.

ATTACK may add a top-level route, dedicated runtime/analyzer modules, dedicated request/readiness and
payload transport, additive C ABI symbols and structs, and a dedicated JUCE component/painter. The
single process-wide two-slot lease coordinator remains the only owner authority.
