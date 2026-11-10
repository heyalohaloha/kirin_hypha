# Installation and Audio Unit sandbox

The macOS PKG installs AU, VST3 and AAX into the system plug-in folders. The Windows EXE installs
VST3 and AAX for all users and needs administrator permission. This page lists the folders, what
each installer removes first, and the Audio Unit file-access declaration.

[Back to the README](../../README.md#the-full-manual)

## Installation

### macOS

1. Download the latest `Kirin-Hypha-<version>-macOS-Universal.pkg` from the [Releases page](https://github.com/heyalohaloha/kirin_hypha/releases).
2. Open the installer package and follow macOS Installer.
   - The package installs **VST3** to `/Library/Audio/Plug-Ins/VST3/`.
   - The package installs **Audio Unit** to `/Library/Audio/Plug-Ins/Components/`.
   - The package installs **AAX** to `/Library/Application Support/Avid/Audio/Plug-Ins/`.
   - The package removes old user-level and system-level Kirin Hypha PRE/POST copies before installing, so DAWs do not load stale bundles first.
3. Rescan plugins in your DAW.
4. Insert **PRE Kirin Hypha** before your processing chain.
5. Insert **POST Kirin Hypha** after your processing chain.

### Windows

1. Download the latest `Kirin-Hypha-<version>-Windows-x64-Setup.exe` and its `.sha256` companion
   from the [Releases page](https://github.com/heyalohaloha/kirin_hypha/releases).
2. Close the DAW and run the installer. The full VST3 + AAX installer requires administrator
   permission and installs both formats for all users. VST3 goes to
   `%ProgramFiles%\Common Files\VST3`; AAX goes to
   `%ProgramFiles%\Common Files\Avid\Audio\Plug-Ins`.
   - The installer first removes the older Kirin Hypha PRE/POST copy installed only for the Windows
     user who started it, also when another administrator account approves the installation, so DAWs
     do not keep loading it. Settings and DAW projects are kept. If a DAW still holds that copy, the
     installer changes nothing and stops; close every DAW and run it again. Start the installer by
     double-clicking it: with **Run as administrator** and another account's credentials, Windows
     starts it as that account, so that account counts as the user who started it.
3. Start your VST3 DAW or Pro Tools and rescan plug-ins if needed.
4. Insert **PRE Kirin Hypha** before your processing chain and **POST Kirin Hypha** after it.

The manual VST3 ZIP is a diagnostic and recovery fallback, not the normal installation path.

## Sandbox & privacy (Audio Unit)

The Audio Unit preserves its file-access declaration (`temporary-exception.files.all.read-write`) for
session and plug-in data. Builds without the official update verification key declare no network
access. A deliberately provisioned update build additionally declares `network.client` and the
update protocol marker; signing and packaging gates check these together.
