# Kirin Hypha

**Kirin Hypha** is a free, open-source audio measurement plugin for macOS (VST3).
It operates as paired instances — a **PRE** plugin and a **POST** plugin — to measure signal states before and after a processing chain and display the difference.

---

## What it measures

**Watch mode (real-time):**

| Metric | Unit |
|---|---|
| LUFS-M | Short-term loudness |
| True Peak | dBTP |
| Crest Factor | dB |
| N (Zwicker loudness) | sone · ISO 532-1 |
| Sharpness | acum · DIN 45692 |

**Record mode (per session):**

| Metric | Unit |
|---|---|
| LUFS-I | Integrated loudness |
| LRA | Loudness Range |
| PLR | Peak-to-Loudness Ratio |
| PSR | Peak-to-Short-term loudness Ratio |

The POST plugin displays Δ (difference) values relative to the paired PRE instance.

---

## Requirements

- macOS 12 or later (Apple Silicon and Intel)
- VST3-compatible DAW (Logic Pro, Reaper, Studio One, Cubase, etc.)

---

## Installation

1. Copy `Kirin Hypha PRE.vst3` and `Kirin Hypha POST.vst3` to
   `~/Library/Audio/Plug-Ins/VST3/`
2. Rescan plugins in your DAW
3. Insert **Kirin Hypha PRE** before your processing chain
4. Insert **Kirin Hypha POST** after your processing chain

---

## Pairing PRE and POST

**Naming is required for pairing.**

1. In the PRE plugin, enter a name in the **Name** field (e.g. `Mix Bus`, `Kick`, `Vocal`)
2. In the POST plugin, enter the same name in the **Name** field
3. The POST plugin detects the matching PRE and begins displaying Δ values

Multiple PRE / POST pairs can run simultaneously within a single session.

---

## Watch mode

Real-time LUFS-M, True Peak, Crest Factor, N, and Sharpness during playback.
POST displays Δ values relative to the paired PRE.

---

## Record mode

Press **Record** to begin a session measurement.
Press **Stop** to capture LUFS-I, LRA, PLR, and PSR.
POST freezes and displays session Δ values until the next session begins.

---

## Building from source

```bash
git clone https://github.com/heyalohaloha/kirin_hypha.git
cd kirin_hypha
cargo run --package xtask -- bundle hypha_pre --release
cargo run --package xtask -- bundle hypha_post --release
```

Requires Rust stable toolchain. Tested on macOS 14 (Sonoma).

---

## License

[GNU General Public License v3.0](LICENSE)
