# Kirin Hypha

**A free, open-source audio measurement plugin for macOS (VST3 and Audio Unit).**

The supported release is currently macOS-only. A manual Windows VST3 validation package is also built and published for external testing, but it is not yet a supported Windows release.

Kirin Hypha operates as paired instances — a **PRE** plugin and a **POST** plugin — to measure signal states before and after a processing chain and display the difference.

On a paired POST, the on-demand Spectrum turns that comparison into a signed **Δ (POST − PRE)**
frequency view, with selectable LR / MID / SIDE observation, a lockable probe, and one temporary
MARK reference. A locked probe also shows the selected frequency's exact six-second **Focus Trail**.
Closing the page stops its optional analysis and discards that short display history.

The POST **ANALYSIS** page can switch between **FREQ** and **SHARP**. SHARP's first observation is a six-second
**Δ Sharpness History**: a signed POST − PRE Sharpness trace that responds at 10 Hz without scoring,
traffic-light judgment, or audio-path changes. Spectrum and Perceptual Δ are mutually exclusive, so
only the visible analyzer runs. One POST Analysis page per DAW process owns the optional PRE/POST
analyzers; another open page remains idle and reports `ANALYSIS — IN USE`.

![Kirin Hypha PRE and POST showing Short-term Watch values and independent MAX values](docs/media/kirin-hypha-pre-post.jpg)

[Watch PRE/POST with the M/S selector and independent Watch MAX values (32-second silent MP4)](docs/media/kirin-hypha-pre-post-demo.mp4)

[Watch POST move from Watch to Keep/Record and hold the final result after Stop (45-second silent MP4)](docs/media/kirin-hypha-record-keep-demo.mp4)

---

## Design

Kirin Hypha is built to **observe, not to advise**.

It produces measurement data. It does not generate, modify, or attenuate audio. The same input file produces the same output, every time. Numbers are reported as captured — no interpretation, no scoring, no recommendation.

Every metric is backed by a known-signal golden test: the expected values are derived independently from the signal definition and the ITU-R BS.1770 filter coefficients, not asserted by hand. The measurement layer demonstrates its precision rather than claiming it.

---

## Modes

| Feature | Standalone | With Kirin OS |
|---|---|---|
| Watch mode | ✓ | ✓ |
| Record mode | — | ✓ |
| plugin_data output | — | ✓ |

Kirin Hypha is free and fully functional as a standalone plugin. Record mode requires a Kirin OS license.

---

## What it measures

### Watch mode (real-time)

| Metric | Window / Unit | Standard |
|---|---|---|
| LUFS-M / LUFS-S | Selectable Momentary (400 ms) or Short-term (3 s) loudness (LUFS) | ITU-R BS.1770-4 |
| True Peak | Recent peak, last 400 ms (dBTP) | ITU-R BS.1770-4 |
| Crest Factor | Peak − RMS, 400 ms (dB) | — |

PRE displays absolute values. POST displays Δ values relative to the paired PRE. The M/S choice
also selects the independent MAX value for the current Watch playback pass.
In the Watch grid, the live 400 ms True Peak and its playback-pass MAX are shown side by side.
Pressing **Keep** changes the grid labels; **Max TP** then means the maximum for the whole Keep
session, including across transport stops.

### POST Analysis: Spectrum (on demand)

A paired POST provides an optional **ANALYSIS** page for inspecting the processing between PRE and
POST. Analysis runs only while this page is open. The signed **Δ (POST − PRE)** curve is the primary
display, with absolute PRE and POST spectra retained as reference curves. Δ is shown on a ±24 dB
scale; the underlying difference is not clipped. A difference is produced only when PRE and POST
frames have the same sample rate, FFT layout, channel mode, channel count, and output-presentation
sample endpoint.

| Control | Observation behavior |
|---|---|
| LR / MID / SIDE | Selects exactly one channel definition; the three analyzers never run in parallel |
| Hover / click | Reads frequency and Δ; click locks the probe, shows its six-second Focus Trail, and × releases it |
| MARK | Captures or replaces one temporary display-only Δ reference; × clears it |
| 100% / 125% / 150% | Resizes the POST Spectrum page and remembers the choice for the loaded instance |

