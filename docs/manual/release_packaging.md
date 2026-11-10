# Maintainer release packaging

How maintainers produce the signed macOS PKG, the recovery ZIP and the signed Windows installer
for a release. Signing credentials stay outside this repository; the release entry and the
distribution gate are the procedures to follow.

[Back to the README](../../README.md#the-full-manual)

Start with the [release entry](../hypha_release_entry.md) and the
[AAX build and signing guide](../aax_build_signing_entry.md). The unified command coordinates
Mac signing/notarization/PACE, the reviewed Windows factory, exact-candidate acceptance, package
creation and the three publication channels:

```bash
node scripts/build_hypha.mjs --release --help
```

The full release contains six Mac bundles (PRE/POST × AU/VST3/AAX) and four Windows bundles
(PRE/POST × VST3/AAX). The coordinator uses the existing release-set producer to prepare the
signed/notarized macOS PKG, the same-release recovery ZIP and the qualified Windows installer.
Inspect and resume the private profile according to the release guide; do not substitute the
legacy VST3-only packaging command for this full release.

`release_state/` is deliberately ignored: it contains release-operator targets and upload readiness, not source. Fill the local state from the generated `.pkg.json` sidecar and keep it out of commits. Publish the `.pkg.json` artifact manifest and `.pkg.sha256` with the GitHub Release.

The installer package requires a `Developer ID Installer` certificate in the keychain. A `Developer ID Application` certificate signs the plug-in bundles, but it is not enough to sign the `.pkg`.

Unsigned smoke-test packages are intentionally marked `UNSIGNED-DO-NOT-UPLOAD` and are written under `/tmp/kirin_hypha_pkg_smoke/`:

```bash
KIRIN_SKIP_PKG_SIGN=1 KIRIN_SKIP_PKG_NOTARIZE=1 \
  node scripts/ls_release/build_kirin_hypha_pkg.mjs
```

Signed Windows installers require an independently controlled signing environment and reviewed
exact source. A successful exact-source public CI run and a reviewed private unsigned producer
on windows-latest bind the signed PRE/POST payload, installer and uninstaller to that commit.
The same reviewed factory signs a pending candidate, then promotes it after dedicated-host
validation; the release entry qualifies the complete signed-full delivery and its origin.
Same-version reinstall, prior-release upgrade, isolated uninstall,
pluginval and dedicated-host acceptance remain mandatory release gates. Signing credentials and
operator configuration must remain outside this public GPL repository. Public CI remains unsigned
and its held binary uploads do not supply the formal candidate. It has no signing credential or
persistent-runner route; manual dispatch cannot enable signing. Follow the four operator gates
in the [distribution gate](../provenance/distribution_gate.md): before Windows AAX signing or
build_only, before candidate signing, after promotion before coordinator/macOS operations, and
before A4 and each A5 resume. Windows external validation precedes promotion; A3 means the
coordinator freeze. The current formal Windows route accepts only an empty update key; public
CI key inputs do not produce a formal candidate. See the [release entry](../hypha_release_entry.md).

[The exposure report](../security/public_repo_exposure_audit_20261005.md) records the trust boundary.

The macOS recovery ZIP is a required artifact of the same full release, with the same signed
payloads and release identity as the normal PKG. The Windows manual VST3 ZIP is a separate
diagnostic/recovery fallback; it does not replace the full signed installer.

Do not run the signed release checks inside a sandboxed child process; macOS `codesign` can report a false `invalid signature` for valid notarized plugin bundles in that context.
After verification passes, upload `Kirin-Hypha-<version>-macOS-Universal.pkg` and
`Kirin-Hypha-<version>-Windows-x64-Setup.exe` to each configured Lemon Squeezy product.

Public diagnostic CI retains all required build/validation jobs and logs. Preview artifacts are published only for inputs accepted for that use; binary/installer uploads remain held pending actual NOTICE, component license and Corresponding Source delivery evidence. Unresolved materials keep individual holds in the [distribution registry](../provenance/asset_distribution_registry.json).
