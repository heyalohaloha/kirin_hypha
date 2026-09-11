# Kirin Hypha macOS Universal AAX build and distribution boundary

Date: 2026-09-10

This is the public-repository source of truth for the technical AAX build and packaging path. It
does not contain account identifiers, credentials, private correspondence, or contract terms.

## Current verified state

PRE and POST have been built on macOS as `x86_64 arm64` AAX bundles from the external AAX SDK.
The local build and installed copies passed both PACE `wraptool verify` and Apple
`codesign --verify --deep --strict` on 2026-09-10. This proves the bundle and signing path on that
Mac.

The signed B-786 bundles (`be7b7de4`, version 1.1.49) were then exercised in Pro Tools Ultimate
2026.4.1 on 2026-09-11 in a 48 kHz / 24-bit session. The Intel slice passed plug-in scanning,
PRE/POST Native insertion, session close/reopen, stereo and multi-mono instantiation, PRE/POST
pairing, zero-delay host reporting, and Offline Bounce. The ten-second stereo bounce contained the
same 480,000 samples as the float source; subtracting the 24-bit bounce from the source measured
-144.49 dBFS peak and -148.41 dBFS RMS, which is the expected 24-bit quantisation boundary rather
than evidence of signal processing. No arm64 Pro Tools execution was performed on this Intel Mac.

Pro Tools placed `ePlugInCategory_None` under **Other**. The SDK has no dedicated analyzer or meter
category, so `ePlugInCategory_None` remains the release category unless a later supported-host
matrix provides contrary evidence.

The old B-786 artifact exposed four components per role: stereo/mono Native and stereo/mono
AudioSuite. AudioSuite is not a meaningful surface for a realtime meter and was not part of the
Offline Bounce proof. Current source disables AudioSuite registration while retaining Native mono,
stereo, and multi-mono support. The local PRE/POST Blind product entry also remains disabled on AAX
until exact-range AAX project-clock and PDC behavior has its own proof; ordinary metering and pairing
remain available.

This host evidence belongs to B-786, not to later same-version source. A current candidate must be
built, signed, installed, and retested from its exact commit before release.

## Reproducible build

Keep the SDK outside this GPL repository, then run the explicit diagnostic build:

```bash
bash scripts/build_aax_universal.sh \
  --sdk "$HOME/SDKs/aax-sdk-2-9-0" \
  --license-confirmed \
  --diagnostic
```

When the Kimera App License is still pending, the AAX host surface can be validated without
blocking on the licensed typeface. The command above selects this mode explicitly. It produces
`build-aax-universal/kirin-hypha-macos-aax-diagnostic.json` beside the PRE/POST
bundles. The receipt records the exact source commit/B number, Universal architectures, Native-only
surface, and binary hashes. The bundles are intentionally unsigned and marked
`not_for_distribution: true`; they are suitable only for an explicitly permitted diagnostic host
(such as Pro Tools Developer). Do not copy them into a release package or replace a user's installed
signed plug-in with them.

For local validation in regular Pro Tools Ultimate, the same Kimera-free build can be signed with
PACE and Developer ID while remaining explicitly non-distributable:

```bash
bash scripts/build_aax_universal.sh \
  --sdk "$HOME/SDKs/aax-sdk-2-9-0" \
  --license-confirmed \
  --diagnostic-sign
```

This requires the local PACE/iLok and signing environment, but does not require the Kimera App
License. It writes the same diagnostic receipt with `signed: true`, `pace_verified: true`, and
`apple_signed: true`; notarization is intentionally not performed, so this artifact is for local
Ultimate host testing only. It must never be included in a pkg/zip/installer or treated as a
release candidate.

For the eventual distribution build, run this separately on the release operator's Mac:

```bash
scripts/build_aax_universal.sh \
  --sdk /absolute/external/aax-sdk-root \
  --license-confirmed \
  --kimera-font /absolute/external/kmrwaldenburg-book.otf \
  --kimera-license-confirmed \
  --sign
```

This builds the Rust FFI for both Apple architectures, creates one Universal static library, and
builds only the PRE/POST AAX targets under `build-aax-universal/`. Omitting the Kimera options is
allowed only for a diagnostic build. `--diagnostic` makes unsigned intent explicit, while
`--diagnostic-sign` permits local PACE + Apple signing without Kimera and writes the same
non-distribution receipt. `--sign` requires the licensed font, a clean source
commit with a B number, the exact tracked JUCE patch stack, and the documented PACE and Apple signing
environment. The font, account identifiers, and signer values remain outside the repository and
must not be written to logs.

The entry point removes only its generated PRE/POST AAX product directories before each wrapper
build. This prevents an unsigned diagnostic build from inheriting PACE symlinks or Apple signature
resources left by an earlier signed artifact.

## Packaging

AAX is an opt-in payload so an ordinary GPL checkout can continue to build and package AU/VST3
without possessing the licensed SDK or PACE tools.

```bash
# Lemon Squeezy macOS pkg
node scripts/ls_release/build_kirin_hypha_pkg.mjs --with-aax

# HP macOS zip
cargo run --package xtask -- release-package --with-aax

# All three release channels (matching Windows VST3+AAX artifact required)
node scripts/ls_release/build_kirin_hypha_release_set.mjs \
  --with-aax \
  --windows-installer-dir dist/WINDOWS_CI/KirinHypha-Windows-signed-full
```

Selecting `--with-aax` fails closed unless exactly one PRE and one POST AAX bundle are present and
all of these checks pass:

- expected bundle identifier, executable, version, and AAX package type;
- exact source commit, `clean source` state, licensed Kimera embedding, and Native-only registration;
- `x86_64 arm64` executable;
- Developer ID seal and notarization check;
- PACE compatibility signature symlink exists, resolves inside the bundle, and survives copying;
- PACE `wraptool verify` succeeds;
- the staged copy and the copy extracted back from the final pkg or zip still match the source.

Every AAX directory copy and zip operation uses `ditto`. AAX package creation additionally expands
the finished pkg and verifies the expanded payload before it can be reported as built.

## Public release boundary

AAX is a format inside the existing macOS and Windows deliverables, not a fourth release channel.
Do not publish with `--with-aax` until the current candidate's Pro Tools gates are complete and the
same release commit also produces a verified signed Windows installer containing PRE/POST AAX. The
existing three-channel release rule remains unchanged.
