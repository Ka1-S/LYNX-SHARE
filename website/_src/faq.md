---
title: FAQ
summary: Short answers to what newcomers ask about LYNX SHARE.
status: full
---
## Is LYNX SHARE free?

Yes. It is free software under the GNU Affero General Public License v3.0 (AGPL-3.0). You may use, study, modify and redistribute it. Anyone who distributes a modified version must publish its source code under the same licence.

## Which systems does it run on?

Windows 10 and 11 (64-bit), macOS 11 or newer (Apple Silicon and Intel) and Linux x86_64 with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36 or newer). The files are on the [download page](/download/).

## Is this FL Studio, or made by Image-Line?

No. LYNX SHARE is an independent program with an FL Studio-style workflow. FL Studio's behaviour and file format were studied clean-room, no Image-Line code was copied, and LYNX SHARE is not affiliated with Image-Line.

## Does it open my FL Studio projects, and what is lost?

`File › Open...` reads `.flp` files: channels, samples, patterns, arrangements, automation, the Mixer and third-party plugins you have installed. FL Studio's native plugins play on recreated engines, most of them exact or near-exact and a few approximate. FLEX packs are never opened. An import report lists what was approximated or left out. See [Coming from FL Studio](/wiki/coming-from-fl-studio/).

## Does it open Ableton Live sets?

Yes, sets saved by Live 11 and 12 (`.als`): tracks, clips, mixer settings, automation and VST2 or VST3 plugins. Live's own effects are dropped, racks keep their first chain, and Simpler, Sampler and drum racks become LYNX SHARE samplers. See [Coming from Ableton Live](/wiki/coming-from-ableton/).

## Does it open FL Studio Mobile projects?

Partly. A `.flm` opens with its arrangement, samples, drum kits and DirectWave programs. FLM's effects are not imported yet.

## Can I send a project back to FL Studio or Live?

Yes. `File › Export › FL Studio project...` writes a `.flp`, and `File › Export › Ableton Live set (.als)...` writes a set for Live 12 or Live 11. What the other program does not have is rendered to audio or left out, and a report says which. The limits are on the [File formats](/wiki/file-formats/) page.

## Do my VST plugins work?

VST3 and CLAP plugins load on every system. VST2 loads on Windows and Linux, Audio Units on macOS and LV2 on Linux. On Windows, 32-bit VST2 and VST3 plugins run through a bridge. See [Plugins](/wiki/plugins/).

## Is it stable?

It is an alpha. It opens, plays and exports whole projects, but it is not yet a replacement for the DAW you rely on every day. Save often and report what breaks.

## Why does Windows or macOS warn when I open it?

The Windows executable is not code-signed yet, so SmartScreen may warn: click `More info`, then `Run anyway`. The macOS app is not notarized by Apple: right-click it and choose `Open` the first time. The steps are on the [Troubleshooting](/wiki/troubleshooting/) page.

## Does it send any data?

There is no telemetry, analytics, account or licence check. It checks GitHub for a new version (you can turn this off in `Settings › General › Updates`), and it sends a crash report only if you agree each time or set it to always send. It also connects when you ask for a stem model download or take part in a live online session. The [privacy policy](https://github.com/Ka1-S/LYNX-SHARE/blob/main/PRIVACY.md) has the details.

## How do updates arrive?

When a newer version exists, a bar under the toolbar offers `Update`. The Windows installer version installs it over itself. The portable and macOS versions save the new file to your Downloads folder. The Linux AppImage updates through AppImageUpdate or Gear Lever. The Microsoft Store version is updated by the Store and has no `Update` button. Release notes are on the [updates page](/updates/).

## Which languages does the interface have?

Fifteen: English, Simplified Chinese, Traditional Chinese, Dutch, French, German, Indonesian, Italian, Japanese, Korean, Polish, Brazilian Portuguese, Russian, Spanish and Turkish.

## How do I report a bug or contribute?

Open an issue at [github.com/Ka1-S/LYNX-SHARE/issues](https://github.com/Ka1-S/LYNX-SHARE/issues), or use `Help › Report a problem...` inside the program. The source code is in the same repository, and pull requests from contributors have been merged in past releases.
