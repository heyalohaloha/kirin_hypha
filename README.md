# Kirin Hypha

**A free, open-source audio measurement plugin for macOS (VST3).**

Kirin Hypha operates as paired instances — a **PRE** plugin and a **POST** plugin — to measure signal states before and after a processing chain and display the difference.

---

## Design

Kirin Hypha is built to **observe, not to advise**.

It produces measurement data. It does not generate, modify, or attenuate audio. The same input file produces the same output, every time. Numbers are reported as captured — no interpretation, no scoring, no recommendation.

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

| Metric | Unit | Standard |
|---|---|---|
| LUFS-M | Short-term loudness (LUFS) | ITU-R BS.1770 |
| True Peak | dBTP | ITU-R BS.1770 |
| Crest Factor | dB | — |

PRE displays absolute values. POST displays Δ values relative to the paired PRE.

### Record mode (Kirin OS required)

| Metric | Unit | Standard |
|---|---|---|
| LUFS-M | Short-term loudness (LUFS) | ITU-R BS.1770 |
| True Peak | dBTP | ITU-R BS.1770 |
| Crest Factor | dB | — |
| PSR | Peak-to-Short-term Ratio (dB) | — |
| N (Zwicker loudness) | sone | ISO 532-1 |
| Sharpness | acum | DIN 45692 |

PRE displays all six values. POST displays Δ values for all six. POST values freeze on Stop and hold until the next session.

---

## Screenshots

![Kirin Hypha in Watch mode](docs/images/hypha_watch_mode.jpg)

*Watch mode — real-time measurement. PRE shows absolute values, POST shows Δ relative to the paired PRE.*

![Kirin Hypha in Record mode](docs/images/hypha_record_mode.jpg)

*Record mode — POST Δ values freeze on Stop and hold until the next session (Kirin OS required).*

---

## Download

Download the latest signed and notarized release from the [Releases page](https://github.com/heyalohaloha/kirin_hypha/releases).

Binaries are signed with an Apple Developer ID and notarized by Apple. No Gatekeeper warnings.

---

## Installation

1. Download the latest release archive from the [Releases page](https://github.com/heyalohaloha/kirin_hypha/releases).
2. Unzip and copy both bundles to:
~/Library/Audio/Plug-Ins/VST3/
3. Rescan plugins in your DAW.
4. Insert **Kirin Hypha PRE** before your processing chain.
5. Insert **Kirin Hypha POST** after your processing chain.

---

## Pairing PRE and POST

Pairing is by **name**, not by track position.

1. In the PRE plugin, enter a name in the **Name** field (e.g. `Mix Bus`, `Kick`, `Vocal`).
2. In the POST plugin, enter the same name.
3. POST detects the matching PRE and begins displaying Δ values.

Multiple PRE / POST pairs can run simultaneously (up to 12 active pairs per project).

---

## Watch mode

Real-time display of LUFS-M, True Peak, and Crest Factor during playback.
POST displays the difference between its own measurements and the paired PRE.

Closing the GUI does not stop measurement. The audio thread continues running as long as the plugin is loaded in the DAW.

---

## Record mode (Kirin OS required)

With a Kirin OS license, the POST plugin shows a **Keep** button in Watch mode.

1. Press **Keep** to begin a session recording.
2. Press **Stop** to end the session. POST freezes and displays session Δ values.
3. Optionally press **Note** to attach an annotation ([Good] / [Fix] / [Hold]).

Multiple pairs record independently. Values remain frozen until the next Keep is pressed.

---

## Kirin OS ecosystem

Kirin Hypha is one piece of a larger ecosystem. With Kirin OS, session data is written to `plugin_data` in a structured JSON schema and can be bundled into a C2PA-signed `.kirin` file alongside the audio.

Hypha itself remains **standalone and free** — Kirin OS is not required to use Watch mode.

Kirin OS launches June 6, 2026. More at [kirinmastering.com](https://kirinmastering.com).

---

## Requirements

- macOS 12 or later (Apple Silicon and Intel)
- VST3-compatible DAW

Tested on macOS 14 (Sonoma).

**Not currently supported:** Windows · Linux · CLAP · AU

---

## Building from source

```bash
git clone https://github.com/heyalohaloha/kirin_hypha.git
cd kirin_hypha
cargo run --package xtask -- bundle hypha_pre --release
cargo run --package xtask -- bundle hypha_post --release
```

Requires Rust stable toolchain. Built bundles are written to `target/bundled/`.

---

## License

[GNU General Public License v3.0](LICENSE)

Kirin Hypha is released under GPLv3 to keep the measurement layer auditable. The numbers a tool produces should be inspectable — any user, researcher, or engineer can read the code that generated them. Derivative works inherit the same openness.

---

## Acknowledgements

Built on [nih-plug](https://github.com/robbert-vdh/nih-plug) by Robbert van der Helm.

---

*Kirin Hypha — observation, kept simple.*
