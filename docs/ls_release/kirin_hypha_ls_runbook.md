# Kirin Hypha Lemon Squeezy Release Runbook

Purpose: build the Kirin Hypha macOS installer package safely, verify it locally, let Daisuke upload it to Lemon Squeezy, then verify the uploaded Lemon Squeezy file against local release state.

This follows the Kirin OS release style: state JSON is the handoff source, the dry-run script verifies local artifact facts, and Lemon Squeezy upload is performed by Daisuke.

## Distribution channels (BOTH updated every release)

Kirin Hypha ships through TWO channels. Updating only one leaves the other on the old version.

1. **Lemon Squeezy (paid)** — the signed/notarized installer `.pkg`, delivered inside the existing Kirin OS / Kirin Sense products. Phases 0–7 below.
2. **HP free download** — `kirinmastering.com/hypha` → "Download for macOS — Free", which links to a GitHub Release `.zip` on `heyalohaloha/kirin_hypha`. See **"HP Free Download Channel"** below. If skipped, free-download users stay on the old (buggy) version.

Both channels reuse the SAME signed+notarized universal bundles from Phase 1 (the `.pkg` and the `.zip` are two packagings of the same bundles).

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
- Kirin Hypha is delivered through the existing Kirin OS and Kirin Sense products, not through a standalone Hypha product for this release:
  - Kirin OS: product `1115751`, variant `1746981`
  - Kirin Sense: product `1120268`, variant `1753806`
- The release operator builds and verifies the package. Daisuke only needs to provide/install the Apple `Developer ID Installer` certificate when missing and perform the Lemon Squeezy browser upload if no authenticated automation is available.

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

## Phase 5: Upload To Lemon Squeezy

Daisuke, or the release operator using Daisuke's authenticated Lemon Squeezy browser session, uploads only:

- `dist/LS_UPLOAD/Kirin-Hypha-1.1.1-macOS-Universal.pkg`

Upload the same package to both existing products:

- Kirin OS: `https://app.lemonsqueezy.com/products/1115751`
- Kirin Sense: `https://app.lemonsqueezy.com/products/1120268`

Do not upload:

- `*-UNSIGNED-DO-NOT-UPLOAD.pkg`
- intermediate component packages
- old zip packages, once the product has moved to installer delivery

## Phase 6: Verify Lemon Squeezy After Upload

After the upload, Chrome must be logged into Lemon Squeezy. The state JSON contains the Kirin OS and Kirin Sense product admin URLs under `lemonSqueezy.products[]`.

```bash
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/kirin_hypha_1.1.1_ls.state.json \
  --with-ls-chrome
```

The Lemon Squeezy check verifies:

- Product pages include `Kirin OS` and `Kirin Sense`.
- Product pages include `Published`.
- Product page includes the installer file name.
- Product page includes the rounded Lemon Squeezy display size from state.

## HP Free Download Channel (GitHub Release + Vercel)

The homepage `kirinmastering.com/hypha` has a "Download for macOS — Free" button that links to a GitHub Release asset:
`https://github.com/heyalohaloha/kirin_hypha/releases/download/vX.Y.Z/Kirin-Hypha-X.Y.Z-macOS-Universal.zip`

This is a SEPARATE channel from Lemon Squeezy and MUST be updated on every release. Run these AFTER Phase 1 (the bundles are already signed+notarized — the `.zip` reuses them).

### HP-1: Build the free `.zip` (signed bundles → universal zip)

```bash
cargo run --package xtask -- release-package
# -> dist/Kirin-Hypha-X.Y.Z-macOS-Universal.zip (+ .zip.sha256, release-manifest.json)
# verify_sources refuses ad-hoc/unsigned bundles, so this only succeeds after `notarize`.
```

### HP-2: Create the GitHub Release (the URL the HP links to)

```bash
gh release create vX.Y.Z --repo heyalohaloha/kirin_hypha --target main \
  --title "Kirin Hypha X.Y.Z" \
  --notes "..." \
  dist/Kirin-Hypha-X.Y.Z-macOS-Universal.zip \
  dist/Kirin-Hypha-X.Y.Z-macOS-Universal.zip.sha256

# verify the asset URL resolves (must be HTTP 200 before the HP goes live):
curl -sIL -o /dev/null -w '%{http_code}\n' \
  "https://github.com/heyalohaloha/kirin_hypha/releases/download/vX.Y.Z/Kirin-Hypha-X.Y.Z-macOS-Universal.zip"
```

### HP-3: Bump the HP download links (repo `~/Dev/kirin_hp`)

Update BOTH files (EN + JA) from the old `vN/...N...zip` to the new `vX.Y.Z/...X.Y.Z...zip`:

- `hypha.html` (EN, the `Download for macOS — Free` link)
- `ja/hypha.html` (JA, the `macOS版を無料ダウンロード` link)

```bash
cd ~/Dev/kirin_hp && git add hypha.html ja/hypha.html && git commit -m "hypha: bump free download link -> vX.Y.Z"
```

### HP-4: Deploy the HP (Daisuke)

`kirin_hp` has no git remote — deploy is manual Vercel:

```bash
cd ~/Dev/kirin_hp && vercel --prod
```

**Order matters:** create the GitHub Release (HP-2) BEFORE deploying (HP-4) so the live page's link is not a 404.

## Phase 7: Report

Report:

- Local artifact name.
- Local byte size.
- Local SHA-512 and SHA-256.
- Local verification pass/fail.
- Lemon Squeezy post-upload pass/fail.
- Whether commit/push was performed.
