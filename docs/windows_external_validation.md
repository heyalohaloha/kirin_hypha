# Kirin Hypha Windows Validation and Regression Checklist

Purpose: validate every Windows JUCE VST3 release on a real Windows DAW before public upload.

Status: Windows 10/11 64-bit VST3 is supported. The v1.1.48 release records Windows validation as
complete after CI, pluginval, and Studio One Pro checks on the dedicated Windows validation machine.
This document remains the required regression checklist for later release commits; a new package
must stay blocked until these gates are complete for that commit.

## Current Boundary

- Windows delivery target: JUCE VST3, PRE and POST.
- Current published state: v1.1.48 is a supported, validated manual PRE/POST ZIP without an installer
  or Authenticode signatures.
- Next-release procedure: GitHub Actions builds PRE/POST Windows VST3, validates them with pluginval,
  signs their PE binaries, and packages both roles in one Inno Setup installer.
- Planned primary packaging: `scripts/windows/build-installer.mjs` creates the Setup EXE, SHA-256,
  and manifest. `scripts/windows/verify-installer.ps1` gates repeat install, installed hashes and
  signatures, uninstaller signature, cleanup, and preservation of unrelated VST3 files.
- Fallback packaging: `scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs` creates a manual
  recovery ZIP. It never replaces the primary installer.
- Per-release validation gate: real Windows DAW load, PRE/POST discovery, Keep, Record, offline
  bounce, and audio transparency must be complete before LS-ready.
- Next-release Authenticode scope: PRE binary, POST binary, Setup EXE, and generated uninstaller must
  all be Valid before the installer can replace the manual ZIP.

## Artifact To Send

For the next release, first validate the unsigned installer candidate named
`kirin-hypha-windows-installer` from the complete green Hypha CI run. Its manifest must identify the
same exact release-candidate commit and version. After this checklist is green, the private signing
factory may rebuild that exact source commit with `external_validation=complete`; it then verifies the
signed payload and installer mechanics again before producing `KirinHypha-Windows-signed-full`.

The handoff artifact should include:

- `Kirin-Hypha-<version>-Windows-x64-Setup.exe`
- the matching `.exe.sha256`
- the matching `.exe.json`, with signing `verified_unsigned_ci_candidate`, CI validation `passed`,
  and external validation `pending`

Before the checklist passes, treat that commit's package as a validation build. After it passes, the
exact source commit may enter the signed factory with external validation marked complete. Publish
only the factory's signed output after its own pluginval, transparency, signature, repeat-install,
and uninstall gates pass. The unsigned candidate and fallback ZIP are not normal user-facing downloads.

## Tester Requirements

- Windows 10 or 11, 64-bit.
- A DAW that can load VST3 plug-ins.
- Ability to run the current-user installer and Windows Installed apps uninstaller.
- Ability to send screenshots and exported WAV files back to Daisuke.

Record these facts in the report:

```text
Windows version:
CPU:
DAW name/version:
Sample rate:
Buffer size:
Install path used:
Kirin Hypha commit:
CI run URL:
```

## Install

Close the DAW before installing.

Open `Kirin-Hypha-<version>-Windows-x64-Setup.exe` and select **Current user** for the first pass.
Repeat the installer once before DAW testing to exercise same-version update behavior. Both passes
must complete without manual VST3 folder selection.

Expected installed paths:

```text
%LOCALAPPDATA%\Programs\Common\VST3\Kirin Hypha PRE.vst3
%LOCALAPPDATA%\Programs\Common\VST3\Kirin Hypha POST.vst3
```

The optional all-users selection installs to the global path and requires elevation. Do not use it
for the first validation pass:

```text
%ProgramFiles%\Common Files\VST3
```

If the tester has no existing Kirin data they need to keep, they may start clean:

```powershell
Remove-Item -Recurse -Force "$env:TEMP\kirin" -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force "$env:LOCALAPPDATA\Kirin OS\plugin_data" -ErrorAction SilentlyContinue
```

Do not delete existing Kirin data on a machine where the tester relies on Kirin OS / Kirin Sense data.

## Test 1: DAW Scan And Load

1. Start the DAW.
2. Rescan VST3 plug-ins if needed.
3. Confirm both plug-ins are visible:
   - `PRE Kirin Hypha`
   - `POST Kirin Hypha`
4. Insert each plug-in on a stereo bus.
5. Open each plug-in GUI.
6. At the Windows display scale used for the test, inspect the complete PRE and POST screens.

Pass:

- Both plug-ins appear in the DAW.
- Both plug-ins instantiate.
- Both GUIs open without crashing the DAW.
- The header reads `PRE` / `POST` in full; neither title is replaced by an ellipsis.
- In POST, the loudness row shows the `Δ` prefix and the pair-menu trigger shows a downward
  triangle, not a missing-glyph square.
