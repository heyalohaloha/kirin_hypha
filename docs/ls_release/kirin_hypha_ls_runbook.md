# Kirin Hypha Lemon Squeezy Release Runbook

Purpose: build the Kirin Hypha macOS installer package safely, verify it locally, let an authorized release operator upload it to Lemon Squeezy, then verify the uploaded file against local release state.

Release-operator state is local-only and ignored by Git. Public artifact facts are emitted as `.json` and `.sha256` sidecars under `dist/` and published with the corresponding GitHub Release.

## Distribution channels (ALL updated every release)

Kirin Hypha ships through three release surfaces. Updating only one leaves the others on the old version.

1. **Lemon Squeezy (paid)** — the signed/notarized installer `.pkg`, delivered inside the existing Kirin OS / Kirin Sense products. Phases 0–7 below.
2. **HP free download** — `kirinmastering.com/hypha` → "Download for macOS — Free", which links to a GitHub Release `.zip` on `heyalohaloha/kirin_hypha`. See **"HP Free Download Channel"** below. If skipped, free-download users stay on the old (buggy) version.
3. **Windows VST3** — a manual PRE/POST VST3 `.zip` built from the green `windows-latest` artifact. It is not a Windows installer and is not Authenticode-signed. See **"Windows VST3 Channel"** below.

The macOS paid/free channels reuse the SAME signed+notarized universal bundles from Phase 1 (the `.pkg` and the `.zip` are two packagings of the same bundles). Windows uses the JUCE VST3 output from the Windows CI job.

## Files

- Runbook: `docs/ls_release/kirin_hypha_ls_runbook.md`
- Local state template: `docs/ls_release/kirin_hypha_ls_state.example.json`
- Local state: `release_state/kirin_hypha_X.Y.Z_ls.state.json` (ignored; never commit)
- Build script: `scripts/ls_release/build_kirin_hypha_pkg.mjs`
- Dry-run script: `scripts/ls_release/kirin_hypha_ls_dry_run.mjs`
- Full release set script: `scripts/ls_release/build_kirin_hypha_release_set.mjs`
- Windows VST3 package script: `scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs`

## Boundaries

- Do not write to Notion from this repository.
- Do not upload unsigned packages to Lemon Squeezy.
- The signed package requires a `Developer ID Installer` certificate. `Developer ID Application` is sufficient for the plug-in bundles, but not for the installer package.
- Lemon Squeezy displays file sizes as rounded MiB labels. Compare local bytes to `bytes / 1024 / 1024`, rounded to 2 decimals.
- Kirin Hypha is delivered through configured existing products, not through a new standalone product unless the distribution policy changes.
- Product IDs, variant IDs, admin URLs, upload readiness, and operator notes belong only in the ignored local state file.
- The release operator builds and verifies the package, provides the Apple `Developer ID Installer` certificate when needed, and performs the browser upload if no authenticated automation is available.
- Windows is part of the release set. If the current Windows artifact is not present, the release is blocked instead of silently shipping macOS only.

## Immutable Release Provenance

Published artifacts, their filenames, checksums, signatures, and embedded manifests remain unchanged after publication. If necessary repository maintenance rewrites a release commit:

1. Compare the original artifact commit with the rewritten commit across every tracked path outside the intentionally removed path.
2. Proceed only when that comparison is tree-identical.
3. Add the original artifact commit, current public commit, removed path, verification result, and canonical filtered-tree digest to `docs/release_commit_map.json`. The digest format is `git-ls-tree-r-z-v1`: SHA-256 over the NUL-terminated records emitted by `git ls-tree -r -z --full-tree <commit>` after removing records whose path starts with the declared excluded prefix.
4. Add the same mapping to the existing GitHub Release notes.
5. Recheck the published asset hashes, signatures, and notarization without replacing the assets.

Do not rename, regenerate, or re-upload an existing artifact merely to make its embedded commit match rewritten history. The original commit is part of the artifact's immutable build record; the additive mapping preserves the audit trail.

## One Script Release Set

After the macOS source bundles are built/notarized and the latest green Windows CI artifact `kirin-hypha-windows-vst3` has been downloaded, run:

```bash
node scripts/ls_release/build_kirin_hypha_release_set.mjs \
  --windows-artifact-dir dist/WINDOWS_CI/kirin-hypha-windows-vst3 \
  --windows-external-validation complete
```

This runs Windows readiness/preflight, builds the Windows VST3 zip/state, builds the macOS LS `.pkg`, and builds the macOS HP `.zip`.

If the Windows artifact is missing, the script fails before reporting release ready. Do not use `--skip-windows-package` for a public release.

## Phase 0: Read State

```bash
sed -n '1,220p' docs/ls_release/kirin_hypha_ls_runbook.md
mkdir -p release_state
cp docs/ls_release/kirin_hypha_ls_state.example.json \
  release_state/kirin_hypha_X.Y.Z_ls.state.json
```

Replace `X.Y.Z` and populate the local artifact and product-target fields. Check current state:

```bash
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/kirin_hypha_X.Y.Z_ls.state.json
```

Before the first signed package exists, this intentionally fails because the `.pkg` artifact and state hashes are not populated.

## Phase 1: Build Source Bundles

If the four source bundles are not already current, rebuild and notarize them:

```bash
scripts/build_juce_universal.sh
cargo run --package xtask -- notarize
```

