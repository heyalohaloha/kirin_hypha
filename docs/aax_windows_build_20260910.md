# Kirin Hypha Windows AAX build and distribution boundary

Date: 2026-09-10

This public-repository document records technical build and packaging facts only. It contains no
account identifiers, credentials, private correspondence, or contract terms.

## Verified build state

PRE and POST were configured and built on the Windows validation host with AAX SDK 2.9.0, Visual
Studio Build Tools 2022, MSVC x64, and the tracked JUCE patch stack on 2026-09-10. The produced PE
binaries were both machine type `0x8664` (`x64`):

| Role | Binary size | File/Product version |
|---|---:|---|
| PRE | 15,467,008 bytes | 1.1.49 |
| POST | 17,541,120 bytes | 1.1.49 |

This proves the external-SDK Windows build path. The same exact binaries subsequently passed both
PACE and Authenticode verification through the combined signing path described below.

## Reproducible build

Run from a Visual Studio x64 developer shell with the SDK outside the GPL repository:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build_aax_windows.ps1 `
  -Sdk C:\absolute\external\aax-sdk-2-9-0 `
  -LicenseConfirmed `
  -KimeraFont C:\absolute\licensed\KMR-Waldenburg-Book.otf `
  -KimeraLicenseConfirmed
```

The script builds the Rust FFI static library, applies and verifies the tracked JUCE patch stack,
requires a clean B-numbered source, configures the explicit AAX gate, builds only PRE/POST AAX,
checks both version resources, and writes `kirin-hypha-windows-aax-build.json`. The manifest pins the
full source commit, source state, Kimera embedding state, Native-only/AudioSuite state, and exact
PRE/POST hashes. Omitting the Kimera options is permitted for an unsigned diagnostic build, but the
resulting manifest records `kimera_embedded: false` and release signing rejects it.

## Signing boundary

PACE AAX Code Signing Tools 6.0.1 are installed on the Windows validation host. With the physical
signing authorization attached, an unsigned binary reaches signature inspection and fails as
unsigned instead of failing on authorization. This confirms the local authorization path without
claiming that the artifact is signed.

Two Windows signing experiments establish the operation boundary:

- `wraptool sign` without `--signid` or `--keyfile` rejects the input because Windows platform
  signing credentials are mandatory.
- `wraptool sign --dsig off` is rejected because the `sign` operation does not permit disabling the
  platform signature.

PACE and Authenticode signing therefore cannot be split into two mutations of the binary on
Windows. The prior candidate sequence (PACE first, eSigner second) is invalid.

The supported route is one `wraptool sign` operation with a certificate thumbprint from the
current user's Windows certificate store. SSL.com's eSigner CKA exposes the existing cloud-held
certificate through the Windows CNG/KSP interface, allowing `signtool.exe` and callers such as
`wraptool` to use the same certificate without exporting its private key. The private release
factory owns eSigner credentials; they are not copied into this public GPL repository.

Distribution verification uses `wraptool verify --localonly` so it never asks for or emits an iLok
account password. The command still requires the physical `PACE Tools` authorization to be attached;
without it, verification fails closed before inspecting a candidate.

The release sequence is:

1. Build PRE and POST on the SDK-equipped Windows host.
2. Load the eSigner CKA certificate into that same user's certificate store.
3. Run `scripts/windows/sign-aax-wraptool.ps1` once, passing the certificate thumbprint to
   `wraptool`; the physical signing authorization must be attached. Before mutation, the script
   verifies the build provenance and refuses modified-source, non-Kimera, non-Native-only, or
   hash-mismatched input.
4. Verify PACE locally, Authenticode validity, Kirin publisher identity, secure timestamp, x64 PE,
   version, and exact PRE/POST bundle structure before packaging. Successful signing writes
   `kirin-hypha-windows-aax-signed.json`, preserving unsigned hashes and recording signed hashes.

The output directory must be new and separate from the unsigned build. Failed attempts never mutate
the unsigned source artifacts.

## Verified combined-signing result

The private signing factory completed the combined Windows signing proof on 2026-09-11 (JST):

- public signing-procedure commit: `910136e67d373b6b14b4ada96e78d176cd02a5af` (`B-810`)
- unsigned PRE/POST handoff digest:
  `sha256:e1da9b5c44e9dd514334060fef5929d133daa237144f1f515be18013fb95c62b`
- signed PRE/POST proof digest:
  `sha256:7ccb47dadc72bee245740bbe3480de0fe4c78e438a83a1ac0d557aa2b38a494c`

Both bundles passed `wraptool verify --localonly`, Windows Authenticode validation, publisher and
RFC 3161 timestamp checks, x64/version checks, and the exact PRE/POST structure gate. The signing
job reused the immutable unsigned handoff and did not rebuild the binaries. Its cleanup removed the
temporary CKA profile and unloaded the certificate; the ephemeral runner was then detached.

This completes the Windows AAX build and standalone signing proof. It does not claim installer or
Pro Tools validation.

Official implementation references:

- [SSL.com: eSigner CKA with SignTool](https://www.ssl.com/how-to/automate-ev-code-signing-with-signtool-or-certutil-esigner/)
- [SSL.com: eSigner CKA CI/CD integration](https://www.ssl.com/how-to/how-to-integrate-esigner-cka-with-ci-cd-tools-for-automated-code-signing/)

## Installer boundary

`scripts/windows/build-installer.mjs --aax-artifact-dir <dir>` is opt-in. It accepts AAX only when
exact PRE and POST bundles are present, both are x64 and version-matched, and both pass PACE and
Authenticode verification. It also requires the signed AAX provenance to match the clean installer
source commit and B number. The signed provenance is copied beside the installer, hashed into the
installer manifest, and rechecked by the three-channel release-set gate. This prevents a signed AAX
from another same-version commit, a system-font diagnostic build, or an AudioSuite-enabled build
from being mixed into a release. AAX is copied after the VST3 eSigner payload stage and is never
signed again by the installer builder.

The AAX installer variant requires administrator access and owns only these paths under the 64-bit
Common Files directory:

- `Avid\Audio\Plug-Ins\Kirin Hypha PRE.aaxplugin`
- `Avid\Audio\Plug-Ins\Kirin Hypha POST.aaxplugin`

Repeat install and uninstall verification checks both formats, both signature systems, exact hashes,
and preservation of unrelated files. The ordinary VST3-only installer remains the default path.

The latest public Windows installer and the current source version are both 1.1.49. A real
prior-public-version upgrade can therefore run only after the next release version is assigned.
`verify-installer.ps1` requires an explicit older installer for the AAX variant and rejects an equal
or newer version, so a same-version reinstall cannot be mislabeled as an upgrade.

## Remaining release gates

- Build the VST3+AAX installer and run install, same-version reinstall, prior-public-version upgrade,
  and uninstall validation.
- Complete Windows Pro Tools load, category, transparency, Offline Bounce, restore, mono/stereo,
  and pairing validation. The macOS B-786 artifact has an Intel Pro Tools proof, but that evidence
  neither covers Windows nor a later same-version source commit.
- Produce all three public channels from the same release commit; AAX is not a fourth channel.
