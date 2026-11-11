---
title: Install
summary: Install, update and remove LYNX SHARE on Windows, macOS and Linux.
status: full
---
Every release has builds for Windows, macOS and Linux on the [releases page](https://github.com/Ka1-S/LYNX-SHARE/releases). The files are built from the tagged source by GitHub Actions and listed with their SHA-256 in `SHA256SUMS.txt`. The [Download](/download/) page links to the current ones.

## Install on Windows

Windows 10 or 11, 64-bit.

1. Download `LYNX-<version>-setup-x64.exe`.
2. Run it. It installs for your user only and needs no admin rights.
3. SmartScreen may warn, because the executable is not code-signed yet. Choose `More info`, then `Run anyway`.
4. Start LYNX SHARE from the Start menu or the desktop shortcut.

The installer makes LYNX SHARE the program that opens `.lynx` projects. It offers LYNX SHARE under *Open with* for `.flp` files without changing FL Studio's default.

There are two other ways:

| Way | File or place | What it does |
|---|---|---|
| Portable zip | `LYNX-<version>-windows-x64.zip` | Extract it anywhere and run `LYNX.exe`. Nothing is installed. |
| Microsoft Store | [LYNX SHARE on the Store](https://apps.microsoft.com/detail/9NQRFBFDCX6N) | The Store installs it and keeps it updated. |

## Install on macOS

macOS 11 Big Sur or newer. One universal `LYNX-<version>-macos-universal.dmg` covers Intel and Apple silicon.

1. Open the `.dmg` and drag LYNX SHARE to *Applications*.
2. The app is not notarized by Apple and has no Developer ID signature, so the first launch needs an extra step. Right-click (or Control-click) LYNX SHARE in *Applications*, choose *Open*, then *Open* again.
3. On macOS 15 Sequoia or newer, try to open it once, then go to *System Settings › Privacy & Security* and click *Open Anyway*.

The Terminal alternative:

```
xattr -dr com.apple.quarantine /Applications/LYNX.app
```

macOS asks for microphone access the first time you record.

## Install on Linux

x86_64 with glibc 2.35 or newer (Ubuntu 22.04+, Debian 12+, Fedora 36+, current Arch or Manjaro). LYNX SHARE needs X11 or XWayland. Audio goes through PipeWire, JACK or ALSA, and nothing extra has to be installed for that.

AppImage (`LYNX-<version>-x86_64.AppImage`):

```
chmod +x LYNX-*.AppImage
./LYNX-*.AppImage
```

Some distributions need libfuse2 to run AppImages (`libfuse2` on Ubuntu 22.04, `libfuse2t64` on 24.04, `fuse2` on Arch, `fuse-libs` on Fedora). On the first start the setup wizard offers to add LYNX SHARE to the applications menu and to open `.lynx` files with it. You can switch that later in `Options › General settings...`, or from a terminal with `./LYNX-*.AppImage --menu-entry on` (or `off`).

If you would rather not install libfuse2, use `LYNX-<version>-linux-x86_64.tar.gz`: extract it and run `./install.sh`. That installs LYNX SHARE for your user under `~/.local` with a menu entry, an icon and the `.lynx` file type.

## Where settings are kept

| System | Settings folder |
|---|---|
| Windows | `%APPDATA%\LYNX` |
| macOS | `~/Library/Application Support/LYNX` |
| Linux | `~/.config/LYNX` (or under `$XDG_CONFIG_HOME`) |

The folder holds your options, the chosen language (`language.txt`), your shortcuts (`keys.json`), your plugin folders (`plugin-folders.txt`) and the list of scanned plugins (`plugins.xml`).

## Update

At start-up LYNX SHARE asks GitHub for the newest version number. Nothing about you is sent. When a new version is out, a bar shows `LYNX SHARE <version> is available` with a `What's new` button and an update button. `Help › Check for updates...` asks at any time.

| Build | How it updates |
|---|---|
| Windows installer | `Update` downloads the new installer, asks about unsaved changes, installs and restarts |
| Windows portable zip, macOS | `Update` downloads the new file and shows it in its folder |
| Linux AppImage | `Update` updates the AppImage file in place. Gear Lever and AppImageUpdate can do the same from outside |
| Linux `.tar.gz` | `Download...` opens the download page |
| Microsoft Store | The Store updates it. This build has no update check of its own |

To turn the check off, open `Options › General settings...` and clear `Check for updates automatically`.

## Remove

- Windows installer: uninstall LYNX SHARE from the *Apps & features* list of Windows.
- Portable zip: delete the folder you extracted.
- Linux `.tar.gz`: run `./install.sh --uninstall`.

## Good to know

- The Windows builds are unsigned until the project's code-signing application is accepted. The macOS app has an ad-hoc signature only. Linux packages are not signed.
- To check a download, compare its SHA-256 with the line in `SHA256SUMS.txt` on the release page.
- The installer, the zip, the `.dmg` and the `.tar.gz` include the demo project `Demo.lynx`.
- Closing the update bar with its close button skips that version.

## Related

- [Start here](/wiki/getting-started/)
- [Audio setup](/wiki/audio-setup/)
- [Troubleshooting](/wiki/troubleshooting/)
- [Download](/download/)
- [Updates](/updates/)
