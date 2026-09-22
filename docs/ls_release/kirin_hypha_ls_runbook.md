# Kirin Hypha Lemon Squeezy Release Runbook

Purpose: build the Kirin Hypha macOS installer package safely, verify it locally, let an authorized release operator upload it to Lemon Squeezy, then verify the uploaded file against local release state.

Release-operator state is local-only and ignored by Git. Public artifact facts are emitted as `.json` and `.sha256` sidecars under `dist/` and published with the corresponding GitHub Release.

## Distribution channels (ALL updated every release)

Kirin Hypha ships through three release surfaces. Updating only one leaves the others on the old version.

1. **Lemon Squeezy (paid)** — the signed/notarized installer `.pkg`, delivered inside the existing Kirin OS / Kirin Sense products. Phases 0–7 below.
2. **HP free download** — `kirinmastering.com/hypha` → "Download for macOS — Free", which links to a GitHub Release `.zip` on `heyalohaloha/kirin_hypha`. See **"HP Free Download Channel"** below. If skipped, free-download users stay on the old (buggy) version.
3. **Windows** — one Authenticode-signed Inno Setup `.exe` containing PRE and POST. Public CI builds
   and validates the VST3 source artifact; the private, approved Windows signing hosts produce and
   verify the signed installer. AAX releases add verified PACE+Authenticode AAX to the same
   installer; the manual VST3 `.zip` is fallback-only. See **"Windows VST3 Channel"** below.

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
- Windows is part of the release set. If the current signed installer artifact, all required signed
  payload surfaces, CI install/uninstall result, or external DAW validation is missing, the release
  is blocked instead of silently shipping macOS only. An AAX release requires PRE/POST AAX on both
  operating systems from the same commit.

## Validation-first source order

Do not select a version, upload, tag, or publish while the product commit is still changing. Signing
and notarization of a non-public candidate are validation steps, not publication approval. Use this
order for every release:

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
   macOS/AU CI, and Windows CI/pluginval for that exact commit and version. This commit, not its
   pre-integration parent, owns the release evidence.
5. Build the non-public signed/notarized candidates from that exact commit. This includes the macOS
   AU/VST3 bundles and the signed Windows installer. When AAX is selected for the release, it also
   includes the macOS and Windows PACE-signed AAX pairs. Record every artifact hash before host
   validation. Do not upload these candidates to a public channel yet.
6. Run the required macOS and Windows DAW/Pro Tools checks against those candidates. The Windows
   factory must also run install, same-version reinstall, prior-public-version upgrade, uninstall,
   pluginval, signature, and payload-hash verification against the exact signed installer. Do not
   substitute an older commit's evidence or an unsigned binary where the final signed binary is
   required.
7. After every release-candidate gate is green, package and publish all three channels using the
   already verified signed bundle/installer hashes. Do not rebuild, resign, change source, or change
   version after validation; any byte change creates a new candidate and repeats the affected gates.

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

For an AAX release, select the format at the top-level command so it is propagated to both macOS
packages and enforced against the Windows manifest:

```bash
node scripts/ls_release/build_kirin_hypha_release_set.mjs \
  --with-aax \
  --windows-installer-dir dist/WINDOWS_CI/KirinHypha-Windows-signed-full
```

The command rejects a Windows AAX installer when `--with-aax` was omitted, and rejects a VST3-only
installer when it was selected. macOS AAX bundles must carry the exact current commit, `clean
source`, explicit distribution intent, and Native-only stamps. Kimera is optional and its absence
does not block release. Windows AAX must carry the equivalent signed
provenance sidecar, whose hash and PRE/POST hashes are bound into the installer manifest. In both
cases, same-version bundles from an older commit cannot be packaged.

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

After the Pro Tools release gates are complete, the same macOS deliverables may also contain the
separately built PRE/POST AAX bundles from `build-aax-universal/`. AAX remains opt-in so the normal
GPL checkout does not require the external SDK or PACE tools. It is a format inside the existing
macOS and Windows deliverables, not a fourth release channel.

The AAX release build must be created through `scripts/build_aax_universal.sh --sign`. That command
requires the exact Kirin PACE and Developer ID identities, freezes the signed PRE/POST pair into a
hash-addressed archive with a complete content manifest, and submits that archive to Apple. It
writes `build-aax-universal/kirin-hypha-macos-aax-notarization.json` only after `notarytool submit`,
`notarytool info`, and the preserved `notarytool log` agree on the Accepted job and archive name.
The archive, manifest, and log remain under `build-aax-universal/aax-notarization/<sha256>/`.
A v1, signature-only, or diagnostic receipt is never accepted by either macOS packaging path.

The HP zip also carries an exact copy of the accepted receipt at
`AAX/kirin-hypha-macos-aax-notarization.json`. Post-extraction verification rejects a missing or
different copy, so the public artifact retains the notarization submission and PRE/POST hash binding.

## Phase 2: Build Installer Package

Public release package:

```bash
node scripts/ls_release/build_kirin_hypha_pkg.mjs
```

Release candidate including AAX:

```bash
node scripts/ls_release/build_kirin_hypha_pkg.mjs --with-aax
```

The four source, installed, archive, executable, display-name, and VST3 CID contracts come from
`config/hypha_macos_ship_bundles.json`. Before `pkgbuild`, the script verifies the exact payload
layout—including the role-first VST3 outer names—against each bundle's `CFBundleExecutable`.
With `--with-aax`, the separate AAX manifest requires exactly PRE and POST, verifies the exact source
commit, clean-source/distribution/Native-only stamps, a recorded optional Kimera state, Universal architecture, exact Apple and PACE signer
identities, secure timestamp, accepted notarization archive/log receipt, and PACE symlink integrity.
It reconfirms the receipt online with `notarytool info` and `notarytool log`, materializes AAX only
from the verified submitted archive, expands the finished pkg, and compares the complete bundle
trees again. It never rereads AAX payload from the mutable build directory. Every AAX directory copy uses
`ditto`; the final pkg is separately submitted to Apple and stapled as the distributed outer
container.

