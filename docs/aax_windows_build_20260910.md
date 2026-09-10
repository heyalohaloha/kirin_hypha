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
   `wraptool`; the physical signing authorization must be attached.
4. Verify PACE locally, Authenticode validity, Kirin publisher identity, secure timestamp, x64 PE,
   version, and exact PRE/POST bundle structure before packaging.

The output directory must be new and separate from the unsigned build. Failed attempts never mutate
the unsigned source artifacts.

Official implementation references:

- [SSL.com: eSigner CKA with SignTool](https://www.ssl.com/how-to/automate-ev-code-signing-with-signtool-or-certutil-esigner/)
- [SSL.com: eSigner CKA CI/CD integration](https://www.ssl.com/how-to/how-to-integrate-esigner-cka-with-ci-cd-tools-for-automated-code-signing/)

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

The latest public Windows installer and the current source version are both 1.1.49. A real
prior-public-version upgrade can therefore run only after the next release version is assigned.
`verify-installer.ps1` requires an explicit older installer for the AAX variant and rejects an equal
or newer version, so a same-version reinstall cannot be mislabeled as an upgrade.

## Remaining release gates

- Run the combined PACE + eSigner CKA signing path once on the private self-hosted release runner.
  Windows uses `--explicitsigningoptions` with the complete `sign` argument list so the SHA-256
  file digest and RFC 3161 timestamp arguments precede the target passed to SignTool; PACE's
  default invocation does not supply the now-required `/fd` argument.
- Build the VST3+AAX installer and run install, same-version reinstall, prior-public-version upgrade,
  and uninstall validation.
- Complete Pro Tools load, category, transparency, Offline Bounce, restore, mono/stereo, and pairing
  validation after the developer license is issued.
- Produce all three public channels from the same release commit; AAX is not a fourth channel.