The page analyzes one selected channel view at a time. **LR** transforms L and R independently and
averages their power, so opposite-polarity channels do not cancel. **MID** analyzes the `(L+R)/2`
waveform. **SIDE** analyzes `(L−R)/2` and is available only for stereo input; mono never fabricates a
SIDE result. Switching LR / MID / SIDE clears the old frame and waits for an exact PRE/POST match in
the newly selected mode. Record-mode N and Sharpness use their own independent-channel definition,
described below, so they can differ from MID or SIDE Spectrum on wide or phase-opposed material.

Hovering the plot shows frequency and Δ; the 125% and 150% views also show PRE and POST values. A
click in the plot locks that readout to the same frequency until its × is pressed. While locked,
**Focus Trail** shows the last six seconds of Δ at that frequency: compact at 100%, with its own lane
at 125% and 150%. Its newest point is the same exact PRE/POST presentation frame as the live Δ, not a
UI-clock estimate. Missing, reversed, or incompatible frames clear the trail instead of joining a
false history. **MARK** freezes one display-only Δ curve beneath the live curve; pressing MARK again
replaces it, and its × clears it.
MARK is temporary and is cleared when the pair, sample rate, FFT layout, channel mode, or page changes.
It neither adds another analyzer nor changes the measured values. The 100% / 125% / 150% size choice
is remembered while that plug-in instance remains loaded, while Spectrum itself still opens off.
Focus Trail retains only fixed-capacity display snapshots while Spectrum is open. It adds no analyzer,
does not smooth or delay the live Δ, and is discarded on pair, rate, layout, channel-mode, or page
changes.

The optional PRE/POST file exchange is supervised by the existing 10 Hz IO update. If its dedicated
presentation worker stops making progress, that stable path performs one non-blocking exact exchange
before the 1.5-second request lease can expire. It does not start a second analyzer, join mismatched
frames, or move any filesystem work onto the Audio Thread. This recovery applies equally to FREQ and
SHARP.

Where both spectra are extremely quiet, the displayed Δ alone is faded toward zero: it is fully
suppressed at and below −120 dBFS and reaches full strength at −96 dBFS. This display floor does not
alter the captured PRE or POST values. The analyzer follows the host sample rate, using a 4096-sample
Hann window, an 8192-point FFT, and a 30 Hz analysis cadence. The live curve presents the newest
frame at 12 Hz and numeric probe values at 2 Hz, without averaging or changing the measured
endpoint. At 48 kHz the window spans about
85 ms; FFT-point spacing changes with the host rate. It compares measured programme energy rather
than reconstructing a plug-in transfer function, so narrow low-frequency EQ shapes can appear broader
than the corresponding EQ control graph.

### POST Analysis: Perceptual Δ (on demand)

From the POST analysis page, **SHARP** opens Perceptual Δ and **FREQ** returns to Spectrum. The first
Perceptual Δ observation is **Δ Sharpness History**. It plots the signed Sharpness difference
`POST − PRE` over the latest six seconds, with the newest exact value shown in acum. The measured
difference is not clipped; the stable display scale is ±2 acum. The curve is spatially rounded for
legibility, but no temporal smoothing delays it. All measured 10 Hz points remain in a retained
six-second timeline: the curve is repainted at 5 Hz and the exact PRE / POST / Δ numbers are held
for 500 ms. A delayed UI update catches up from that factual timeline instead of exposing scheduling
jitter as broken segments. A true measurement discontinuity starts one clean new run; no missing
value is interpolated.

Each point comes from one non-overlapping 100 ms aperture and is published at 10 Hz. Before the
first point, PRE reports readiness and POST commits one shared, aperture-aligned presentation epoch
at least 200 ms in the future. PRE and POST reset their Phase D and optional sample-rate-converter
state once at that epoch, then preserve that state continuously across later apertures. They must
match in schema, host sample rate, aperture length, state epoch, channel definition, channel count,
and output-presentation sample endpoint. If any of those facts differ, no Δ point is produced.