This writes:

- `dist/LS_UPLOAD/Kirin-Hypha-X.Y.Z-macOS-Universal.pkg`
- `dist/LS_UPLOAD/Kirin-Hypha-X.Y.Z-macOS-Universal.pkg.sha256`
- `dist/LS_UPLOAD/Kirin-Hypha-X.Y.Z-macOS-Universal.pkg.json`

Unsigned smoke package, for payload testing only:

```bash
KIRIN_SKIP_PKG_SIGN=1 KIRIN_SKIP_PKG_NOTARIZE=1 \
  node scripts/ls_release/build_kirin_hypha_pkg.mjs --with-aax
```

The smoke package is written under `/tmp/kirin_hypha_pkg_smoke/` and is named `UNSIGNED-DO-NOT-UPLOAD`.
The outer smoke pkg is unsigned, but included AAX bundles must still pass both PACE and Apple gates.

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

After the Pro Tools and same-commit Windows AAX gates are complete, add `--with-aax`. The command
uses `ditto` to stage and create the zip, extracts the completed zip, and re-runs the exact AAX
signature, identity, hash, accepted-notarization-receipt, and symlink gates.

Do not publish a macOS AAX artifact while the matching Windows installer lacks PRE/POST AAX. When
using `--with-aax`, add both AAX install paths to the ignored release state's `expectedPayloads` so
the Lemon Squeezy dry run also checks the pkg payload listing.

### HP-2: Create the GitHub Release (the URL the HP links to)

```bash
RELEASE_COMMIT=<exact-40-character-release-commit>
test "$(git rev-parse "$RELEASE_COMMIT^{commit}")" = "$RELEASE_COMMIT"

gh release create vX.Y.Z --repo heyalohaloha/kirin_hypha --target "$RELEASE_COMMIT" \
  --title "Kirin Hypha X.Y.Z" \
  --notes "..." \
  dist/Kirin-Hypha-X.Y.Z-macOS-Universal.zip \
  dist/Kirin-Hypha-X.Y.Z-macOS-Universal.zip.sha256

# A new tag must resolve to the immutable release commit, never merely to the
# branch that happened to be current when this command ran.
test "$(git ls-remote origin refs/tags/vX.Y.Z | cut -f1)" = "$RELEASE_COMMIT"

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
For an AAX release, first dispatch `hypha-aax-signing.yml` in the private Kirin release-control
repository and record its successful `KirinHypha-Windows-AAX-signed` run ID. A VST3-only release
does not dispatch the AAX workflow and leaves `signed_aax_run_id` empty.

Then dispatch `hypha-windows-signing.yml` in the private Kirin release-control repository with:

```text
hypha_commit=<exact 40-character commit>
hypha_ci_run_id=<green Hypha CI run ID>
signed_aax_run_id=<successful AAX signing run ID, or empty for VST3-only>
signed_candidate_run_id=
external_validation=pending
external_validation_report_sha256=
```

This produces `KirinHypha-Windows-signed-candidate`. Run
`docs/windows_external_validation.md` against that exact signed installer, retain the report, and
record its SHA-256. Do not rebuild or re-sign after the DAW test. Dispatch the same workflow again:

```text
hypha_commit=<same exact 40-character commit>
hypha_ci_run_id=<same green Hypha CI run ID>
signed_aax_run_id=<same AAX signing run ID, or empty for VST3-only>
signed_candidate_run_id=<successful signed-candidate workflow run ID>
external_validation=complete
external_validation_report_sha256=<lowercase SHA-256 of the retained validation report>
```

The promotion job downloads the exact candidate, verifies its hash, source identity, signature and
pending state, then changes only the JSON sidecar. It does not rebuild or re-sign the installer. The factory
rejects a CI run whose commit or conclusion does not match, keeps the four `ESIGNER_*` secrets out of
this public GPL repository, requires all three complete Hypha CI jobs, rejects distribution scripts
outside its private SHA-256 allowlist, downloads pinned CodeSignTool bytes, uses a verified immutable
Inno Setup release, and signs:

- PRE VST3 PE binary
- POST VST3 PE binary
- generated uninstaller
- Setup EXE

It first installs the previous public signed installer, upgrades it with the candidate, installs the
candidate a second time, compares installed payload hashes, verifies all signatures, silently
uninstalls, checks registry cleanup, and proves unrelated VST3 (and, when selected, AAX) sentinels
were not removed. VST3-only signing runs on the hosted Windows runner. AAX uses the approved
`hypha-aax-build` and `hypha-aax-sign` self-hosted runners because PACE verification is required.

### WIN-2: Required artifacts

Download `KirinHypha-Windows-signed-full`. It must contain exactly:

- `Kirin-Hypha-X.Y.Z-Windows-x64-Setup.exe`
- matching `.exe.sha256`
- matching `.exe.json`

For an AAX release it must additionally contain the matching
`Kirin-Hypha-X.Y.Z-Windows-x64-AAX.json` signed-provenance sidecar. A VST3-only release must not
claim AAX in the installer manifest.

The JSON must report signing `valid`, CI validation `passed`, external validation `complete`, the
tested installer's identical SHA-256, retained report SHA-256, signed-candidate workflow URL, and
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
