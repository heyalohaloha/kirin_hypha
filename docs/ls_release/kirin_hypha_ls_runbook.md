# Kirin Hypha Lemon Squeezy Release Runbook

Purpose: build the Kirin Hypha macOS installer package safely, verify it locally, let an authorized release operator upload it to Lemon Squeezy, then verify the uploaded file against local release state.

Release-operator state is local-only and ignored by Git. Public artifact facts are emitted as `.json` and `.sha256` sidecars under `dist/` and published with the corresponding GitHub Release.

## Distribution channels (ALL updated every release)

Kirin Hypha ships through three release surfaces. Updating only one leaves the others on the old version.

1. **Lemon Squeezy (paid)** — the signed/notarized installer `.pkg`, delivered inside the existing Kirin OS / Kirin Sense products. Phases 0–7 below.
2. **HP free download** — `kirinmastering.com/hypha` → "Download for macOS — Free", which links to a GitHub Release `.zip` on `heyalohaloha/kirin_hypha`. See **"HP Free Download Channel"** below. If skipped, free-download users stay on the old (buggy) version.
3. **Windows VST3** — one Authenticode-signed Inno Setup `.exe` containing PRE and POST, built and
   installed/uninstalled on `windows-latest`. The manual `.zip` is fallback-only. See
   **"Windows VST3 Channel"** below.

The macOS paid/free channels reuse the SAME signed+notarized universal bundles from Phase 1 (the `.pkg` and the `.zip` are two packagings of the same bundles). Windows uses the JUCE VST3 output from the Windows CI job.

## Files

- Runbook: `docs/ls_release/kirin_hypha_ls_runbook.md`
- Local state template: `docs/ls_release/kirin_hypha_ls_state.example.json`
- Local state: `release_state/kirin_hypha_X.Y.Z_ls.state.json` (ignored; never commit)
- Build script: `scripts/ls_release/build_kirin_hypha_pkg.mjs`
- Dry-run script: `scripts/ls_release/kirin_hypha_ls_dry_run.mjs`
- Full release set script: `scripts/ls_release/build_kirin_hypha_release_set.mjs`
- Windows installer build: `scripts/windows/build-installer.mjs`
- Windows installer verification: `scripts/windows/verify-installer.ps1`
- Windows fallback ZIP: `scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs`

## Boundaries

- Do not write to Notion from this repository.
- Do not upload unsigned packages to Lemon Squeezy.
- The signed package requires a `Developer ID Installer` certificate. `Developer ID Application` is sufficient for the plug-in bundles, but not for the installer package.
- Lemon Squeezy displays file sizes as rounded MiB labels. Compare local bytes to `bytes / 1024 / 1024`, rounded to 2 decimals.
- Kirin Hypha is delivered through configured existing products, not through a new standalone product unless the distribution policy changes.
- Product IDs, variant IDs, admin URLs, upload readiness, and operator notes belong only in the ignored local state file.
- The release operator builds and verifies the package, provides the Apple `Developer ID Installer` certificate when needed, and performs the browser upload if no authenticated automation is available.
- Windows is part of the release set. If the current signed installer artifact, all four valid
  Authenticode surfaces, CI install/uninstall result, or external DAW validation is missing, the
  release is blocked instead of silently shipping macOS only.

## Validation-first source order

Do not select a version, build release artifacts, sign, notarize, upload, tag, or publish while the
product commit is still under validation. Use this order for every release:

1. Record the candidate product branch and its exact 40-character commit. Treat a later commit as a
   new candidate that must repeat the preliminary product gates.
2. Validate that product commit locally before starting release integration. Do not reuse evidence
   from an older product commit.
3. Integrate the distribution-procedure commit into the validated product line, choose the next
   unused version, and assign the integration commit its unique B number. The integration is required
   because the pinned signing factory executes `scripts/windows/build-installer.mjs` from the checked-out
   Hypha source. If any of the four trusted distribution files changed, review them and update the
   private factory's SHA-256 allowlist in the same integration step.
4. Record the resulting integration commit as the release candidate. Run the complete local suites,
   macOS/AU CI, Windows CI/pluginval, and dedicated-machine Windows DAW validation for that exact
   commit and version. This commit, not its pre-integration parent, owns the release evidence.