- `PAIR ●`, `PAIR ◌`, `PAIR —`, metric labels/units, PRE name, POST pair name, feedback text,
  buttons, and pair-menu rows contain no missing-glyph squares or unintended truncation.

Fail data:

- DAW name/version.
- Screenshot of the plug-in scan or error, plus full PRE and POST GUI screenshots.
- Any crash report.

## Test 2: Watch Files

1. Create a short session with audio playback.
2. Insert PRE before the processing chain and POST after it on the same bus or main bus.
3. Press play for at least 10 seconds.
4. Check that Watch files are being written:

```powershell
Get-ChildItem "$env:TEMP\kirin" -Recurse -Filter pre.json  | Select-Object -First 10 FullName,Length,LastWriteTime
Get-ChildItem "$env:TEMP\kirin" -Recurse -Filter post.json | Select-Object -First 10 FullName,Length,LastWriteTime
```

Pass:

- At least one `pre.json` exists under `%TEMP%\kirin`.
- At least one `post.json` exists under `%TEMP%\kirin`.
- The files update while transport is playing.
- The POST GUI shows live values, not only `---`.

Fail data:

- Screenshot of PRE and POST GUIs during playback.
- Output of the two PowerShell commands.
- Zip of `%TEMP%\kirin` if it exists.

## Test 3: Pairing And Keep

1. Optionally name the PRE instance with a simple label such as `Mix` or `Drum`.
2. On POST, open the pair menu.
3. Select that exact PRE under **Pair choices (not Keep targets)**; matching names are not required.
4. Press `Keep`.
5. Confirm PRE acknowledges the record request and POST indicates active Keep/Record state.
6. Stop Keep.

Pass:

- POST can see the PRE candidate.
- Selecting the PRE updates the POST pair label.
- Keep starts without error.
- Stop exits Keep without leaving the DAW in a bad state.

Fail data:

- Screenshot of POST pair menu.
- Screenshot before and after pressing Keep.
- Zip of `%TEMP%\kirin`.

## Test 4: Offline Bounce Record

1. Select the same PRE/POST pair.
2. Press `Keep`.
3. Run an offline bounce/export for at least 20 seconds.
4. Confirm POST remains in the active Keep/Record generation after the bounce. Host offline-render state is evidence about processing mode; it does not own the Record lifecycle.
5. Press `Stop` explicitly to close the take.
6. Check Record/plugin_data output:

```powershell
Get-ChildItem "$env:LOCALAPPDATA\Kirin OS\plugin_data" -Recurse -Filter *.json |
  Sort-Object LastWriteTime -Descending |
  Select-Object -First 20 FullName,Length,LastWriteTime
```

For the newest PRE and POST record JSON files, search for Phase D fields:

```powershell
Select-String -Path "<LATEST_JSON_PATH>" -Pattern '"n_prime_total"','"sharpness"','"psb"'
```

Pass:

- Record JSON files are written under `%LOCALAPPDATA%\Kirin OS\plugin_data`.
- PRE and POST record files are both present.
- The newest files contain measurement frames.
- `n_prime_total`, `sharpness`, or `psb` fields are present after Phase D warm-up.
- Offline bounce does not crash the DAW.

Fail data:

- Output of the PowerShell commands.
- Zip of `%LOCALAPPDATA%\Kirin OS\plugin_data`.
- Screenshot of POST after bounce.

## Test 5: Audio Transparency

Kirin Hypha must not change audio.

Preferred evidence:

1. Export a short WAV with Hypha PRE/POST active.
2. Export the same range with Hypha PRE/POST removed or bypassed.
3. Send both WAV files to Daisuke for null/bit comparison.

Pass:

- The two files null or compare bit-identical after alignment.

Some production sessions contain free-running modulation, reverbs, or other non-deterministic processors
and cannot produce two bit-identical exports even when the insert state is unchanged. In that case, do not
attribute the difference to Hypha. Run `KirinAudioTransparencyContractTests` against the exact installed
PRE and POST VST3 bundles. The host contract exercises active realtime, active offline, stereo, and mono
processing with known sample buffers and fails on the first changed sample bit or any non-zero latency.

Pass requires either a deterministic DAW null or a passing exact-binary host contract for both PRE and POST.
If neither can be completed, this gate remains unresolved. Do not treat that as a full release pass.

## Release Decision

- Any failure in Test 1 or Test 2: do not proceed to LS.
- Any failure in Test 3 or Test 4: do not proceed to LS.
- Test 5 missing: beta can continue, but LS release remains blocked.
- All tests pass with artifacts attached: mark external validation complete in the signed installer
  manifest and include that exact installer in the three-channel release set.
