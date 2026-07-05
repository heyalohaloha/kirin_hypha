# Kirin Hypha Windows External Validation

Purpose: validate the first Windows JUCE VST3 build before any Lemon Squeezy upload.

Status: external validation gate. A Windows VST3 zip/state can be generated before validation, but it must stay blocked until the pass/fail gates below are complete.

## Current Boundary

- Windows delivery target: JUCE VST3, PRE and POST.
- Current proven state: GitHub Actions builds PRE/POST Windows VST3, validates them with pluginval, and packages a Windows VST3 zip candidate.
- Release packaging: `scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs` creates the zip, SHA256 file, sidecar JSON, and LS state.
- Validation gate: real Windows DAW load, PRE/POST discovery, Keep, Record, offline bounce, and audio transparency must be complete before LS-ready.
- Not included yet: Windows installer and Authenticode signing.

## Artifact To Send

Send a zip made from the latest green CI artifact named `kirin-hypha-windows-vst3`, or the packaged CI artifact named `kirin-hypha-windows-vst3-ls-package`.

The handoff zip should include:

- `Kirin Hypha PRE.vst3`
- `Kirin Hypha POST.vst3`
- `COMMIT.txt` with the commit hash and CI run URL
- `SHA256SUMS.txt`
- this validation document

The recipient must treat it as a beta validation build, not a public installer.

## Tester Requirements

- Windows 10 or 11, 64-bit.
- A DAW that can load VST3 plug-ins.
- Ability to unzip files and copy folders into the user VST3 directory.
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

Use the user-level VST3 directory first:

```powershell
New-Item -ItemType Directory -Force "$env:LOCALAPPDATA\Programs\Common\VST3"
Copy-Item -Recurse -Force ".\Kirin Hypha PRE.vst3" "$env:LOCALAPPDATA\Programs\Common\VST3\"
Copy-Item -Recurse -Force ".\Kirin Hypha POST.vst3" "$env:LOCALAPPDATA\Programs\Common\VST3\"
```

Expected installed paths:

```text
%LOCALAPPDATA%\Programs\Common\VST3\Kirin Hypha PRE.vst3
%LOCALAPPDATA%\Programs\Common\VST3\Kirin Hypha POST.vst3
```

Avoid the global admin path for the first validation pass:

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

Pass:

- Both plug-ins appear in the DAW.
- Both plug-ins instantiate.
- Both GUIs open without crashing the DAW.

Fail data:

- DAW name/version.
- Screenshot of the plug-in scan or error.
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

1. Name the PRE instance with a simple label such as `Mix` or `Drum`.
2. On POST, open the pair menu.
3. Select the matching PRE.
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
4. Confirm POST closes Keep/Record after the bounce when the same Record generation has processed at least 1 second of offline audio. Short offline preflight fragments must not close Record. To test the manual-stop fallback only, launch with `KIRIN_HYPHA_OFFLINE_AUTOSTOP=0`.
5. Press `Stop` manually to close the take.
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

If the tester cannot do a null test, this gate remains unresolved. Do not treat that as a full release pass.

## Release Decision

- Any failure in Test 1 or Test 2: do not proceed to LS.
- Any failure in Test 3 or Test 4: do not proceed to LS.
- Test 5 missing: beta can continue, but LS release remains blocked.
- All tests pass with artifacts attached: prepare Windows LS packaging and a separate Windows LS state/runbook.