The psychoacoustic engine runs at its defined 48 kHz analysis rate after following the host-rate
input; at 44.1, 48, 96, or 192 kHz each endpoint still represents exactly 100 ms of host
presentation time. A non-48 kHz converter may buffer an additional chunk before publishing an
already-measured endpoint; it does not shift or interpolate that endpoint. A dropped block,
timeline discontinuity, mode edge, or missed epoch clears the short history and requires a new
shared future epoch instead of continuing state across a gap.

**LR** measures L and R independently and uses their arithmetic mean, so channel polarity cannot
cancel the observation. **MID** measures `(L+R)/2`; **SIDE** measures `(L−R)/2` and remains stereo-
only. Changing channel mode starts a new history. Closing the page stops Sharpness analysis and
discards the display history. Switching to Spectrum stops Sharpness before the FFT starts, and vice
versa. All of this is display-only: it neither changes audio nor rewrites Watch or Record results.

### Record mode (Kirin OS required)

| Metric | Window / Unit | Standard |
|---|---|---|
| LUFS-M / LUFS-S | Selectable Momentary (400 ms) or Short-term (3 s) loudness (LUFS) | ITU-R BS.1770-4 |
| Integrated Loudness | Current Keep session (LUFS) | ITU-R BS.1770-4 |
| Max True Peak | Current Keep session maximum (dBTP) | ITU-R BS.1770-4 |
| Crest Factor | Peak − RMS, 400 ms (dB) | — |
| PSR | Peak-to-Short-term Ratio, 3 s (dB) | — |
| Sharpness | acum, independent-channel arithmetic mean | DIN 45692 |

PRE displays all six values. A paired POST displays Δ for the selected M/S loudness, PSR, Crest,
and Sharpness; Integrated Loudness and Max True Peak remain absolute POST session values. The
Integrated value spans transport stops within one Keep. After Stop, the final Record result remains
visible until the first newly computed Watch result arrives.
PSR always uses the engine's 3 s Short-term loudness, regardless of the M/S selector. The selector
changes the loudness value displayed and compared; it does not redefine PSR.

**On True Peak.** Two distinct True Peak quantities are reported. The *recent peak* is the maximum inter-sample peak within the last 400 ms (the same window as LUFS-M) and is shown live in Watch mode; it is not held, so a transient drops out of the reading once that window has passed. The *session maximum* is the running maximum inter-sample peak over the whole recording and is what the Record data stores. When a single dBTP figure is quoted for a file, it is the session maximum. Peak windows are tracked by sample count, so offline / faster-than-real-time rendering does not shift them.

**On Crest Factor.** Crest Factor is the sample-peak level minus the RMS level (both in dBFS) over the same 400 ms window. Peak and RMS are both computed across the pooled samples of all channels — not a mono sum — and the peak is a sample peak, not the inter-sample True Peak. A silent window produces no value (shown as `---`).

**On the psychoacoustic metrics.** N (Zwicker loudness) remains measured and stored for Kirin OS
even though it is no longer one of the six DAW display cells. For multichannel Record data, N and
Sharpness are measured independently per input channel and combined by arithmetic mean. They do not
sum the waveform before the nonlinear psychoacoustic pipeline. Perceptual Δ uses that same definition
for LR, while its explicit MID and SIDE selections intentionally measure `(L+R)/2` and `(L−R)/2`.

---

## Download

