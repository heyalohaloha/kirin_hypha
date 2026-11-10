# Building from source

`node scripts/build_hypha.mjs` builds unsigned PRE/POST plug-ins on macOS and Windows;
`--without-aax` needs no AAX SDK or iLok. This page lists the prerequisites, the full-format
build and the pluginval check.

[Back to the README](../../README.md#the-full-manual)

Contributors: read [CONTRIBUTING.md](../../CONTRIBUTING.md) and [AGENTS.md](../../AGENTS.md). The
[build guide](../hypha_build_entry.md) documents unsigned native builds, and
[release qualification](../hypha_release_entry.md) covers signing, acceptance and publication.
`node scripts/build_hypha.mjs --help` and `--release --help` work without SDKs, credentials or iLok.
For a quick unsigned GUI/DSP test build, run `node scripts/build_hypha.mjs --without-aax`
(Mac: PRE/POST AU+VST3 Universal; Windows: PRE/POST VST3 x64; no AAX SDK or iLok). The default still
builds all formats; this shortcut is not a full-format or release gate.

For all supported formats in one local build, use
[`node scripts/build_hypha.mjs`](../hypha_build_entry.md): macOS produces PRE/POST × AAX/AU/VST3
as Intel + Apple Silicon Universal bundles; Windows produces PRE/POST × AAX/VST3 for x64.
An external licensed AAX SDK is required. The same command runs natively on each OS:

```bash
node scripts/build_hypha.mjs --sdk /absolute/external/aax-sdk-root --license-confirmed
```

This is an unsigned diagnostic build, with no iLok, CI, installation or publication step.
Use `--dry-run` to inspect the plan and `--verify-only` to recheck saved binaries without rebuilding.
Signing and release qualification retain their independent mandatory gates.

For the complete workflow **through the homepage update**, use the same entry's
[`--release` mode](../hypha_release_entry.md). It coordinates the approved signing/notarization
producers, same-commit Windows installer, the acceptance steps, Lemon Squeezy verification,
immutable GitHub Release, EN/JA homepage links, staged production deployment and public download
hash checks. Human host and Lemon Squeezy checkpoints and explicit publication authorization remain
mandatory.

```bash
node scripts/build_hypha.mjs --release --help
```

For a quick AAX-free GUI/DSP build on either supported OS:

```bash
git clone --recurse-submodules https://github.com/heyalohaloha/kirin_hypha.git
cd kirin_hypha
node scripts/build_hypha.mjs --without-aax
```

This creates PRE/POST AU + VST3 Universal on macOS and VST3 x64 on Windows. It requires no AAX SDK
or iLok and does not replace the full-format release gates. Follow the
[build guide](../hypha_build_entry.md) for toolchains, output paths and validation.

Requires Node.js, Git, CMake, Rust stable and the pinned JUCE submodule. macOS also needs the Xcode
toolchain and both Rust CPU targets; Windows needs Visual Studio Build Tools with C++ and the
Windows SDK, Git Bash and the Rust MSVC x64 target. The [build guide](../hypha_build_entry.md)
lists the platform prerequisites.
The shipping formats share one JUCE role-parameterised processor/editor: PRE/POST × AU/VST3/AAX
on macOS and PRE/POST × VST3/AAX on Windows. The VST3 wrapper preserves the original component IDs
and migrates legacy nih-plug state so existing DAW sessions keep their identities and pair names.

For the AAX-free macOS build above, run the VST3 gate before manual DAW validation:

```bash
scripts/validate_macos_pluginval.sh target/hypha-build/macos-universal-no-aax
```

For the default full-format diagnostic build, pass `target/hypha-build/macos-universal` instead.
This VST3 check recreates the exact role-first installed layout (`PRE Kirin Hypha.vst3` / `POST Kirin Hypha.vst3`) in an isolated runtime directory, resolves each executable through `CFBundleExecutable`, verifies the preserved component IDs and host names, and then runs pluginval at strictness level 5 against those staged bundles. Logs are written to `target/pluginval/logs/macos`, while plug-in runtime writes stay under `target/pluginval/runtime/macos/`. Override with `PLUGINVAL_STRICTNESS_LEVEL=10` only for the slower stress pass. If Steinberg's VST3 validator is installed, pass it with `VST3_VALIDATOR_BIN=/path/to/validator`.
