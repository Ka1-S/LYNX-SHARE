# lynx-share.com

The project website: a small static site. Plain HTML, CSS and JavaScript, no framework. Every page reads without
JavaScript; the script adds the language switch, direct download links and the small interactive parts.

| Page | Address | Source |
|---|---|---|
| Home | `/` | `_src/pages/home.html` |
| Features | `/features/` | `_src/pages/features.html` |
| Why LYNX | `/why/` | `_src/pages/why.html` (what only LYNX SHARE does, each with its limits) |
| What LYNX fixes | `/fixes/` | `_src/pages/fixes.html` (common complaints about DAWs and what the RELEASED version does about each; things only in the source go in its "Being built" list). Linked from Home and Why LYNX |
| Instruments and effects | `/instruments-and-effects/` | `_src/catalog.json`, `_src/catalog-img.json` |
| Download | `/download/` | `_src/pages/download.html` |
| Updates | `/updates/` | `_src/updates.json` |
| Wiki | `/wiki/`, `/wiki/<page>/` | `_src/wiki/*.md`, `_src/wiki/index.json` |
| Wiki, one page per built-in plugin | `/wiki/built-in/<id>/` | `_src/catalog.json` (the `wiki` part of each entry) |
| FAQ | `/faq/` | `_src/faq.md` |

`api/` (crash reports) is not generated and not touched by the build. `update/` (the AppImage update address) is never
in this tree: `scripts/deploy-site.sh` keeps the live one.

## Building

    python3 website/_src/build.py            # writes the pages, wiki/search.json and the English string files in i18n/
    python3 website/_src/build.py --check    # exit 1 if a written file is stale
    python3 website/_src/check-links.py      # every internal link, image and #anchor resolves
    python3 scripts/check-site-i18n.py       # every language has every string (also runs build --check)

