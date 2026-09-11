# Kirin Hypha AAX Phase A readiness

Date: 2026-09-07

This document records the SDK-independent Phase A preparation. Later macOS build and signing
results are recorded separately in `docs/aax_macos_universal_build_20260910.md`; Phase A alone does
not claim AAX product support.

## Build boundary

AAX remains disabled by default. The existing platform formats stay AU + VST3 on macOS and VST3
on Windows/Linux when `KIRIN_HYPHA_AAX_SDK_PATH` is empty.

An AAX configure requires all of the following:

- macOS or Windows;
- an external SDK root containing `Interfaces/ACF`;
- `KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED=ON`.

`KIRIN_HYPHA_REQUIRE_AAX=ON` converts a missing SDK path into a configure failure. The SDK must
remain outside this GPL repository. `scripts/check_aax_sdk_absence.mjs` inspects the working tree,
including ignored source/vendor paths, while excluding generated build/output directories.

## Identity candidate

The AAX targets reuse the shipping PRE/POST identities:

| Role | AAX identifier | Manufacturer code | Plug-in code |
|---|---|---|---|
| PRE | `com.kirinmastering.hypha.pre` | `Kirn` | `Khpr` |
| POST | `com.kirinmastering.hypha.post` | `Kirn` | `Khpo` |

The category remains JUCE's `ePlugInCategory_None`, which Pro Tools Ultimate 2026.4.1 displayed under
**Other** during the 2026-09-11 Intel host validation. AAX SDK 2.9.0 has no dedicated analyzer or
meter category. See `docs/aax_macos_universal_build_20260910.md` for the host evidence and remaining
candidate boundary.

## CI boundary

`.github/workflows/aax-phase-a.yml` always runs the SDK-absence guard. Its build matrix is skipped
on normal pushes and pull requests and on the default manual invocation. A manual operator must
explicitly request the build, confirm the license, provide the external path, and use SDK-equipped
self-hosted macOS and Windows runners. The two runner paths are separate workflow inputs because
their filesystem syntax and SDK installation locations differ.

The macOS self-hosted leg uses `scripts/build_aax_universal.sh` and builds both Apple architectures
before linking PRE/POST. It deliberately does not sign in CI. PACE signing stays a release-operator
step on the Mac holding the physical authorization device. The Windows leg remains host-x64 and
writes build provenance; a diagnostic build without the licensed App font cannot enter signing.

## Remaining external gates

- The external AAX SDK 2.9.0, macOS Universal build/signing path, and Windows x64 PRE/POST build
  path were verified on 2026-09-10. The Windows combined PACE/Authenticode signing path was verified
  on 2026-09-11; the VST3+AAX installer lifecycle remains.
- The signed B-786 macOS artifact passed Intel Pro Tools Ultimate 2026.4.1 load, reopen,
  stereo/multi-mono insertion, pairing, zero-delay reporting, and Offline Bounce. Re-run the host
  gates for the exact current release candidate; arm64 Pro Tools execution remains unverified.
- Verify current-candidate bypass/state persistence and the AAX-specific exact-range/PDC contract
  before enabling local PRE/POST Blind on AAX. Current source fails that product entry closed.
- Complete Windows VST3+AAX installer build, install, same-version reinstall, prior-version upgrade,
  uninstall, and Pro Tools validation.
- Re-run all three existing public distribution channels from the same release commit. AAX Phase A
  does not replace the required macOS Lemon Squeezy, macOS HP, or signed Windows installer outputs.
