# Kirin Hypha Windows Validation and Regression Checklist

Purpose: validate each full Windows PRE/POST VST3 + AAX release on real Windows DAWs before public upload.

A previous release's acceptance does not qualify a new commit or new payload. Keep the candidate
blocked until its exact signatures, installer lifecycle and real-host evidence are complete. Use
[`hypha_release_entry.md`](hypha_release_entry.md) for the complete three-channel release procedure.

## Current Boundary

- Windows delivery target: PRE/POST × JUCE VST3/AAX x64 in one full installer.
- All four payload binaries, the Setup EXE and the generated uninstaller require valid
  Authenticode signatures. AAX also requires matching PACE verification and signed provenance.
- Per-release procedure: exact-source public CI must pass. A reviewed private unsigned producer
  builds PRE/POST Windows VST3 on windows-latest and validates them with pluginval; the same
  reviewed factory imports that qualified artifact, signs the PE binaries and packages both roles.
  Public CI binary uploads are not the formal source of the candidate.
- Primary packaging: `scripts/windows/build-installer.mjs` creates the Setup EXE, SHA-256,
  and manifest. `scripts/windows/verify-installer.ps1` gates install, same-version reinstall,
  installed hashes and signatures, uninstaller signature, cleanup, and preservation of unrelated
  VST3 files.
- Fallback packaging: `scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs` creates a manual
  recovery ZIP. It never replaces the primary installer.
- Per-release validation gate: real Windows DAW load, PRE/POST discovery, Keep, Record, offline
  bounce, and audio transparency must be complete before LS-ready.
- Authenticode scope: PRE/POST VST3, PRE/POST AAX, Setup EXE and generated uninstaller must all
  be Valid before publication. Retain exact payload hashes and AAX PACE verification evidence.

## Artifact To Send

For each later release, close Gate 1 of the
[distribution gate](provenance/distribution_gate.md) before Windows AAX signing or build_only.
At the same reviewed factory commit, finish those input runs and close Gate 2 before candidate
signing: qualify both 3-day handoffs with actual IDs, digests, commits and expiry/job-time margin,
and recheck repository retention, concurrency and candidate budget. Then validate the
`KirinHypha-Windows-signed-candidate` artifact. Its manifest must identify the exact green Hypha CI
commit and version, and its Setup EXE and installed PRE/POST payloads must already be Authenticode
signed. The candidate must remain `external_validation=pending` and `public_ready=false` until this
checklist is complete. Record its actual run/artifact ID and API expires_at immediately after upload;
signed-full and its deadline become available only after promotion. This Windows external validation
precedes promotion; it is not A3. The coordinator's A3 is freeze after its windows stage.

The handoff artifact includes these top-level distribution files:

- `Kirin-Hypha-<version>-Windows-x64-Setup.exe`
- the matching `.exe.sha256`
- the matching `.exe.json`, with signing `valid`, CI validation `passed`, external validation
  `pending`, and `distribution.public_ready=false`

An AAX candidate also includes its matching signed-provenance sidecar. Preserve the complete
factory ZIP and extracted tree, including `provenance/` producer, qualification, Legal/source
and role-specific map evidence. Keep reports/logs outside that tree.