5. Only after every release-candidate gate is green, build, sign, notarize, package, and publish all
   three distribution channels from that same commit. Do not change source or version after validation.

If any exact commit, CI run ID, external-validation receipt, or artifact hash differs between steps,
stop and restart from the affected validation step. Never advance a release by branch name alone.

## Immutable Release Provenance

Published artifacts, their filenames, checksums, signatures, and embedded manifests remain unchanged after publication. If necessary repository maintenance rewrites a release commit:

1. Compare the original artifact commit with the rewritten commit across every tracked path outside the intentionally removed path.
2. Proceed only when that comparison is tree-identical.
3. Add the original artifact commit, current public commit, removed path, verification result, and canonical filtered-tree digest to `docs/release_commit_map.json`. The digest format is `git-ls-tree-r-z-v1`: SHA-256 over the NUL-terminated records emitted by `git ls-tree -r -z --full-tree <commit>` after removing records whose path starts with the declared excluded prefix.
4. Add the same mapping to the existing GitHub Release notes.
5. Recheck the published asset hashes, signatures, and notarization without replacing the assets.

Do not rename, regenerate, or re-upload an existing artifact merely to make its embedded commit match rewritten history. The original commit is part of the artifact's immutable build record; the additive mapping preserves the audit trail.

## One Script Release Set

After the macOS source bundles are built/notarized and the latest green CI artifact
`KirinHypha-Windows-signed-full` has been downloaded, run:

```bash
node scripts/ls_release/build_kirin_hypha_release_set.mjs \
  --windows-installer-dir dist/WINDOWS_CI/KirinHypha-Windows-signed-full
```

This verifies the downloaded signed Windows installer and its sidecars, runs Windows static gates,
builds the macOS LS `.pkg`, and builds the macOS HP `.zip`.

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

The primary Windows package is `Kirin-Hypha-X.Y.Z-Windows-x64-Setup.exe`. It installs PRE and POST
to Steinberg's standard per-user VST3 directory by default, supports an explicit all-users choice,
and registers one product uninstaller without owning the shared VST3 root.

### WIN-1: CI build and signing

First dispatch this repository's `.github/workflows/ci.yml` with
`windows_signing=unsigned`. Record the completed green run ID and its exact 40-character commit.
Then dispatch `hypha-windows-signing.yml` in the private Kirin release-control repository with:

```text
hypha_commit=<exact 40-character commit>
hypha_ci_run_id=<green Hypha CI run ID>
external_validation=complete
```

Use `complete` only when `docs/windows_external_validation.md` is green for the source. The factory
rejects a CI run whose commit or conclusion does not match, keeps the four `ESIGNER_*` secrets out of
this public GPL repository, requires all three complete Hypha CI jobs, rejects distribution scripts
outside its private SHA-256 allowlist, downloads pinned CodeSignTool bytes, uses a verified immutable
Inno Setup release, and signs:

- PRE VST3 PE binary
- POST VST3 PE binary
- generated uninstaller
- Setup EXE

It then installs the EXE twice for the current user, compares installed payload hashes, verifies all
signatures, silently uninstalls, checks registry cleanup, and proves an unrelated VST3 sentinel was
not removed.

### WIN-2: Required artifacts

Download `KirinHypha-Windows-signed-full`. It must contain exactly:

- `Kirin-Hypha-X.Y.Z-Windows-x64-Setup.exe`
- matching `.exe.sha256`
- matching `.exe.json`

The JSON must report signing `valid`, CI validation `passed`, external validation `complete`, and
`distribution.public_ready=true`. The one-script release set rejects any weaker state.

The `kirin-hypha-windows-vst3-ls-package` artifact is a fallback-only manual ZIP. Its schema is
`kirin-hypha-windows-vst3-fallback-artifact-v3`; neither complete external validation nor signed
embedded binaries promote the ZIP to the primary distribution.

## Phase 7: Report

Report:

- Local artifact name.
- Local byte size.
- Local SHA-512 and SHA-256.
- Local verification pass/fail.
- Lemon Squeezy post-upload pass/fail.
- Whether commit/push was performed.