The macOS source ship set is the JUCE common shell in both formats:

- `juce_shell/build-universal/.../AU/Kirin Hypha PRE.component`
- `juce_shell/build-universal/.../AU/Kirin Hypha POST.component`
- `juce_shell/build-universal/.../VST3/Kirin Hypha PRE.vst3`
- `juce_shell/build-universal/.../VST3/Kirin Hypha POST.vst3`

## Phase 2: Build Installer Package

Public release package:

```bash
node scripts/ls_release/build_kirin_hypha_pkg.mjs
```

The four source, installed, archive, executable, display-name, and VST3 CID contracts come from
`config/hypha_macos_ship_bundles.json`. Before `pkgbuild`, the script verifies the exact payload
layout—including the role-first VST3 outer names—against each bundle's `CFBundleExecutable`.

This writes:

- `dist/LS_UPLOAD/Kirin-Hypha-X.Y.Z-macOS-Universal.pkg`
- `dist/LS_UPLOAD/Kirin-Hypha-X.Y.Z-macOS-Universal.pkg.sha256`
- `dist/LS_UPLOAD/Kirin-Hypha-X.Y.Z-macOS-Universal.pkg.json`

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
  --state release_state/kirin_hypha_X.Y.Z_ls.state.json \
  --print-artifacts-json
```

Copy the printed `size`, `sha512`, `sha256`, and `lsDisplaySize` values into the ignored local state file. These artifact facts must match the generated public `.pkg.json` sidecar.

## Phase 4: Verify Local Package

```bash
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/kirin_hypha_X.Y.Z_ls.state.json \
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

The authorized release operator uploads only:

- `dist/LS_UPLOAD/Kirin-Hypha-X.Y.Z-macOS-Universal.pkg`

Upload the package to every product configured in the ignored local state. Do not copy product IDs or admin URLs into tracked documentation.

Do not upload:

- `*-UNSIGNED-DO-NOT-UPLOAD.pkg`
- intermediate component packages
- old zip packages, once the product has moved to installer delivery

## Phase 6: Verify Lemon Squeezy After Upload

After the upload, Chrome must be logged into Lemon Squeezy. The local state JSON contains the configured product admin URLs under `lemonSqueezy.products[]`.

```bash
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/kirin_hypha_X.Y.Z_ls.state.json \
  --with-ls-chrome
```

The Lemon Squeezy check verifies:

- Product pages include every configured product name.
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

### HP-3: Bump the website download links

Update BOTH files (EN + JA) from the old `vN/...N...zip` to the new `vX.Y.Z/...X.Y.Z...zip`:

- `hypha.html` (EN, the `Download for macOS — Free` link)
- `ja/hypha.html` (JA, the `macOS版を無料ダウンロード` link)

The website repository and deployment credentials are maintained separately from this public source repository. Update both language variants through that repository's documented release workflow.

### HP-4: Deploy the website

Use the website repository's authorized production deployment workflow.

**Order matters:** create the GitHub Release (HP-2) BEFORE deploying (HP-4) so the live page's link is not a 404.

## Windows VST3 Channel

The Windows package is built from the GitHub Actions artifact `kirin-hypha-windows-vst3` after the Windows job has passed build, artifact verification, and pluginval.

### WIN-1: CI package artifact

The Windows CI job now also runs:

```bash
node scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs \
  --artifact-dir juce_shell/build-windows \
  --output-dir dist/WINDOWS_CI \
  --release-kind ci \
  --external-validation pending
```

It uploads:

- `kirin-hypha-windows-vst3` — raw PRE/POST VST3 bundles
- `kirin-hypha-windows-vst3-ls-package` — packaged Windows VST3 zip + `.sha256` + `.json`

### WIN-2: Local LS candidate from downloaded artifact

Download/extract `kirin-hypha-windows-vst3` to `dist/WINDOWS_CI/kirin-hypha-windows-vst3`, then run:

```bash
node scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs \
  --artifact-dir dist/WINDOWS_CI/kirin-hypha-windows-vst3 \
  --release-kind ls \
  --external-validation complete
```

This writes:

- `dist/WINDOWS_LS/Kirin-Hypha-X.Y.Z-Windows-VST3-BNNN-<commit>.zip`
- `dist/WINDOWS_LS/Kirin-Hypha-X.Y.Z-Windows-VST3-BNNN-<commit>.zip.sha256`
- `dist/WINDOWS_LS/Kirin-Hypha-X.Y.Z-Windows-VST3-BNNN-<commit>.zip.json`
- `release_state/kirin_hypha_X.Y.Z_windows_ls_bNNN.state.json` (ignored local workflow state)

The public `.zip.json` uses schema `kirin-hypha-windows-vst3-artifact-v2` and contains artifact identity, hashes, source commit, limitations, and validation facts. Schema v2 removes the operator-only `release_status` and `ls_upload` fields from legacy schema v1 and uses `complete` or `pending` for external validation. Upload readiness exists only in the ignored local state. If Windows external validation is not complete, use `--external-validation pending`; the local state will be generated as a blocker and must not be reported as LS-ready.

## Phase 7: Report

Report:

- Local artifact name.
- Local byte size.
- Local SHA-512 and SHA-256.
- Local verification pass/fail.
- Lemon Squeezy post-upload pass/fail.
- Whether commit/push was performed.