Python 3 standard library only. **The built pages are committed**: edit the sources in `_src/`, never the
`index.html` files, then run the build and commit both. `_src/build.py` holds the shared header and footer, the
markdown subset the wiki uses (headings, lists, tables, code, links, one picture), and the page list.
`assets/site.css` and `assets/site.js` are shared by every page; `assets/features.js` (Features), `assets/catalog.js`
(the catalogue's filter) and `assets/wiki.js` (the wiki search over `wiki/search.json`, which the build writes) belong
to one page each. `assets/motion.js` is loaded last on every page: reveals on scroll, numbers that count up
(`data-count`), the glass header, tilt and glare on screenshots. The entrances at the top of a page and all hover
motion are CSS (`site.css`, "motion"). Motion is on only under `html.mo`, which the script in `<head>` sets unless the
visitor asked for reduced motion; a page hides something only when `motion.js` ran and the thing is below the fold, so
a failed script never leaves a blank page. Stills for checking a page: add `?shot` (no motion at all).
The server never serves `_src/` (`.htaccess`).

Look at the result with `python3 -m http.server -d website 8000` (links are root-absolute, so open it through a
server, not as files).

## Adding a release to the Updates page (release step)

1. Add ONE object at the top of `_src/updates.json`, written from the GitHub release notes in plain language:
   `version`, `tag`, `date` (UTC), `url`, `title` (what the release is about, not a slogan), `summary` (one or two
   sentences), then `new`, `better`, `fixed` and optionally `known`: lists of one- or two-sentence items. `**bold**`
   and `` `code` `` are allowed in items. `shots` (optional): `[{"src": "/assets/img/x.webp?v=<version>", "alt": "..."}]`,
   only pictures taken from that release's build.
2. `python3 website/_src/build.py`. The newest entry also fills the "Latest update" box on Home and the version
   shown on Home and Download.
3. Translate it: `python3 scripts/check-site-i18n.py --todo <id> todo.json` writes the English strings language
   `<id>` lacks; translate the values (keys and markup unchanged) and `--merge <id> todo.json`. Repeat for the 14
   languages, run the build again, then `scripts/check-site-i18n.py` must report 0 errors.
4. Refresh the screenshots in `assets/img/` if a window changed (they must come from the build people download),
   bump their `?v=` in the page sources, and deploy.
5. Built-in plugins: if the release added, removed or changed one, update `_src/catalog.json` and redraw the pictures
   (next section).

Editing the text of an existing item means its translations are stale: remove that key from the 14 language files
so the check reports it.

## The plugin catalogue and its pictures

`_src/catalog.json` has one entry per plugin built into the RELEASED version: `id`, `name` (as the app shows it),
`cat` (`synth`, `drums`, `sampler`; `eq`, `dynamics`, `reverb`, `delay`, `modulation`, `distortion`, `pitch`, `stereo`;
`controller`, `visual`, `routing`: the first three are instruments, the last three tools), `flp` (the FL Studio plugin
whose settings the engine opens from a `.flp`, or empty), `desc` (the card text, translated: key `c.<id>` in
`i18n/catalog/<lang>.json`), `img` (which render is its picture) and `wiki` (`intro`, `controls` rows, `tips`: the
plugin's wiki page, English only; `null` = no page, the card has no link). The count on Home and in the page
heading is the length of this file.

Pictures are drawn by the released program itself, off-screen (no window, no input):

    LYNX.exe --pluginthumbs R\thumbs        # every plugin, small: the list of what is built in
    LYNX.exe --artshot R\art after          # full-size editors
    LYNX.exe --plugui R\plugui              # more effect editors
    LYNX.exe --flextrashot R\extra
    LYNX.exe --showcase R\snap\Demo.lynx
    LYNX.exe --snapshot R\snap\s R\snap\Demo.lynx      # windows and dialogs: the wiki's pictures
    python3 website/_src/import-shots.py R               # needs Pillow; writes assets/img/plugins, assets/img/wiki

`import-shots.py` converts the render each entry names in `img` and every `/assets/img/wiki/<name>.webp` a page or
article uses (from `snap/s-<name>.png`), and writes the sizes to `_src/catalog-img.json` and `_src/img-sizes.json`.
The build puts `width` and `height` on every picture from those two files, and the stylesheet never shows a picture
above its own size. `--artshot` also draws each editor at twice the size: that becomes `<id>@2x.webp` (`srcset`). The
other modes of the program draw at 100% only, so window and dialog pictures have no @2x file. A plugin without a
render gets a name plate on its card, never a made-up picture.

**The logo in the pictures.** Since 2026-11-13 the brand is the logo by w3teal in its app-icon form (the mark with a
pepper stem on a dark tile: `resources/brand/lynx-share-logo.svg`; `python3 tools/make_logo.py site` writes
`assets/img/logo.png`, the favicons, `assets/img/lynx-share-logo.svg`, the README logo, the social preview and the
Store images from it). The pictures in `assets/img/` and `docs/img/` are still the 0.1.26 renders, whose program draws
the earlier pepper in the logo button of a plugin's header: `scripts/site-shots.py R --logo-was <0.1.26 logo>`
repainted that button with the mark (without the tile, on a dark disc, as the program draws it from 0.1.27). At the
next release, whose program draws the new logo itself, run `site-shots.py` again WITHOUT `--logo-was` so nothing is painted.

## 0.1.26 release: pictures and catalogue still to do (texts are ready)

The 0.1.26 texts are in the page sources; every picture is still from 0.1.25. No picture was added or changed for
0.1.26, because pictures must come from the released build. At the release, from the 0.1.26 build:

- **Windows and features** (bump `?v=0.1.25` to `?v=0.1.26` in `_src/pages/*.html` after replacing each file):
  `assets/img/hero.webp`, `playlist.webp`, `rack.webp`, `pianoroll.webp`, `mixer.webp`, `synth.webp`, `eq.webp`,
  `automation.webp`, `cliplaunch.webp` (now with the Clip View and Device View), and `assets/media/poster.jpg`.
  `lynx-tour.mp4` and `demo.mp3` are from 0.1.23: re-render or keep the line that says so.
- **Painted logos**: the plugin editors whose header logo button was repainted with the logo by w3teal (see "The logo
  in the pictures" above). The release that draws that logo itself: render them again with `site-shots.py` and
  without `--logo-was`.
- **Why LYNX pictures and captions**: the three captions that say "0.1.25" (`why.online.cap`, `why.work.cap`,
  `why.crash.cap`) go with their pictures: change the text (and drop the key from the 14 language files) when the
  picture is replaced. A shot of the Share plugins dialog and of the Simple workflow would fit the new paragraphs.
- **New pictures the texts would carry** (none is referenced yet, so nothing is broken without them): Simple
  workflow, Clip View with racks, Slicex and FPC editors, Freeze in the Channel rack, the loudness meter.
- **Catalogue** (`_src/catalog.json`, 193 entries): the 0.1.26 entries are written, with their wiki pages and 14
  translations: Slicex, FPC, GMSynth (FLM), Instrument Rack, MIDI Effect Rack, Audio Effect Rack, Strike Shaper and
  the 19 Pocket effects. They have no picture yet, so their cards and pages show the name plate. Each entry's `img`
  already names the render `LYNX.exe --pluginthumbs R\thumbs` writes for it (the name is the picker key folded by
  `pluginthumbs::fileNameFor` in `src/ui/PluginThumbs.cpp`: slug plus the FNV-1a hash of the key), so at the release
  it is only: draw the thumbs with the 0.1.26 build, run `import-shots.py R`, build. Expected files:

  | Plugin | Render in `R/thumbs/` | Picture the import writes |
  |---|---|---|
  | `Pocket Distortion` | `fx-pocket-distortion-a516163b.png` | `assets/img/plugins/fx-pocket-distortion.webp` |
  | `Pocket Limiter Classic` | `fx-pocket-limiter-classic-9f7b85ec.png` | `assets/img/plugins/fx-pocket-limiter-classic.webp` |
  | `Pocket Limiter` | `fx-pocket-limiter-78c822c4.png` | `assets/img/plugins/fx-pocket-limiter.webp` |
  | `Pocket Leveller` | `fx-pocket-leveller-9ec3e94d.png` | `assets/img/plugins/fx-pocket-leveller.webp` |
  | `Pocket Gate` | `fx-pocket-gate-62c77eab.png` | `assets/img/plugins/fx-pocket-gate.webp` |
  | `Pocket Trance Delay` | `fx-pocket-trance-delay-66c6fd82.png` | `assets/img/plugins/fx-pocket-trance-delay.webp` |
  | `Pocket Tape Delay` | `fx-pocket-tape-delay-0f54b1b3.png` | `assets/img/plugins/fx-pocket-tape-delay.webp` |
  | `Pocket MultiFX` | `fx-pocket-multifx-a84fac6f.png` | `assets/img/plugins/fx-pocket-multifx.webp` |
  | `Pocket Autoduck` | `fx-pocket-autoduck-46067e8e.png` | `assets/img/plugins/fx-pocket-autoduck.webp` |
  | `Pocket Chorus` | `fx-pocket-chorus-b213cc3e.png` | `assets/img/plugins/fx-pocket-chorus.webp` |
  | `Pocket Spacer` | `fx-pocket-spacer-392cfe80.png` | `assets/img/plugins/fx-pocket-spacer.webp` |
  | `Pocket Stereoizer` | `fx-pocket-stereoizer-a5e00518.png` | `assets/img/plugins/fx-pocket-stereoizer.webp` |
  | `Pocket Wow & Flutter` | `fx-pocket-wow--flutter-26cf9583.png` | `assets/img/plugins/fx-pocket-wow-flutter.webp` |
  | `Pocket Phaser` | `fx-pocket-phaser-71135523.png` | `assets/img/plugins/fx-pocket-phaser.webp` |
  | `Pocket Flanger` | `fx-pocket-flanger-555624c5.png` | `assets/img/plugins/fx-pocket-flanger.webp` |
  | `Pocket Tremolo` | `fx-pocket-tremolo-accd2ac8.png` | `assets/img/plugins/fx-pocket-tremolo.webp` |
  | `Pocket Waveshaper` | `fx-pocket-waveshaper-fb5855fc.png` | `assets/img/plugins/fx-pocket-waveshaper.webp` |
  | `Pocket Compressor` | `fx-pocket-compressor-16f7badd.png` | `assets/img/plugins/fx-pocket-compressor.webp` |
  | `Pocket Spreader` | `fx-pocket-spreader-513f9c08.png` | `assets/img/plugins/fx-pocket-spreader.webp` |
  | `Strike Shaper` | `fx-strike-shaper-f0a21b19.png` | `assets/img/plugins/fx-strike-shaper.webp` |
  | `Slicex` | `gen-slicex-3a81a870.png` | `assets/img/plugins/gen-slicex.webp` |
  | `FPC` | `gen-fpc-4469dc65.png` | `assets/img/plugins/gen-fpc.webp` |
  | `Instrument Rack` | `gen-instrument-rack-afeab4be.png` | `assets/img/plugins/gen-instrument-rack.webp` |
  | `MIDI Effect Rack` | `gen-midi-effect-rack-4e12ccdb.png` | `assets/img/plugins/gen-midi-effect-rack.webp` |
  | `Audio Effect Rack` | `fx-audio-effect-rack-4ff76714.png` | `assets/img/plugins/fx-audio-effect-rack.webp` |

  GMSynth (FLM) is not in the plugin picker (a channel gets it only from a `.flm`), so no mode of the program draws
  it: its `img` is empty and it keeps the name plate. Five 0.1.25 entries changed and their texts are updated; their
  pictures must be drawn again with the rest: `art/compressor-after-100.png` and `-200.png` (FET),
  `art/apex-limiter-after-100.png` and `-200.png` (Levelling), `art/lynx-tune-after-100.png` and `-200.png` (BREATH),
  `thumbs/gen-fruity-slicer-2a00813d.png` (envelope page: the thumb shows the plugin page only) and
  `thumbs/gen-fruity-dance-d1b027b0.png` (sprite sheet items).
- **Wiki**: updated for 0.1.26: Simple layout, Layouts, File formats, Coming from Ableton Live, Coming from FL
  Studio, Plugins, Mixer, Playlist, Piano roll, MIDI controllers, Online sessions, Recording, Settings, Shortcuts,
  Command line and Channel rack (Freeze). Not re-read against 0.1.26: Browser (Preview in time, similar samples),
  Exporting (one file per marker section, presets, For other apps), Automation, Importing projects, Audio setup,
  Troubleshooting, Tour, First song, Getting started, Install, Undo history. The pictures on the updated pages still
  show 0.1.25 windows; draw them again from the 0.1.26 build with `--snapshot` (the Settings window now has eight
  page tabs, so `assets/img/wiki/settings-general.webp` first). New sections that have no picture and would gain
  from one: Freeze (Channel rack), docked plugin windows and Connections (Plugins), Controller type and MPE
  controller (MIDI controllers), Share plugins with guests (Online sessions), LYNX extras (Settings).
- The Updates entry's `date` is 2026-11-11, the day it was written: set the release day.

## Other notes

- **Wiki pages**: one markdown file each, with `title`, `summary` and `status` (`full` or `short`) in the front
  matter; `_src/wiki/index.json` sets the four groups and the order (`hidden`: built but not listed; `catalog`: the
  group that also lists the plugin pages; `links`: other site pages listed in the group). The sidebar groups are
  `<details>`, so they fold without JavaScript; "On this page" is made from the article's `##` headings. Article
  shape: what it is, a numbered first task, tables for tools and keys, "Good to know", "Related". Write only what the released version does. Titles,
  summaries and the sidebar are translated; the bodies are English only for now (pages show a note in the reader's
  language).
- **Old addresses**: the site used to be one page. `/#features`, `/#download`, `/#source`, `/#signing` and
  `/#crash-reports` still exist on Home; `/#classics`, `/#try`, `/#media`, `/#gallery`, `/#plugins` and `/#dlPort`
  are sent on to their new page by a small script at the end of Home.
- **Download links** (`DL` in `assets/site.js`) run over `/releases?per_page=10` of the GitHub API; for each OS and
  format the newest non-draft release with a matching file wins: Windows `/setup.*\.exe$/i` (fallback `/\.exe$/i`)
  and `/windows.*\.zip$/i`; macOS `/macos.*\.dmg$/i` (fallback `/\.dmg$/i`); Linux `/\.AppImage$/i` and
  `/linux.*\.tar\.gz$/i`. Without JavaScript, or when the API fails, every button links to `releases/latest`.
  The visitor's OS (`navigator.userAgentData.platform`, then `navigator.platform` / `userAgent`; a touch "Mac" is
  iPadOS) only gets the accent colour. `?os=win|mac|linux|any` forces it (testing, screenshots).
- **Languages**: the same 15 as the app (ids and native names from `src/ui/I18n.cpp`). English is in the pages; the
  others are `i18n/<id>.json` (`ui` strings, `featured` and `insp` plugin descriptions) and `i18n/updates/<id>.json`
  (the Updates page) and `i18n/catalog/<id>.json` (the plugin cards), fetched only for the chosen language. `i18n/latest/<id>.json` is written by the build from the
  updates file. Order: `?lang=` (`pt` still works) > saved choice > `navigator.languages` > English. A string is
  translatable when its element has `data-i18n="key"` (`data-i18n-alt`, `data-i18n-aria`, `data-i18n-ph` for attributes;
  `data-args='["x"]'` fills `{0}`); a new English text needs a new key. CJK pages use the system font first and
  fetch Noto Sans only when it is missing.
- **Features** no longer lists plugins: its two plugin sections (`#plugins`, `#classics`, ids kept) summarise and link
  to the catalogue and to `docs/PLUGINS.md` at the release tag. `_src/plugins.js` is not read by the build any more;
  it stays only because `scripts/gen-inspired.py` writes its `INSPIRED` block (remove both together if wanted).
- **Screenshots** come from LYNX SHARE's own showcase project (`LYNX.exe --showcase Demo.lynx`, then
  `LYNX.exe --snapshot <prefix> Demo.lynx` with `LYNX_SNAPSHOT_SIZE=1920x1080`), never from Image-Line demo projects.
  `assets/media/demo.mp3` is `LYNX.exe --export Demo.lynx demo.wav`; `lynx-tour.mp4` is a slideshow of the
  screenshots over that audio; `peaks.json` is its waveform (600 peaks).
- `?shot` in the address is kept for headless screenshots (nothing on the pages animates on load any more).
- Publish with `scripts/deploy-site.sh` (Hostinger API token in `~/.config/hostinger/token`, never committed).
