# Kirin Hypha AAX Phase A readiness

Date: 2026-09-07

This document records SDK-independent preparation only. It does not claim that Kirin Hypha has
been built, signed, or tested as an AAX plug-in.

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

The current category candidate is JUCE's `ePlugInCategory_None`, which is JUCE's non-synth default.
It must be checked against the
licensed SDK and in Pro Tools before it becomes a release decision.

## CI boundary

`.github/workflows/aax-phase-a.yml` always runs the SDK-absence guard. Its build matrix is skipped
on normal pushes and pull requests and on the default manual invocation. A manual operator must
explicitly request the build, confirm the license, provide the external path, and use SDK-equipped
self-hosted macOS and Windows runners. The two runner paths are separate workflow inputs because
their filesystem syntax and SDK installation locations differ.

## Remaining external gates

- Obtain the licensed AAX SDK and confirm its exact version and supported toolchains.
- Configure and build PRE/POST on SDK-equipped macOS and Windows hosts.
- Verify identifiers, category, channel layouts, bypass, latency, state restore, and Offline Bounce
  in the supported Pro Tools versions.
- Complete PACE signing requirements and installer/package validation.
- Re-run all three existing public distribution channels from the same release commit. AAX Phase A
  does not replace the required macOS Lemon Squeezy, macOS HP, or signed Windows installer outputs.
