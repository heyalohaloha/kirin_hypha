# Kirin Hypha Lemon Squeezy Release Runbook

Purpose: build the Kirin Hypha macOS installer package safely, verify it locally, let Daisuke upload it to Lemon Squeezy, then verify the uploaded Lemon Squeezy file against local release state.

This follows the Kirin OS release style: state JSON is the handoff source, the dry-run script verifies local artifact facts, and Lemon Squeezy upload is performed by Daisuke.

## Files

- Runbook: `docs/ls_release/kirin_hypha_ls_runbook.md`
- State JSON: `release_state/kirin_hypha_1.1.1_ls.state.json`
- Build script: `scripts/ls_release/build_kirin_hypha_pkg.mjs`
- Dry-run script: `scripts/ls_release/kirin_hypha_ls_dry_run.mjs`

## Boundaries

- Do not write to Notion from this repository.
- Do not upload unsigned packages to Lemon Squeezy.
- The signed package requires a `Developer ID Installer` certificate. `Developer ID Application` is sufficient for the plug-in bundles, but not for the installer package.
- Lemon Squeezy displays file sizes as rounded MiB labels. Compare local bytes to `bytes / 1024 / 1024`, rounded to 2 decimals.
- Kirin Hypha product id / variant id were not found locally. Fill `lemonSqueezy.productAdminUrl` in the state before running `--with-ls-chrome`.

## Phase 0: Read State

```bash
sed -n '1,220p' docs/ls_release/kirin_hypha_ls_runbook.md
sed -n '1,220p' release_state/kirin_hypha_1.1.1_ls.state.json
```

Check current state:

```bash
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/kirin_hypha_1.1.1_ls.state.json
```

Before the first signed package exists, this intentionally fails because the `.pkg` artifact and state hashes are not populated.

## Phase 1: Build Source Bundles

If the four source bundles are not already current, rebuild and notarize them:

```bash
cargo run --package xtask -- bundle-universal hypha_pre --release
cargo run --package xtask -- bundle-universal hypha_post --release
cargo run --package xtask -- stamp-egui-version
scripts/build_juce_universal.sh
cargo run --package xtask -- notarize
```

The source ship set is construction-C:

- `juce_shell/build-universal/.../AU/Kirin Hypha PRE.component`
- `juce_shell/build-universal/.../AU/Kirin Hypha POST.component`
- `target/bundled/Kirin Hypha PRE.vst3`
- `target/bundled/Kirin Hypha POST.vst3`

## Phase 2: Build Installer Package

Public release package:

```bash
node scripts/ls_release/build_kirin_hypha_pkg.mjs
```

This writes:

- `dist/LS_UPLOAD/Kirin-Hypha-1.1.1-macOS-Universal.pkg`
- `dist/LS_UPLOAD/Kirin-Hypha-1.1.1-macOS-Universal.pkg.sha256`
- `dist/LS_UPLOAD/Kirin-Hypha-1.1.1-macOS-Universal.pkg.json`

Unsigned smoke package, for payload testing only:

```bash
KIRIN_SKIP_PKG_SIGN=1 KIRIN_SKIP_PKG_NOTARIZE=1 \
  node scripts/ls_release/build_kirin_hypha_pkg.mjs
```

The smoke package is written under `/tmp/kirin_hypha_pkg_smoke/` and is named `UNSIGNED-DO-NOT-UPLOAD`.

## Phase 3: Update State

After a signed package is built, print current artifact facts:

```bash
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/kirin_hypha_1.1.1_ls.state.json \
  --print-artifacts-json
```

Copy the printed `size`, `sha512`, `sha256`, and `lsDisplaySize` values into `release_state/kirin_hypha_1.1.1_ls.state.json`.

## Phase 4: Verify Local Package

```bash
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/kirin_hypha_1.1.1_ls.state.json \
  --with-apple-verification
```

The local verification checks:

- PKG exists.
- Byte size matches state.
- SHA-512 matches state.
- SHA-256 matches state.
- Lemon Squeezy display size matches state.
- `pkgutil --payload-files` includes the four expected plug-in bundles.
- `pkgutil --check-signature` passes.
- `spctl -t install` accepts the package.
- `xcrun stapler validate` passes.

## Phase 5: Daisuke Uploads To Lemon Squeezy

Daisuke uploads only:

- `dist/LS_UPLOAD/Kirin-Hypha-1.1.1-macOS-Universal.pkg`

Do not upload:

- `*-UNSIGNED-DO-NOT-UPLOAD.pkg`
- intermediate component packages
- old zip packages, once the product has moved to installer delivery

## Phase 6: Verify Lemon Squeezy After Upload

After Daisuke uploads the file, Chrome must be logged into Lemon Squeezy and `lemonSqueezy.productAdminUrl` must be set in the state JSON.

```bash
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/kirin_hypha_1.1.1_ls.state.json \
  --with-ls-chrome
```

The Lemon Squeezy check verifies:

- Product page includes `Kirin Hypha`.
- Product page includes `Published`.
- Product page includes the installer file name.
- Product page includes the rounded Lemon Squeezy display size from state.

## Phase 7: Report

Report:

- Local artifact name.
- Local byte size.
- Local SHA-512 and SHA-256.
- Local verification pass/fail.
- Lemon Squeezy post-upload pass/fail.
- Whether commit/push was performed.
