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
| PRE | 15,474,176 bytes | 1.1.49 |
| POST | 17,548,800 bytes | 1.1.49 |

This proves the external-SDK Windows build path. The binaries are not distribution-ready until
PACE and Authenticode verification both pass.

## Reproducible build

Run from a Visual Studio x64 developer shell with the SDK outside the GPL repository:

```powershell
pwsh -NoProfile -File scripts/build_aax_windows.ps1 `
  -Sdk C:\absolute\external\aax-sdk-2-9-0 `
  -LicenseConfirmed
```

The script builds the Rust FFI static library, applies and verifies the tracked JUCE patch stack,
configures the explicit AAX gate, builds only PRE/POST AAX, and checks both version resources.

## Signing boundary

PACE AAX Code Signing Tools 6.0.1 are installed on the Windows validation host. The installed
`wraptool` documentation confirms that Windows signing can use either a code-signing certificate
thumbprint from a Windows certificate store or a PKCS12 key file. This host currently has neither;
the existing Windows Authenticode route uses the separate remote eSigner workflow.

Distribution verification uses `wraptool verify --localonly` so it never asks for or emits an iLok
account password. The command still requires the physical `PACE Tools` authorization to be attached;
without it, verification fails closed before inspecting a candidate.

Therefore the release sequence is not declared complete yet. The candidate sequence is:

1. PACE-sign both AAX binaries using the physical signing authorization.
2. Authenticode-sign those same binaries with the existing eSigner route.
3. Re-run PACE `wraptool verify` after Authenticode signing.
4. Reject the artifact if either PACE or Authenticode verification fails.

Step 3 is the deciding experiment. Do not encode the order as a release rule until it passes on the
actual PRE and POST artifacts.

## Installer boundary

`scripts/windows/build-installer.mjs --aax-artifact-dir <dir>` is opt-in. It accepts AAX only when
exact PRE and POST bundles are present, both are x64 and version-matched, and both pass PACE and
Authenticode verification. AAX is copied after the VST3 eSigner payload stage and is never signed
again by the installer builder.

The AAX installer variant requires administrator access and owns only these paths under the 64-bit
Common Files directory:

- `Avid\Audio\Plug-Ins\Kirin Hypha PRE.aaxplugin`
- `Avid\Audio\Plug-Ins\Kirin Hypha POST.aaxplugin`

Repeat install and uninstall verification checks both formats, both signature systems, exact hashes,
and preservation of unrelated files. The ordinary VST3-only installer remains the default path.

## Remaining release gates

- Move the physical signing authorization to Windows and run the signing-order experiment once.
- Build the VST3+AAX installer and run install, same-version reinstall, prior-public-version upgrade,
  and uninstall validation.
- Complete Pro Tools load, category, transparency, Offline Bounce, restore, mono/stereo, and pairing
  validation after the developer license is issued.
- Produce all three public channels from the same release commit; AAX is not a fourth channel.
