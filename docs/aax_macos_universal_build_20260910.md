# Kirin Hypha macOS Universal AAX build and distribution boundary

Date: 2026-09-10

This is the public-repository source of truth for the technical AAX build and packaging path. It
does not contain account identifiers, credentials, private correspondence, or contract terms.

## Current verified state

PRE and POST have been built on macOS as `x86_64 arm64` AAX bundles from the external AAX SDK.
The local build and installed copies passed both PACE `wraptool verify` and Apple
`codesign --verify --deep --strict` on 2026-09-10. This proves the bundle and signing path on that
Mac; it does not yet claim Pro Tools product support.

`AAX_CATEGORY` remains `ePlugInCategory_None` until the licensed Pro Tools validation session can
confirm the correct insert-menu category. Pro Tools loading, A-path transparency, Offline Bounce,
state restore, and PRE/POST pairing remain release gates.

## Reproducible build

Keep the SDK outside this GPL repository, then run:

```bash
scripts/build_aax_universal.sh \
  --sdk /absolute/external/aax-sdk-root \
  --license-confirmed
```

This builds the Rust FFI for both Apple architectures, creates one Universal static library, and
builds only the PRE/POST AAX targets under `build-aax-universal/`. Add `--sign` only on the release
operator's Mac with the documented PACE and Apple signing environment configured. No account or
signer values belong in scripts, logs, or the repository.

## Packaging

AAX is an opt-in payload so an ordinary GPL checkout can continue to build and package AU/VST3
without possessing the licensed SDK or PACE tools.

```bash
# Lemon Squeezy macOS pkg
node scripts/ls_release/build_kirin_hypha_pkg.mjs --with-aax

# HP macOS zip
cargo run --package xtask -- release-package --with-aax
```

Selecting `--with-aax` fails closed unless exactly one PRE and one POST AAX bundle are present and
all of these checks pass:

- expected bundle identifier, executable, version, and AAX package type;
- `x86_64 arm64` executable;
- Developer ID seal and notarization check;
- PACE compatibility signature symlink exists, resolves inside the bundle, and survives copying;
- PACE `wraptool verify` succeeds;
- the staged copy and the copy extracted back from the final pkg or zip still match the source.

Every AAX directory copy and zip operation uses `ditto`. AAX package creation additionally expands
the finished pkg and verifies the expanded payload before it can be reported as built.

## Public release boundary

AAX is a format inside the existing macOS and Windows deliverables, not a fourth release channel.
Do not publish with `--with-aax` until the Pro Tools gates are complete and the same release commit
also produces a verified signed Windows installer containing PRE/POST AAX. The existing three-channel
release rule remains unchanged.