Download the latest signed and notarized macOS release from the [Releases page](https://github.com/heyalohaloha/kirin_hypha/releases).

The macOS installer package and the plug-in bundles inside it are signed with Apple Developer ID certificates and notarized by Apple, so Gatekeeper normally opens them without a warning. If a downloaded file is still flagged — for instance when the quarantine attribute persists — you can inspect it and clear the flag yourself:

```bash
# Inspect the installer signature
pkgutil --check-signature "Kirin-Hypha-<version>-macOS-Universal.pkg"

# Verify the download against the SHA-256 shown on the Releases page
shasum -a 256 "Kirin-Hypha-<version>-macOS-Universal.pkg"

# Remove the quarantine attribute if macOS blocks the installer
xattr -d com.apple.quarantine "Kirin-Hypha-<version>-macOS-Universal.pkg"
```

The installer package has companion `.pkg.sha256` and artifact JSON assets on the Releases page. Older zip archives are manual-install fallback artifacts.

The Windows VST3 zip on the Releases page is a **validation candidate**, not a supported installer. It is a manual PRE/POST VST3 package for Windows 10/11 64-bit, has no installer or Authenticode signature, and remains blocked from supported release status until the external DAW and audio-transparency gates in [`docs/windows_external_validation.md`](docs/windows_external_validation.md) are complete.

### Release provenance

Published artifacts are immutable. If repository maintenance changes a public commit ID after a release, the commit recorded in that artifact remains its original build commit. [`docs/release_commit_map.json`](docs/release_commit_map.json) maps an affected artifact commit to the commit currently referenced by its release tag and records the verified source-equivalence scope.

---

## Installation

1. Download the latest `Kirin-Hypha-<version>-macOS-Universal.pkg` from the [Releases page](https://github.com/heyalohaloha/kirin_hypha/releases).
2. Open the installer package and follow macOS Installer.
   - The package installs **VST3** to `/Library/Audio/Plug-Ins/VST3/`.
   - The package installs **Audio Unit** to `/Library/Audio/Plug-Ins/Components/`.
   - The package removes old user-level and system-level Kirin Hypha PRE/POST copies before installing, so DAWs do not load stale bundles first.
3. Rescan plugins in your DAW.
4. Insert **PRE Kirin Hypha** before your processing chain.
5. Insert **POST Kirin Hypha** after your processing chain.

---

## Sandbox & privacy (Audio Unit)

The Audio Unit declares a broad file-access entitlement (`temporary-exception.files.all.read-write`), which the AU sandbox requires for the plug-in to persist its session data (`.kirin` and plug-in data) on disk. The build declares no network entitlement and links no networking or web frameworks — you can confirm this with `codesign -d --entitlements - "Kirin Hypha PRE.component"` and `otool -L "Kirin Hypha PRE.component"`.

---

## Pairing PRE and POST

Pairing is by **name**, not by track position.

1. In the PRE plugin, enter a name in the **Name** field (e.g. `Mix Bus`, `Kick`, `Vocal`). Names accept UTF-8 text, including Japanese.
2. In the POST plugin, enter the same name.
3. POST detects the matching PRE and begins displaying Δ values.

Multiple PRE / POST pairs can run simultaneously (up to 12 active pairs per project).

---

## Watch mode

Real-time display of selectable LUFS-M / LUFS-S, True Peak (recent), and Crest Factor during
playback. The M and S selections retain independent playback-pass maximums. POST displays the
difference between its own measurements and the paired PRE.
On a paired POST, **ANALYSIS** can be opened on demand. Its **FREQ** Spectrum view shows the signed
POST − PRE frequency difference, and **SHARP** switches the same page to on-demand **Perceptual Δ**
with a six-second Δ Sharpness History. The views are mutually exclusive: only the currently visible
analyzer runs, and only one POST Analysis page per DAW process may own the optional PRE/POST analysis
pair at a time.

Closing the GUI does not stop measurement. The audio thread continues running as long as the plugin is loaded in the DAW.

---

## Record mode (Kirin OS required)

With a Kirin OS license, the POST plugin shows a **Keep** button in Watch mode.

1. Press **Keep** to begin a session recording.
2. Press **Stop** to end the session. The session is written to the `.kirin` record.

Integrated Loudness and session-maximum True Peak accumulate across DAW transport stops while the
same Keep remains active. During an offline bounce/export, POST auto-runs the same cleanup as
**Stop** when the host reports that offline processing has ended. If a host does not emit that
offline-end edge, Keep remains armed until manual **Stop** or the idle auto-stop backstop after
10 minutes without Active signal.

After Stop, the final Record display remains visible until the next playback produces its first
newly computed Watch result; that result returns the grid to Watch without showing stale Watch data
in between. Multiple pairs record independently.

If measurement samples are ever dropped during a recording (for example, on a buffer overflow), the dropped-sample count and an integrity flag are written into the session data. Incomplete measurement is recorded as incomplete, never presented as complete.

---

## Kirin OS ecosystem

Kirin Hypha is one piece of a larger ecosystem. With Kirin OS, session data is written to `plugin_data` in a structured JSON schema and can be bundled with C2PA provenance into a tamper-evident `.kirin` file alongside the audio.

Hypha itself remains **standalone and free** — Kirin OS is not required to use Watch mode.

Kirin OS is available now. More at [kirinmastering.com](https://kirinmastering.com).

---

## Requirements

- macOS 12 or later (Apple Silicon and Intel)
- VST3- or Audio Unit-compatible DAW

Tested on macOS 14 (Sonoma).

**Windows validation candidate:** Windows 10 or 11, 64-bit, VST3 only. External validation is pending; this is not yet a supported release.

**Not currently supported:** Linux · CLAP

---

## Building from source

```bash
git clone https://github.com/heyalohaloha/kirin_hypha.git
cd kirin_hypha
scripts/build_juce_universal.sh
scripts/validate_macos_pluginval.sh juce_shell/build-universal
```

Requires Rust stable toolchain, CMake, Xcode command line tools, and the pinned JUCE submodule.
The macOS release ship set is one JUCE role-parameterised processor/editor compiled as AU and VST3. The VST3 wrapper preserves the original component IDs and migrates legacy nih-plug state so existing DAW sessions keep their identities and pair names.

Run the macOS pluginval gate before opening Studio One for manual validation. It recreates the exact role-first installed layout (`PRE Kirin Hypha.vst3` / `POST Kirin Hypha.vst3`) in an isolated runtime directory, resolves each executable through `CFBundleExecutable`, verifies the preserved component IDs and host names, and then runs pluginval at strictness level 5 against those staged bundles. Logs are written to `target/pluginval/logs/macos`, while plug-in runtime writes stay under `target/pluginval/runtime/macos/`. Override with `PLUGINVAL_STRICTNESS_LEVEL=10` only for the slower stress pass. If Steinberg's VST3 validator is installed, pass it with `VST3_VALIDATOR_BIN=/path/to/validator`.

## Maintainer release packaging

On the release machine, after signing and notarizing the four source plug-in bundles with `cargo run --package xtask -- notarize`, build the Lemon Squeezy installer package with the Kirin OS-style release scripts:

```bash
node scripts/ls_release/build_kirin_hypha_pkg.mjs
mkdir -p release_state
cp docs/ls_release/kirin_hypha_ls_state.example.json \
  release_state/kirin_hypha_X.Y.Z_ls.state.json
node scripts/ls_release/kirin_hypha_ls_dry_run.mjs \
  --state release_state/kirin_hypha_X.Y.Z_ls.state.json \
  --with-apple-verification
```

`release_state/` is deliberately ignored: it contains release-operator targets and upload readiness, not source. Fill the local state from the generated `.pkg.json` sidecar and keep it out of commits. Publish the `.pkg.json` artifact manifest and `.pkg.sha256` with the GitHub Release.

The installer package requires a `Developer ID Installer` certificate in the keychain. A `Developer ID Application` certificate signs the plug-in bundles, but it is not enough to sign the `.pkg`.

Unsigned smoke-test packages are intentionally marked `UNSIGNED-DO-NOT-UPLOAD` and are written under `/tmp/kirin_hypha_pkg_smoke/`:

```bash
KIRIN_SKIP_PKG_SIGN=1 KIRIN_SKIP_PKG_NOTARIZE=1 \
  node scripts/ls_release/build_kirin_hypha_pkg.mjs
```

The legacy manual-install zip can still be built with:

```bash
cargo run --package xtask -- release-package
```

Do not run the signed release checks inside a sandboxed child process; macOS `codesign` can report a false `invalid signature` for valid notarized plugin bundles in that context.
Upload only `Kirin-Hypha-<version>-macOS-Universal.pkg` to the configured Lemon Squeezy products after local verification passes.

---

## License

[GNU General Public License v3.0](LICENSE)

Kirin Hypha is released under GPLv3 to keep the measurement layer auditable. The numbers a tool produces should be inspectable — any user, researcher, or engineer can read the code that generated them. Derivative works inherit the same openness.

---

## Acknowledgements

Built on [nih-plug](https://github.com/robbert-vdh/nih-plug) by Robbert van der Helm.

---

*Kirin Hypha — observation, kept simple.*