Retain the completed report and calculate its SHA-256. Then run the same factory in promotion mode
with the signed-candidate run ID and report SHA-256. Promotion downloads that exact candidate,
re-verifies its installer hash and Authenticode signature, and updates the manifest's validation,
readiness and factory-origin metadata. It also adds `provenance/candidate-manifest.json`,
`provenance/signed-candidate-artifact.json` and `provenance/promotion-transition.json`, while retaining
the candidate's existing evidence. It does not rebuild or re-sign the EXE. Use only the resulting
`KirinHypha-Windows-signed-full` as input to the release entry after confirming its EXE SHA-256 is identical to the one
tested here. Retain and extract the entire signed-full ZIP without adding, deleting or rewriting
files: the coordinator compares all files and hashes with the original archive. Follow
[WIN-2](ls_release/kirin_hypha_ls_runbook.md#win-2-required-artifacts) for the private handoff and
selected public assets; the complete private evidence tree is not published. After promotion,
close Gate 3 before coordinator --execute or macOS signing/notarization/PACE, including actual
candidate/full deadlines, the original ZIP and complete tree, and planned A3/A4/A5 dates.
Close Gate 4 before A4 host acceptance and each A5 resume; stop if A5 cannot fit the earlier
deadline with margin. The signed candidate and fallback ZIP are not normal user-facing downloads.

## Tester Requirements

- Windows 10 or 11, 64-bit.
- A VST3 DAW and regular Pro Tools capable of loading the signed AAX candidate.
- Administrator permission for the full all-users installer and its uninstaller.
- A new dedicated test session; preserve existing user sessions and Kirin OS data.
- Ability to send screenshots and exported WAV files to the release maintainer.

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
Signed candidate workflow run URL:
Signed candidate installer SHA-256:
Validation report SHA-256:
```

## Install

Close the DAW before installing.

Open `Kirin-Hypha-<version>-Windows-x64-Setup.exe` with administrator permission. The full AAX
installer uses the all-users installation scope; the automated lifecycle uses `/ALLUSERS`.
Repeat the installer once before DAW testing to exercise same-version update behavior. Both passes
must complete without manual VST3 folder selection.

For an upgrade test, begin separately from the preceding public Windows releases by installing their
signed installers in the scopes users received by default: an earlier VST3-only release installs for
the current user, a VST3 + AAX release for all users. Test each relevant starting state separately,
including both copies present at once. Leave the old copies, registrations and scopes in place: the
candidate installer itself must update or migrate them, so never move an old bundle aside before
the candidate runs. Open and save one DAW session containing both roles, close the DAW, then run the
candidate installer. Reopen that session and confirm that the candidate replaces both owned bundles,
removes only this user's own older current-user PRE/POST copy, preserves the session identities and
pair selection, and does not alter unrelated VST3 files or other Windows accounts. Record, from the
running DAW and its plug-in scanner, the path and SHA-256 of every Kirin Hypha binary actually
loaded; every loaded path and hash must be the candidate's. Record the old and new versions in the
external-validation report. Same-version reinstall does not substitute for this upgrade test.

Setup removes the older current-user PRE/POST bundles and then that copy's registration itself. It
never runs the older per-user uninstaller from the user-writable profile, so that uninstaller's files
stay unregistered in `%LOCALAPPDATA%\Programs\Kirin Mastering\Kirin Hypha`. If an older current-user
bundle cannot be removed (for example a DAW still holds it), Setup stops before installing anything
new, keeps the older registration, and asks to close every DAW and plug-in scanner; running it again
completes the update. Cancelling during file copying leaves Kirin Hypha uninstalled until Setup runs again;
settings and DAW projects are kept. Setup cannot see another Windows account's current-user copy, or
the copy of the signed-in user when Setup is elevated with a different administrator account; that
account removes its own copy from Windows Settings.

Expected installed paths for the full installer:

```text
%ProgramFiles%\Common Files\VST3\Kirin Hypha PRE.vst3
%ProgramFiles%\Common Files\VST3\Kirin Hypha POST.vst3
%ProgramFiles%\Common Files\Avid\Audio\Plug-Ins\Kirin Hypha PRE.aaxplugin
%ProgramFiles%\Common Files\Avid\Audio\Plug-Ins\Kirin Hypha POST.aaxplugin
```

A separate VST3-only diagnostic installer may support current-user installation. It does not
qualify the full AAX installer, and it refuses a current-user installation while an all-users copy
exists. Before an independent fresh-install test, preserve and restore an existing installation using
the approved procedure rather than deleting it; for an upgrade test keep the old copies as described
above.

Do not clear `%TEMP%\kirin` or Kirin OS `plugin_data` globally. Use owned test sessions and an
isolated QA storage plan. Establish the storage environment for every scanner/broker and host
process before it can load a plug-in. Preserve shared Hypha UI preferences and track only the
resources created by this validation. A configured QA root does not sandbox arbitrary file choosers.
Record the actual Watch and plugin-data roots in the report; the following normal-path examples
must be adjusted to those verified roots when isolated QA storage is used.

## Test 1: DAW Scan And Load

1. Start the DAW.
2. In the VST3 DAW, rescan VST3 plug-ins if needed. Repeat scan/load in regular Pro Tools for AAX.
3. Confirm both plug-ins are visible:
   - `PRE Kirin Hypha`
   - `POST Kirin Hypha`
4. Insert each plug-in on a stereo bus.
5. Open each plug-in GUI.
6. At the Windows display scale used for the test, inspect the complete PRE and POST screens.
7. Save and reopen a new test session in each host. A Developer host that cannot save/export
   sessions does not substitute for this regular Pro Tools acceptance.

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
3. Select that exact PRE under **PRE connection**; matching names are not required.
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
3. Send both WAV files to the release maintainer for null/bit comparison.

Pass:

- The two files null or compare bit-identical after alignment.

Some production sessions contain free-running modulation, reverbs, or other non-deterministic processors
and cannot produce two bit-identical exports even when the insert state is unchanged. In that case, do not
attribute the difference to Hypha. Run `KirinAudioTransparencyContractTests` against the exact installed
PRE and POST VST3 bundles. The host contract exercises active realtime, active offline, stereo, and mono
processing with known sample buffers and fails on the first changed sample bit or any non-zero latency.

For VST3, pass requires either a deterministic DAW null or a passing exact-binary host contract
for both PRE and POST. This host-contract fallback qualifies VST3 only. AAX retains its actual
Pro Tools export/null evidence requirement. Missing evidence for either format leaves that
format's existing acceptance unresolved.

## Full-candidate host acceptance

The order is Windows basic external validation → promotion → coordinator/A3 → full A4 acceptance.
This full A4 section is completed after promotion; it is not a prerequisite for marking the
Windows basic external-validation report complete or running promotion.

After A3, complete the mandatory host matrix and expectations fixed for the candidate before A3 in the
[release entry](hypha_release_entry.md), including LISTEN clock/PDC, local Blind anonymity,
Exact 4 S frame counts, pair-loss/return and recreation, state/automation recall, the declared
rate/buffer/layout matrix and UI stress. The 1.1.51 acceptance scope excludes the legacy
Exact 4 S comparison by release-owner decision; this exclusion is not a passing test and does
not change the default gate for later candidates. The five checks above are part of full
acceptance; they do not replace it. Bind every result to the exact installed payload hashes.

## Release Decision

- Any failure in Test 1 or Test 2: do not proceed to LS.
- Any failure in Test 3 or Test 4: do not proceed to LS.
- Test 5 missing: beta can continue, but LS release remains blocked.
- All five basic Windows tests pass with artifacts attached: mark external validation complete
  and promote the same signed installer under the reviewed factory procedure. Then continue
  through coordinator/A3, full A4 and the remaining distribution gates before public release.
